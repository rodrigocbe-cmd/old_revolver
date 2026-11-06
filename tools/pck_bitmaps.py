"""Lista os bitmaps de um .pck de Flash do Red Dead Revolver e grava uma folha em PNG (cinza pelos indices).

Cada bitmap tem ponteiro de pixels, largura e altura (16 bits) e 1/largura e 1/altura em float. Os ponteiros somam
primeiro_dword + 0x80 ao deslocamento no arquivo. A saida deriva do jogo: grave so em pastas ignoradas (out/).

Uso: python tools/pck_bitmaps.py <arquivo.pck> <saida.png>
"""
import re
import struct
import sys
import zlib


def png(path, w, h, px):
    raw = b''.join(b'\0' + px[y * w:(y + 1) * w] for y in range(h))

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)

    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


d = open(sys.argv[1], 'rb').read()
base = struct.unpack_from('<I', d, 0)[0] + 0x80
items = []
for o in range(4, len(d) - 12, 4):
    w, h = struct.unpack_from('<HH', d, o)
    if w not in (8, 16, 32, 64, 128, 256, 512) or h not in (8, 16, 32, 64, 128, 256, 512):
        continue
    if struct.unpack_from('<ff', d, o + 4) != (1.0 / w, 1.0 / h):
        continue
    off = struct.unpack_from('<I', d, o - 4)[0] - base
    if 0 <= off < len(d):
        items.append((o, off, w, h))
print(len(items))
SHEET_W = 1024
rows = []
x = y = rowh = 0
canvas = bytearray(SHEET_W * 1024)
for o, off, w, h in items:
    print(hex(o), 'pix', hex(off), w, h)
    bpp4 = off + w * h > len(d)
    if x + w > SHEET_W:
        x, y, rowh = 0, y + rowh + 4, 0
    if y + h > 1024:
        break
    for j in range(h):
        canvas[(y + j) * SHEET_W + x:(y + j) * SHEET_W + x + w] = d[off + j * w:off + j * w + w].ljust(w, b'\0')
    x += w + 4
    rowh = max(rowh, h)
png(sys.argv[2], SHEET_W, min(1024, y + rowh + 1), bytes(canvas))
