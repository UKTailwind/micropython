import asyncio

async def drummer():
    while True:
        print("boom")
        await asyncio.sleep_ms(400)

async def singer():
    while True:
        print("      la")
        await asyncio.sleep_ms(650)

async def countdown():
    for n in range(10, 0, -1):
        print(f"            {n}")
        await asyncio.sleep(1)
    print("            LIFT OFF")

async def main():
    asyncio.create_task(drummer())
    asyncio.create_task(singer())
    await countdown()                # main() ends when this ends

asyncio.run(main())
