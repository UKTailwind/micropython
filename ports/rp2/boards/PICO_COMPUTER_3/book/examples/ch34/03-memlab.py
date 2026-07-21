import gc
print(gc.mem_free())               # bytes available right now
gc.collect()                       # take out the recycling, on demand
print(gc.mem_free())               # ...usually more
