"""Minimal ISO9660 reader (no external deps): list or extract the PS2 disc.

Usage:
  python tools/iso_extract.py list    <iso>
  python tools/iso_extract.py extract <iso> <dest_dir> [/PATH ...]   (no paths = whole disc)
"""
import os
import struct
import sys

SECTOR = 2048


def walk(f, lba, size, path, out):
    f.seek(lba * SECTOR)
    data = f.read(size)
    pos = 0
    while pos < len(data):
        ln = data[pos]
        if ln == 0:
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        rec = data[pos:pos + ln]
        pos += ln
        ext_lba, ext_size = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        name = rec[33:33 + rec[32]]
        if name in (b'\x00', b'\x01'):
            continue
        full = path + '/' + name.decode('ascii', 'replace').split(';')[0]
        if rec[25] & 2:
            walk(f, ext_lba, ext_size, full, out)
        else:
            out.append((full, ext_lba, ext_size))


def entries(f):
    f.seek(16 * SECTOR)
    pvd = f.read(SECTOR)
    if pvd[1:6] != b'CD001':
        sys.exit('not an ISO9660 image')
    root = pvd[156:156 + 34]
    out = []
    walk(f, struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], '', out)
    return out


def main():
    cmd, iso = sys.argv[1], sys.argv[2]
    with open(iso, 'rb') as f:
        files = entries(f)
        if cmd == 'list':
            for path, _, size in files:
                print('%-40s %12d' % (path, size))
            return
        dest = sys.argv[3]
        wanted = {p.upper() for p in sys.argv[4:]}
        for path, lba, size in files:
            if wanted and path.upper() not in wanted:
                continue
            target = os.path.join(dest, *path.strip('/').split('/'))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            f.seek(lba * SECTOR)
            with open(target, 'wb') as o:
                left = size
                while left:
                    chunk = f.read(min(left, 8 << 20))
                    o.write(chunk)
                    left -= len(chunk)
            print('%-40s %12d' % (path, size))


if __name__ == '__main__':
    main()
