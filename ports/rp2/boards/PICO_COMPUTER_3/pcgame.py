# Game-loop timing for the Pico Computer 3 -- a drift-free fixed-cadence clock
# (MMBasic's SYNC), so animation and game loops run at a steady rate.
#
#     import pcgame, hdmi
#     clock = pcgame.Clock(60)               # aim for 60 frames/second
#     while playing:
#         dt = clock.tick()                  # wait for the next frame; dt = seconds
#         update(dt)                          # move things by dt (frame-rate independent)
#         draw()
#         print(clock.fps)                    # measured frames/second
#
# `Clock.tick()` holds an absolute deadline and waits until it, then advances the
# deadline by exactly one period -- so a slow frame is not "paid back" with drift,
# the cadence stays anchored to the clock (exactly like SYNC). Pass vsync=True to
# lock to the HDMI refresh instead (tear-free, at the display's own rate).

import time


class Clock:
    def __init__(self, fps=60, vsync=False):
        self.vsync = vsync
        self.set_fps(fps)
        now = time.ticks_us()
        self._end = time.ticks_add(now, self.period_us)
        self._last = now
        self._fcount = 0
        self._fbase = now
        self._fps = 0.0

    def set_fps(self, fps):
        """Change the target frame rate (frames per second)."""
        self.target = fps
        self.period_us = int(1000000 / fps) if fps > 0 else 0

    def tick(self):
        """Wait until the next frame is due, then return the time (seconds) since
        the previous tick(). Call once per frame."""
        if self.vsync:
            import hdmi

            hdmi.vsync()
        elif self.period_us:
            delay = time.ticks_diff(self._end, time.ticks_us())
            if delay > 0:
                # Sleep the bulk (saves power), then spin the last ~1 ms so the
                # deadline is hit to the microsecond.
                if delay > 1500:
                    time.sleep_us(delay - 1000)
                while time.ticks_diff(self._end, time.ticks_us()) > 0:
                    pass
            self._end = time.ticks_add(self._end, self.period_us)
            # If a frame overran by more than a whole period, restart the cadence
            # from now instead of trying to claw back the backlog.
            if time.ticks_diff(self._end, time.ticks_us()) < -self.period_us:
                self._end = time.ticks_add(time.ticks_us(), self.period_us)

        now = time.ticks_us()
        dt = time.ticks_diff(now, self._last) / 1000000
        self._last = now
        # Rolling frames-per-second, updated about once a second.
        self._fcount += 1
        elapsed = time.ticks_diff(now, self._fbase)
        if elapsed >= 1000000:
            self._fps = self._fcount * 1000000.0 / elapsed
            self._fcount = 0
            self._fbase = now
        return dt

    @property
    def fps(self):
        """Measured frames per second (updated ~once a second)."""
        return self._fps

    def reset(self):
        """Re-anchor the cadence to now (e.g. after a pause)."""
        now = time.ticks_us()
        self._end = time.ticks_add(now, self.period_us)
        self._last = now
