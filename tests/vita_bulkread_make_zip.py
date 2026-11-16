# Builds a zip with stored and deflated entries, varied extra fields and
# sizes, and writes the central-directory facts the engine would know.
import zipfile, random, struct, sys, os
random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
path = sys.argv[1]
names = []
with zipfile.ZipFile(path, 'w') as z:
    for i in range(400):
        n = 'sprites/S%04d.png' % i if i % 3 else 'sounds/DS%04d.ogg' % i
        size = random.choice([0, 1, 7, 300, 4000, 40000, 300000, 1500000]) if i % 17 else 2600000
        data = bytes(random.getrandbits(8) for _ in range(min(size, 2000))) * (size // 2000 + 1)
        data = data[:size]
        zi = zipfile.ZipInfo(n)
        zi.compress_type = zipfile.ZIP_DEFLATED if random.random() < 0.7 else zipfile.ZIP_STORED
        n_ = random.choice([0, 4, 28, 300, 900]); zi.extra = (struct.pack('<HH', 0xCAFE, n_) + os.urandom(n_)) if random.random() < 0.3 else b''
        z.writestr(zi, data)
        names.append(n)
with zipfile.ZipFile(path) as z, open(path + '.idx', 'w') as out:
    for zi in z.infolist():
        out.write('%d %d %d %d %d\n' % (zi.header_offset, zi.compress_size, zi.file_size, 1 if zi.compress_type == 8 else 0, len(zi.filename)))
    for zi in z.infolist():
        open(path + '.%s' % zi.filename.replace('/', '_'), 'wb').write(z.read(zi))
