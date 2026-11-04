import sys
path = sys.argv[1]
needles = sys.argv[2].split('|')
raw = open(path, 'rb').read()
if raw[:2] in (b'\xff\xfe', b'\xfe\xff'):
    s = raw.decode('utf-16', 'replace').splitlines()
else:
    s = raw.decode('utf-8', 'replace').splitlines()
n = 0
for i, l in enumerate(s):
    if any(x in l for x in needles):
        print(i + 1, l[:160])
        n += 1
        if n > 200:
            break
