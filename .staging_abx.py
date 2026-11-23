import os, re

R = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
ABX = os.path.join(R, "reference", "abx.txt")

txt = open(ABX, encoding="utf-8", errors="ignore").read()
print("abx.txt chars:", len(txt))

print("\n=== flash-related lines in Nicla schematic text ===")
hits = 0
for ln in txt.splitlines():
    s = ln.strip()
    if not s:
        continue
    if re.search(r"MX25|W25Q|S25FL|IS25|GD25|AT25|QSPI|FLASH|Flash|FLASH1|U\d+.*25R", s):
        print("  ", s[:160])
        hits += 1
        if hits > 60:
            break

print("\n=== designators near 'flash' ===")
for m in re.finditer(r"(MX25R\d+\w*)", txt):
    print("  part:", m.group(1))

print("\n=== search for package hints (SOP/WSON/USON/SOIC) ===")
for ln in txt.splitlines():
    s = ln.strip()
    if re.search(r"WSON|USON|SOIC|SOP-8|SO8|208", s) and len(s) < 200:
        print("  ", s[:150])
