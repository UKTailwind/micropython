# your offset from UTC, in hours (fractions allowed)
tz(1)
# fetch the time, apply tz, set system clock AND DS3231
ntpsync()
auto(True)         # ...and do that automatically at every boot
