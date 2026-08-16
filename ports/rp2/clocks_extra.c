/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "pico.h"
#include "pico/runtime_init.h"
#include "clocks_extra.h"
#include "hardware/regs/clocks.h"
#include "hardware/platform_defs.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/pll.h"
#include "hardware/xosc.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/ticks.h"
#include "hardware/vreg.h"
#include "hardware/timer.h"     // busy_wait_us_32, for the voltage settle
#if HAS_RP2040_RTC
#include "hardware/rtc.h"
#endif

// Core voltage for a target clk_sys, following MMBasic's ladder in
// PicoMite.c so a board behaves the same under both firmwares.
//
// The RP2350 comes out of reset at 1.10 V, which is not enough for the
// speeds this port runs at. The Pico Computer 3 itself does not depend on
// this - DVDD there comes from an external 1.3 V regulator and the internal
// one is not what feeds the core - but a CLONE built on a stock module such
// as the Pimoroni PGA2350 has only the chip's own regulator, and at 252 MHz
// on 1.10 V it simply does not run. That is the reported failure.
//
// MMBasic uses 1.60 V above 320 MHz. This uses 1.40 V: enough headroom over
// the 1.30 V the PC3 supplies externally at 378 MHz, without asking a clone
// to dissipate what MMBasic asks for until someone reports needing it.
//
// vreg_disable_voltage_limit() first because anything above 1.30 V is behind
// POWMAN's limit - the SDK's VREG_VOLTAGE_MAX is 1.30 and vreg_set_voltage()
// would otherwise clamp.
void set_core_voltage_for_khz(uint32_t khz) {
    enum vreg_voltage v;

    if (khz <= 200000) {
        v = VREG_VOLTAGE_1_15;
    } else if (khz <= 320000) {
        v = VREG_VOLTAGE_1_30;
    } else {
        v = VREG_VOLTAGE_1_40;
    }
    vreg_disable_voltage_limit();
    vreg_set_voltage(v);
    // It must SETTLE before anything runs at the new speed. MMBasic waits
    // 10 ms here and so do we; a busy wait rather than sleep_ms so this is
    // safe to call before the alarm pool exists and with interrupts off.
    busy_wait_us_32(10000);
}

static void start_all_ticks(void) {
    uint32_t cycles = clock_get_hz(clk_ref) / MHZ;
    // Note RP2040 has a single tick generator in the watchdog which serves
    // watchdog, system timer and M0+ SysTick; The tick generator is clocked from clk_ref
    // but is now adapted by the hardware_ticks library for compatibility with RP2350
    // npte: hardware_ticks library now provides an adapter for RP2040

    for (int i = 0; i < (int)TICK_COUNT; ++i) {
        tick_start((tick_gen_num_t)i, cycles);
    }
}

// Override the SDK's __weak runtime_init_clocks() with the USB-always variant.
void runtime_init_clocks(void) {
    runtime_init_clocks_optional_usb(true);
}

// Copy of runtime_init_clocks() from pico-sdk, with USB
// PLL and clock init made optional (for light sleep wakeup).
void runtime_init_clocks_optional_usb(bool init_usb) {
    // Disable resus that may be enabled from previous software
    clocks_hw->resus.ctrl = 0;

    // Enable the xosc
    xosc_init();

    // Before we touch PLLs, switch sys and ref cleanly away from their aux sources.
    hw_clear_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (clocks_hw->clk[clk_sys].selected != 0x1) {
        tight_loop_contents();
    }
    hw_clear_bits(&clocks_hw->clk[clk_ref].ctrl, CLOCKS_CLK_REF_CTRL_SRC_BITS);
    while (clocks_hw->clk[clk_ref].selected != 0x1) {
        tight_loop_contents();
    }

    /// \tag::pll_init[]
    pll_init(pll_sys, PLL_SYS_REFDIV, PLL_SYS_VCO_FREQ_HZ, PLL_SYS_POSTDIV1, PLL_SYS_POSTDIV2);
    if (init_usb) {
        pll_init(pll_usb, PLL_USB_REFDIV, PLL_USB_VCO_FREQ_HZ, PLL_USB_POSTDIV1, PLL_USB_POSTDIV2);
    }
    /// \end::pll_init[]

    // Configure clocks

    // RP2040 CLK_REF = XOSC (usually) 12MHz / 1 = 12MHz
    // RP2350 CLK_REF = XOSC (XOSC_MHZ) / N (1,2,4) = 12MHz

    // clk_ref aux select is 0 because:
    //
    // - RP2040: no aux mux on clk_ref, so this field is don't-care.
    //
    // - RP2350: there is an aux mux, but we are selecting one of the
    //   non-aux inputs to the glitchless mux, so the aux select doesn't
    //   matter. The value of 0 here happens to be the sys PLL.

    clock_configure_undivided(clk_ref,
        CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
        0,             // No aux mux
        XOSC_HZ);

    // This must be done after we've configured CLK_REF to XOSC due to the need to time a delay
    #if SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST && defined(SYS_CLK_VREG_VOLTAGE_MIN)
    if (vreg_get_voltage() < SYS_CLK_VREG_VOLTAGE_MIN) {
        vreg_set_voltage(SYS_CLK_VREG_VOLTAGE_MIN);
        // wait for voltage to settle; must use CPU cycles as TIMER is not yet clocked correctly
        busy_wait_at_least_cycles((uint32_t)((SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US * (uint64_t)XOSC_HZ) / 1000000));
    }
    #endif

    /// \tag::configure_clk_sys[]
    // CLK SYS = PLL SYS (usually) 125MHz / 1 = 125MHz
    clock_configure_undivided(clk_sys,
        CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
        CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        SYS_CLK_HZ);
    /// \end::configure_clk_sys[]

    if (init_usb) {
        // CLK USB = PLL USB 48MHz / 1 = 48MHz
        clock_configure_undivided(clk_usb,
            0, // No GLMUX
            CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
            USB_CLK_HZ);
    }

    // CLK ADC = PLL USB 48MHZ / 1 = 48MHz
    clock_configure_undivided(clk_adc,
        0,             // No GLMUX
        CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
        USB_CLK_HZ);

    #if HAS_RP2040_RTC
    // CLK RTC = PLL USB 48MHz / 1024 = 46875Hz
    #if (USB_CLK_HZ % RTC_CLOCK_FREQ_HZ == 0)
    // this doesn't pull in 64 bit arithmetic
    clock_configure_int_divider(clk_rtc,
        0,             // No GLMUX
        CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
        USB_CLK_HZ,
        USB_CLK_HZ / RTC_CLOCK_FREQ_HZ);
    #else
    clock_configure(clk_rtc,
        0,             // No GLMUX
        CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
        USB_CLK_HZ,
        RTC_CLOCK_FREQ_HZ);
    #endif
    #endif

    // CLK PERI = clk_sys. Used as reference clock for UART and SPI serial.
    clock_configure_undivided(clk_peri,
        0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
        SYS_CLK_HZ);

    #if HAS_HSTX
    // CLK_HSTX = clk_sys. Transmit bit clock for the HSTX peripheral.
    clock_configure_undivided(clk_hstx,
        0,
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
        SYS_CLK_HZ);
    #endif

    // Finally, all clocks are configured so start the ticks
    // The ticks use clk_ref so now that is configured we can start them
    start_all_ticks();
}
