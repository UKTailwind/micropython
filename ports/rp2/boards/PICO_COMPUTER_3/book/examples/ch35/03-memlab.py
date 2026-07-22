import gc
print(gc.mem_free())               # bytes available right now
# take out the recycling, on demand
gc.collect()
print(gc.mem_free())               # ...usually more
