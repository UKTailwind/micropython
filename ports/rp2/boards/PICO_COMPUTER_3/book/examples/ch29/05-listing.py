import ds3231
# 07:00, every day, survives resets
ds3231.set_alarm(7, 0)
if ds3231.alarm_fired():      # poll this...
    ds3231.clear_alarm()      # ...and acknowledge
