/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND...
 */

// `btaudio`: Bluetooth Classic A2DP SOURCE (stream audio OUT to a Bluetooth
// speaker/headset). This is a different Bluetooth transport from the board's
// existing BLE support (aioble, keyboard/mouse) -- A2DP is Classic Bluetooth
// (BR/EDR), which MicroPython's `bluetooth` module has no bindings for at
// all. This file adds just enough of BTstack's own classic stack (already
// vendored in lib/btstack, and already used in full by pico-sdk's own
// pico_btstack_classic target -- this file is adapted directly from BTstack's
// official a2dp_source_demo.c example, trimmed to source-only, no AVRCP) to
// send a live PCM stream fed from Python out over A2DP as SBC audio.
//
// IMPORTANT shared-stack constraint: MicroPython's own `bluetooth` module
// (modbluetooth_btstack.c) already owns hci_init()/l2cap_init()/sm_init() and
// HCI power control (via bluetooth.BLE().active(True)). This file must NOT
// call any of those again -- classic and LE share one BTstack/HCI instance,
// dual-mode by design. btaudio_ensure_init() below only adds the classic-only
// pieces (A2DP source, SDP) on top of whatever's already running, and every
// entry point checks hci_get_state() == HCI_STATE_WORKING first, raising a
// clear error if bluetooth.BLE().active(True) hasn't been called yet.
//
// Not yet tested on real hardware -- written against BTstack's official demo
// and API headers, but there is no Bluetooth speaker in hand yet to verify
// against. Expect this to need iteration (SBC bitpool/buffer tuning, timing)
// once real audio is heard.
//
//   import bluetooth
//   bluetooth.BLE().active(True)   # brings up the shared HCI/L2CAP stack
//   import btaudio
//   btaudio.on_found(lambda addr, name, rssi: print(name, rssi))
//   btaudio.scan(8000)             # 8 second classic inquiry
//   btaudio.on_connect(lambda ok: print("connected" if ok else "disconnected"))
//   btaudio.connect(addr)          # addr: 6-byte bytes from on_found
//   ...
//   btaudio.write(pcm_bytes)       # interleaved 16-bit LE stereo PCM
//   btaudio.writable()             # free space (bytes) in the feed buffer

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/ringbuf.h"

#if MICROPY_HW_ENABLE_BT_A2DP

#include <string.h>
#include "btstack.h"

#define NUM_CHANNELS 2
#define BYTES_PER_SAMPLE (2 * NUM_CHANNELS)  // 16-bit stereo
#define AUDIO_TIMEOUT_MS 10
#define SBC_STORAGE_SIZE 1030
// ~46ms of 44.1kHz 16-bit stereo audio. Kept deliberately small: this
// board's SRAM GC arena is already tuned to a tight minimum (see the
// ASSERT in memmap_rp2350/section_extra_post_platform_end.incl -- adding
// classic Bluetooth's own static buffers already ate most of the slack
// upstream left). Revisit upward only alongside checking that assert still
// holds, and only if real playback shows underruns needing more slack.
#define PCM_RINGBUF_SIZE (4096)

typedef struct {
    uint16_t a2dp_cid;
    uint8_t local_seid;
    uint8_t remote_seid;
    bool stream_opened;
    bool streaming;

    uint32_t time_audio_data_sent;  // ms
    uint32_t acc_num_missed_samples;
    uint32_t samples_ready;
    btstack_timer_source_t audio_timer;
    int max_media_payload_size;
    uint32_t rtp_timestamp;

    uint8_t sbc_storage[SBC_STORAGE_SIZE];
    uint16_t sbc_storage_count;
    bool sbc_ready_to_send;
} a2dp_media_sending_context_t;

// SBC 44100 Hz, stereo; block length 16 / subbands 8 / Loudness allocation
// (0xFF byte, per BTstack's own demo -- the sink negotiates the specifics),
// min/max bitpool 2/53. Matches BTstack's a2dp_source_demo.c exactly.
static uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO,
    0xFF,
    2, 53,
};

static bool btaudio_inited = false;
static a2dp_media_sending_context_t media_tracker;
static btstack_sbc_encoder_state_t sbc_encoder_state;
static avdtp_configuration_sbc_t sbc_configuration;
static uint32_t negotiated_sample_rate;

static btstack_packet_callback_registration_t hci_event_callback_registration;
static uint8_t sdp_a2dp_source_service_buffer[150];

// Raw PCM feed from Python (write()); consumed by the audio timer, which
// pulls exactly the number of samples elapsed real time calls for, per
// BTstack's own pacing approach (a2dp_demo_audio_timeout_handler). Backed by
// a plain byte ring buffer (single producer: write() from the VM; single
// consumer: the BTstack run-loop timer -- both run cooperatively on the one
// core, same safety argument as usb_cdc.c's RX ringbuf).
static ringbuf_t pcm_rb;
static uint8_t pcm_rb_store[PCM_RINGBUF_SIZE];

// Rooted (MP_REGISTER_ROOT_POINTER below) so the GC keeps these alive --
// they're the only reference to the Python callback once registered.
static void notify_connect(bool connected) {
    mp_obj_t cb = MP_STATE_PORT(btaudio_connect_cb);
    if (cb != MP_OBJ_NULL && cb != mp_const_none) {
        mp_sched_schedule(cb, mp_obj_new_bool(connected));
    }
}

// --- SBC encode + send, ported from a2dp_demo_send_media_packet /
// a2dp_demo_fill_sbc_audio_buffer / a2dp_demo_audio_timeout_handler ---------

// Pull `num_samples` stereo frames out of the PCM ring buffer into
// `pcm_buffer` (interleaved L/R int16). Zero-fills on underrun rather than
// stalling the timer/encoder -- a brief silence gap is far less disruptive
// than desyncing the whole A2DP timing state machine while Python catches up.
static void pull_pcm(int16_t *pcm_buffer, int num_samples) {
    size_t want = (size_t)num_samples * BYTES_PER_SAMPLE;
    size_t have = ringbuf_avail(&pcm_rb);
    size_t take = (have < want) ? have : want;
    uint8_t *dest = (uint8_t *)pcm_buffer;
    for (size_t i = 0; i < take; i++) {
        dest[i] = (uint8_t)ringbuf_get(&pcm_rb);
    }
    if (take < want) {
        memset(dest + take, 0, want - take);
    }
}

static int fill_sbc_audio_buffer(a2dp_media_sending_context_t *context) {
    int total_num_bytes_read = 0;
    unsigned int num_audio_samples_per_sbc_buffer = btstack_sbc_encoder_num_audio_frames();
    while (context->samples_ready >= num_audio_samples_per_sbc_buffer
        && (unsigned int)(context->max_media_payload_size - context->sbc_storage_count) >= btstack_sbc_encoder_sbc_buffer_length()) {
        int16_t pcm_frame[256 * NUM_CHANNELS];
        pull_pcm(pcm_frame, num_audio_samples_per_sbc_buffer);
        btstack_sbc_encoder_process_data(pcm_frame);

        uint16_t sbc_frame_size = btstack_sbc_encoder_sbc_buffer_length();
        uint8_t *sbc_frame = btstack_sbc_encoder_sbc_buffer();

        total_num_bytes_read += num_audio_samples_per_sbc_buffer;
        // first byte in sbc storage is the SBC media header (frame count)
        memcpy(&context->sbc_storage[1 + context->sbc_storage_count], sbc_frame, sbc_frame_size);
        context->sbc_storage_count += sbc_frame_size;
        context->samples_ready -= num_audio_samples_per_sbc_buffer;
    }
    return total_num_bytes_read;
}

static void send_media_packet(void) {
    int num_bytes_in_frame = btstack_sbc_encoder_sbc_buffer_length();
    int bytes_in_storage = media_tracker.sbc_storage_count;
    uint8_t num_sbc_frames = bytes_in_storage / num_bytes_in_frame;
    media_tracker.sbc_storage[0] = num_sbc_frames;
    a2dp_source_stream_send_media_payload_rtp(media_tracker.a2dp_cid, media_tracker.local_seid, 0,
        media_tracker.rtp_timestamp, media_tracker.sbc_storage, bytes_in_storage + 1);

    unsigned int num_audio_samples_per_sbc_buffer = btstack_sbc_encoder_num_audio_frames();
    media_tracker.rtp_timestamp += num_sbc_frames * num_audio_samples_per_sbc_buffer;
    media_tracker.sbc_storage_count = 0;
    media_tracker.sbc_ready_to_send = false;
}

static void audio_timeout_handler(btstack_timer_source_t *timer) {
    a2dp_media_sending_context_t *context = (a2dp_media_sending_context_t *)btstack_run_loop_get_timer_context(timer);
    btstack_run_loop_set_timer(&context->audio_timer, AUDIO_TIMEOUT_MS);
    btstack_run_loop_add_timer(&context->audio_timer);
    uint32_t now = btstack_run_loop_get_time_ms();

    uint32_t update_period_ms = AUDIO_TIMEOUT_MS;
    if (context->time_audio_data_sent > 0) {
        update_period_ms = now - context->time_audio_data_sent;
    }

    uint32_t num_samples = (update_period_ms * negotiated_sample_rate) / 1000;
    context->acc_num_missed_samples += (update_period_ms * negotiated_sample_rate) % 1000;
    while (context->acc_num_missed_samples >= 1000) {
        num_samples++;
        context->acc_num_missed_samples -= 1000;
    }
    context->time_audio_data_sent = now;
    context->samples_ready += num_samples;

    if (context->sbc_ready_to_send) {
        return;
    }

    fill_sbc_audio_buffer(context);

    if ((unsigned int)(context->sbc_storage_count + btstack_sbc_encoder_sbc_buffer_length()) > (unsigned int)context->max_media_payload_size) {
        context->sbc_ready_to_send = true;
        a2dp_source_stream_endpoint_request_can_send_now(context->a2dp_cid, context->local_seid);
    }
}

static void audio_timer_start(a2dp_media_sending_context_t *context) {
    context->max_media_payload_size = btstack_min(a2dp_max_media_payload_size(context->a2dp_cid, context->local_seid), SBC_STORAGE_SIZE);
    context->sbc_storage_count = 0;
    context->sbc_ready_to_send = false;
    context->streaming = true;
    btstack_run_loop_remove_timer(&context->audio_timer);
    btstack_run_loop_set_timer_handler(&context->audio_timer, audio_timeout_handler);
    btstack_run_loop_set_timer_context(&context->audio_timer, context);
    btstack_run_loop_set_timer(&context->audio_timer, AUDIO_TIMEOUT_MS);
    btstack_run_loop_add_timer(&context->audio_timer);
}

static void audio_timer_stop(a2dp_media_sending_context_t *context) {
    context->time_audio_data_sent = 0;
    context->acc_num_missed_samples = 0;
    context->samples_ready = 0;
    context->streaming = false;
    context->sbc_storage_count = 0;
    context->sbc_ready_to_send = false;
    btstack_run_loop_remove_timer(&context->audio_timer);
}

// --- A2DP + classic HCI event handling, ported from a2dp_source_demo.c's
// a2dp_source_packet_handler / hci_packet_handler, AVRCP/reconfigure-free ---

static void a2dp_source_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    uint8_t status;
    bd_addr_t address;
    uint16_t cid;
    avdtp_channel_mode_t channel_mode;
    uint8_t allocation_method;

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) {
        return;
    }

    switch (hci_event_a2dp_meta_get_subevent_code(packet)) {
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
            a2dp_subevent_signaling_connection_established_get_bd_addr(packet, address);
            cid = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
            status = a2dp_subevent_signaling_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                mp_printf(&mp_plat_print, "btaudio: connection failed, status 0x%02x\n", status);
                media_tracker.a2dp_cid = 0;
                notify_connect(false);
                break;
            }
            media_tracker.a2dp_cid = cid;
            mp_printf(&mp_plat_print, "btaudio: signaling connected to %s\n", bd_addr_to_str(address));
            break;

        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
            cid = avdtp_subevent_signaling_media_codec_sbc_configuration_get_avdtp_cid(packet);
            if (cid != media_tracker.a2dp_cid) {
                return;
            }
            media_tracker.remote_seid = a2dp_subevent_signaling_media_codec_sbc_configuration_get_remote_seid(packet);
            sbc_configuration.sampling_frequency = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
            sbc_configuration.block_length = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet);
            sbc_configuration.subbands = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet);
            sbc_configuration.min_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(packet);
            sbc_configuration.max_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet);
            channel_mode = (avdtp_channel_mode_t)a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet);
            allocation_method = a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet);

            negotiated_sample_rate = sbc_configuration.sampling_frequency;
            sbc_configuration.allocation_method = (btstack_sbc_allocation_method_t)(allocation_method - 1);
            switch (channel_mode) {
                case AVDTP_CHANNEL_MODE_JOINT_STEREO:
                    sbc_configuration.channel_mode = SBC_CHANNEL_MODE_JOINT_STEREO;
                    break;
                case AVDTP_CHANNEL_MODE_STEREO:
                    sbc_configuration.channel_mode = SBC_CHANNEL_MODE_STEREO;
                    break;
                case AVDTP_CHANNEL_MODE_DUAL_CHANNEL:
                    sbc_configuration.channel_mode = SBC_CHANNEL_MODE_DUAL_CHANNEL;
                    break;
                default:
                    sbc_configuration.channel_mode = SBC_CHANNEL_MODE_MONO;
                    break;
            }
            mp_printf(&mp_plat_print, "btaudio: SBC configured, %lu Hz\n", (unsigned long)negotiated_sample_rate);
            btstack_sbc_encoder_init(&sbc_encoder_state, SBC_MODE_STANDARD,
                sbc_configuration.block_length, sbc_configuration.subbands,
                sbc_configuration.allocation_method, sbc_configuration.sampling_frequency,
                sbc_configuration.max_bitpool_value, sbc_configuration.channel_mode);
            break;

        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            status = a2dp_subevent_stream_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                mp_printf(&mp_plat_print, "btaudio: stream failed, status 0x%02x\n", status);
                notify_connect(false);
                break;
            }
            media_tracker.stream_opened = true;
            a2dp_source_start_stream(media_tracker.a2dp_cid, media_tracker.local_seid);
            break;

        case A2DP_SUBEVENT_STREAM_STARTED:
            audio_timer_start(&media_tracker);
            mp_printf(&mp_plat_print, "btaudio: streaming\n");
            notify_connect(true);
            break;

        case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
            send_media_packet();
            break;

        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            audio_timer_stop(&media_tracker);
            break;

        case A2DP_SUBEVENT_STREAM_RELEASED:
            cid = a2dp_subevent_stream_released_get_a2dp_cid(packet);
            if (cid == media_tracker.a2dp_cid) {
                media_tracker.stream_opened = false;
            }
            audio_timer_stop(&media_tracker);
            notify_connect(false);
            break;

        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            cid = a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet);
            if (cid == media_tracker.a2dp_cid) {
                media_tracker.a2dp_cid = 0;
            }
            break;

        default:
            break;
    }
}

// GAP inquiry (device scan) + legacy PIN fallback. Registered on the shared
// HCI event handler list alongside whatever modbluetooth_btstack.c already
// has there -- BTstack supports multiple independent handlers, same pattern
// mp_usbh.c uses for HID/CDC/MSC on the USB host side.
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    bd_addr_t address;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_PIN_CODE_REQUEST:
            // Fallback for older/simple speakers still using legacy pairing
            // rather than Secure Simple Pairing. "0000" is the de facto
            // standard fixed PIN for headset/speaker-class devices.
            hci_event_pin_code_request_get_bd_addr(packet, address);
            gap_pin_code_response(address, "0000");
            break;
        case GAP_EVENT_INQUIRY_RESULT: {
            gap_event_inquiry_result_get_bd_addr(packet, address);
            mp_obj_t found_cb = MP_STATE_PORT(btaudio_found_cb);
            if (found_cb != MP_OBJ_NULL && found_cb != mp_const_none) {
                mp_obj_t name_obj = mp_const_none;
                if (gap_event_inquiry_result_get_name_available(packet)) {
                    int name_len = gap_event_inquiry_result_get_name_len(packet);
                    name_obj = mp_obj_new_str((const char *)gap_event_inquiry_result_get_name(packet), name_len);
                }
                int8_t rssi = 0;
                if (gap_event_inquiry_result_get_rssi_available(packet)) {
                    rssi = (int8_t)gap_event_inquiry_result_get_rssi(packet);
                }
                mp_obj_t items[3] = {
                    mp_obj_new_bytes(address, 6),
                    name_obj,
                    MP_OBJ_NEW_SMALL_INT(rssi),
                };
                mp_sched_schedule(found_cb, mp_obj_new_tuple(3, items));
            }
            break;
        }
        default:
            break;
    }
}

// One-time classic-only setup, added on top of whatever HCI/L2CAP instance
// modbluetooth_btstack.c already brought up. Deliberately does NOT call
// hci_init()/l2cap_init()/hci_power_control() -- see file header.
static void btaudio_ensure_init(void) {
    if (btaudio_inited) {
        return;
    }

    a2dp_source_init();
    a2dp_source_register_packet_handler(&a2dp_source_packet_handler);

    avdtp_stream_endpoint_t *local_stream_endpoint = a2dp_source_create_stream_endpoint(
        AVDTP_AUDIO, AVDTP_CODEC_SBC,
        media_sbc_codec_capabilities, sizeof(media_sbc_codec_capabilities),
        NULL, 0);
    if (local_stream_endpoint != NULL) {
        media_tracker.local_seid = avdtp_local_seid(local_stream_endpoint);
        avdtp_source_register_delay_reporting_category(media_tracker.local_seid);
    }

    sdp_init();
    memset(sdp_a2dp_source_service_buffer, 0, sizeof(sdp_a2dp_source_service_buffer));
    a2dp_source_create_sdp_record(sdp_a2dp_source_service_buffer, 0x10001,
        AVDTP_SOURCE_FEATURE_MASK_PLAYER, NULL, NULL);
    sdp_register_service(sdp_a2dp_source_service_buffer);

    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    pcm_rb.buf = pcm_rb_store;
    pcm_rb.size = sizeof(pcm_rb_store);
    pcm_rb.iget = pcm_rb.iput = 0;

    btaudio_inited = true;
}

static void raise_if_not_working(void) {
    if (hci_get_state() != HCI_STATE_WORKING) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("bluetooth.BLE().active(True) must be called first"));
    }
}

// --- Python bindings --------------------------------------------------------

static mp_obj_t btaudio_scan(size_t n_args, const mp_obj_t *args) {
    raise_if_not_working();
    btaudio_ensure_init();
    mp_int_t duration_ms = (n_args >= 1) ? mp_obj_get_int(args[0]) : 8000;
    // BTstack's inquiry duration is in 1.28s units, 1..48.
    uint8_t units = (uint8_t)btstack_max(1, btstack_min(48, duration_ms / 1280));
    gap_inquiry_start(units);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(btaudio_scan_obj, 0, 1, btaudio_scan);

static mp_obj_t btaudio_stop_scan(void) {
    gap_inquiry_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(btaudio_stop_scan_obj, btaudio_stop_scan);

static mp_obj_t btaudio_on_found(mp_obj_t fn) {
    MP_STATE_PORT(btaudio_found_cb) = (fn == mp_const_none) ? MP_OBJ_NULL : fn;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(btaudio_on_found_obj, btaudio_on_found);

static mp_obj_t btaudio_on_connect(mp_obj_t fn) {
    MP_STATE_PORT(btaudio_connect_cb) = (fn == mp_const_none) ? MP_OBJ_NULL : fn;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(btaudio_on_connect_obj, btaudio_on_connect);

static mp_obj_t btaudio_connect(mp_obj_t addr_obj) {
    raise_if_not_working();
    btaudio_ensure_init();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(addr_obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len != 6) {
        mp_raise_ValueError(MP_ERROR_TEXT("address must be 6 bytes"));
    }
    bd_addr_t addr;
    memcpy(addr, bufinfo.buf, 6);
    uint8_t status = a2dp_source_establish_stream(addr, &media_tracker.a2dp_cid);
    if (status != ERROR_CODE_SUCCESS) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("a2dp connect failed, status 0x%02x"), status);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(btaudio_connect_obj, btaudio_connect);

static mp_obj_t btaudio_disconnect(void) {
    if (media_tracker.a2dp_cid) {
        a2dp_source_disconnect(media_tracker.a2dp_cid);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(btaudio_disconnect_obj, btaudio_disconnect);

static mp_obj_t btaudio_connected(void) {
    return mp_obj_new_bool(media_tracker.streaming);
}
static MP_DEFINE_CONST_FUN_OBJ_0(btaudio_connected_obj, btaudio_connected);

static mp_obj_t btaudio_sample_rate(void) {
    return MP_OBJ_NEW_SMALL_INT(media_tracker.streaming ? negotiated_sample_rate : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(btaudio_sample_rate_obj, btaudio_sample_rate);

// write(buf) -- feed interleaved 16-bit LE stereo PCM. Returns bytes accepted
// (less than len(buf) if the feed buffer is full; caller should back off and
// retry, checking writable() to avoid busy-looping).
static mp_obj_t btaudio_write(mp_obj_t buf_obj) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_obj, &bufinfo, MP_BUFFER_READ);
    const uint8_t *src = bufinfo.buf;
    size_t n = 0;
    while (n < bufinfo.len && ringbuf_put(&pcm_rb, src[n]) >= 0) {
        n++;
    }
    return MP_OBJ_NEW_SMALL_INT(n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(btaudio_write_obj, btaudio_write);

static mp_obj_t btaudio_writable(void) {
    return MP_OBJ_NEW_SMALL_INT(ringbuf_free(&pcm_rb));
}
static MP_DEFINE_CONST_FUN_OBJ_0(btaudio_writable_obj, btaudio_writable);

static const mp_rom_map_elem_t btaudio_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_btaudio) },
    { MP_ROM_QSTR(MP_QSTR_scan), MP_ROM_PTR(&btaudio_scan_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop_scan), MP_ROM_PTR(&btaudio_stop_scan_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_found), MP_ROM_PTR(&btaudio_on_found_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_connect), MP_ROM_PTR(&btaudio_on_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&btaudio_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_disconnect), MP_ROM_PTR(&btaudio_disconnect_obj) },
    { MP_ROM_QSTR(MP_QSTR_connected), MP_ROM_PTR(&btaudio_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_sample_rate), MP_ROM_PTR(&btaudio_sample_rate_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&btaudio_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_writable), MP_ROM_PTR(&btaudio_writable_obj) },
};
static MP_DEFINE_CONST_DICT(btaudio_module_globals, btaudio_module_globals_table);

const mp_obj_module_t btaudio_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&btaudio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_btaudio, btaudio_module);
MP_REGISTER_ROOT_POINTER(mp_obj_t btaudio_found_cb);
MP_REGISTER_ROOT_POINTER(mp_obj_t btaudio_connect_cb);

#endif // MICROPY_HW_ENABLE_BT_A2DP
