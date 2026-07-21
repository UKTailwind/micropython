tz(1)              # your offset from UTC, in hours (fractions allowed)
ntpsync()          # fetch the time, apply tz, set system clock AND DS3231
auto(True)         # ...and do that automatically at every boot
