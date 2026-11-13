import sys
path, ver = sys.argv[1], sys.argv[2]
data = bytearray()
with open(path, 'r', errors='ignore') as f:
    for line in f:
        line = line.strip()
        if not line.startswith(':'):
            continue
        ln = int(line[1:3], 16)
        typ = int(line[7:9], 16)
        if typ == 0 and ln > 0:
            data += bytes.fromhex(line[9:9+ln*2])
ok = ver.encode('ascii') in bytes(data)
print('GATE_VER_OK' if ok else 'GATE_VER_FAIL want=' + ver)
sys.exit(0 if ok else 3)
