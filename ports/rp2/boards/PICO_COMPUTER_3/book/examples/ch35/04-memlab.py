import gc

gc.collect()
start = gc.mem_free()
print(f"free at start:      {start}")

junk = [list(range(10)) for _ in range(2000)]
after = gc.mem_free()
print(f"after 2000 lists:   {after}   (cost {start - after})")

junk = None                        # drop the only reference...
gc.collect()                       # ...and reclaim
end = gc.mem_free()
print(f"after collect:      {end}")
