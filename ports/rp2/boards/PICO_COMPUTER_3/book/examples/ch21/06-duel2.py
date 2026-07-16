import time
import random
import keyboard

def flush():
    while keydown(1):
        time.sleep(0.01)          # wait for all fingers off

print("REACTION DUEL -- player 1: A    player 2: L")
print("Wait for the GO beep. Too early loses the round. Five rounds.")

scores = {1: 0, 2: 0}

for rnd in range(1, 6):
    print(f"\nRound {rnd}: hands ready...")
    flush()
    wait_ms = random.randint(1500, 4000)
    t0 = time.ticks_ms()
    early = 0
    while time.ticks_diff(time.ticks_ms(), t0) < wait_ms:
        k = keydown(1)
        if k == ord("a"):
            early = 1
            break
        if k == ord("l"):
            early = 2
            break
    if early:
        winner = 2 if early == 1 else 1
        print(f"Player {early} jumped the gun! Point to player {winner}.")
        scores[winner] += 1
        continue

    beep(1200, 60)
    print("GO!")
    t0 = time.ticks_ms()
    winner = 0
    while not winner:
        k = keydown(1)
        if k == ord("a"):
            winner = 1
        elif k == ord("l"):
            winner = 2
    ms = time.ticks_diff(time.ticks_ms(), t0)
    print(f"Player {winner} takes it -- {ms} ms!")
    scores[winner] += 1

print(f"\nFinal score  P1: {scores[1]}   P2: {scores[2]}")
champ = 1 if scores[1] > scores[2] else 2
print(f"CHAMPION: player {champ}")
beep()
