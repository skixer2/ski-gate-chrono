import sys
path = sys.argv[1]
needles = sys.argv[2].split('|')
s = open(path, encoding='utf-8', errors='replace').read().splitlines()
n = 0
for i, l in enumerate(s):
    if any(x in l for x in needles):
        print(i + 1, l[:160])
        n += 1
        if n > 200:
            break
