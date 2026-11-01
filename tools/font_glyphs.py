"""Recorta glifos de uma fonte do Red Dead Revolver (.fonttex + <nome>_NN.tex extraidos com dave_list.py).

.fonttex (FUNCOES.md #76): cabecalho de 0x2C bytes, u32 n glifos em 0x2C, n entradas de 10 bytes
(u16 caractere, u16 ?, u16 posicao = y*256 + x na pagina, u8 largura, u8 altura, i16 deslocamento y)
e no fim u32 n_paginas + n_paginas valores (nao usados aqui: a pagina muda quando a posicao volta a zero).
.tex de fonte: 128x128, cabecalho de 14 bytes, paleta RGBA de 256 cores e 16384 indices de 8 bits.

Uso: python tools/font_glyphs.py <pasta> <fonte> <saida.png> [hex ...]   (ex.: ba bc bd be 2020 2021)
"""
import struct
import sys

from PIL import Image


def load(folder, font):
    d = open(f'{folder}/{font}.fonttex', 'rb').read()
    n = struct.unpack_from('<I', d, 0x2C)[0]
    glyphs = [struct.unpack_from('<HHHBBh', d, 0x30 + 10 * i) for i in range(n)]
    q = 0x30 + 10 * n
    pages = struct.unpack_from('<I', d, q)[0]
    ends = struct.unpack_from(f'<{pages}I', d, q + 4)
    return glyphs, ends


def page_image(folder, font, k):
    t = open(f'{folder}/{font}_{k:02d}.tex', 'rb').read()
    w, h = struct.unpack_from('<HH', t, 0)
    pal = t[14:14 + 1024]
    idx = t[14 + 1024:14 + 1024 + w * h]
    img = Image.new('RGBA', (w, h))
    px = []
    for i in idx:
        r, g, b, a = pal[4 * i:4 * i + 4]
        px.append((r, g, b, min(255, a * 2)))   # alfa do PS2 vai ate 0x80
    img.putdata(px)
    return img


def main():
    folder, font, out = sys.argv[1:4]
    want = [int(x, 16) for x in sys.argv[4:]]
    glyphs, ends = load(folder, font)
    crops = []
    k, last = 0, 0
    for c, _, pos, w, h, _ in glyphs:
        if pos < last:   # a posicao volta ao inicio: proxima pagina (a tabela do fim nao bate com isto)
            k += 1
        last = pos
        if want and c not in want:
            continue
        x, y = pos % 256, pos // 256
        crops.append((c, page_image(folder, font, k).crop((x, y, x + w, y + h))))
    W = sum(im.width + 8 for _, im in crops) + 8
    H = max(im.height for _, im in crops) + 16
    sheet = Image.new('RGBA', (W, H), (40, 40, 40, 255))
    x = 8
    for c, im in crops:
        sheet.alpha_composite(im, (x, 8))
        print(f'{c:#06x} em x={x} ({im.width}x{im.height})')
        x += im.width + 8
    sheet.resize((W * 3, H * 3), Image.NEAREST).save(out)


if __name__ == '__main__':
    main()
