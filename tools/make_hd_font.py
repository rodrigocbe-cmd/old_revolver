"""Gera paginas de fonte em alta resolucao (.hd) para a troca de texturas do GS (FUNCOES.md #106).

Cada pagina .tex e casada com o upload gravado por PS2X_TEX_DUMP pelo histograma dos indices, que nao depende da ordem
dos pixels na memoria do GS. Amplia a cobertura da letra e reforca a borda: curvas mais lisas, sem detalhe novo. A saida
deriva do jogo: fica em pasta ignorada pelo git.

Uso: python tools/make_hd_font.py <pasta_dump> <pasta_saida> <fator> <pagina.tex> [pagina.tex ...]
"""
import glob
import os
import struct
import sys

import numpy as np
from PIL import Image


def load_tex(path):
    d = open(path, 'rb').read()
    w, h = struct.unpack_from('<HH', d, 0)
    palette = np.frombuffer(d, dtype=np.uint8, count=1024, offset=14).reshape(256, 4)
    pixels = np.frombuffer(d, dtype=np.uint8, count=w * h, offset=14 + 1024).reshape(h, w)
    return w, h, palette, pixels


def upscale_channel(values, size):
    image = Image.fromarray(values.astype(np.float32), mode='F')
    return np.asarray(image.resize(size, Image.BICUBIC), dtype=np.float32)


def main():
    dump_dir, out_dir, factor = sys.argv[1], sys.argv[2], int(sys.argv[3])
    dumps = {}
    for name in glob.glob(os.path.join(dump_dir, '*.bin')):
        data = np.fromfile(name, dtype=np.uint8)
        dumps.setdefault(len(data), []).append((os.path.basename(name).split('_')[0], np.bincount(data, minlength=256)))
    os.makedirs(out_dir, exist_ok=True)
    for path in sys.argv[4:]:
        w, h, palette, pixels = load_tex(path)
        histogram = np.bincount(pixels.ravel(), minlength=256)
        hashes = sorted({key for key, other in dumps.get(w * h, []) if (other == histogram).all()})
        if not hashes:
            print(os.path.basename(path), 'sem upload correspondente no dump')
            continue
        # Alfa do arquivo vai de 0 a 255; o jogo divide por 2 ao enviar a paleta (0 a 128). Paleta do arquivo em B, G, R, A.
        alpha = (palette[pixels, 3].astype(np.float32) + 1.0) // 2.0
        color = palette[pixels, :3].astype(np.float32)
        size = (w * factor, h * factor)
        # Cor multiplicada pelo alfa na ampliacao, para o fundo transparente nao escurecer a borda
        scaled_alpha = np.clip(upscale_channel(alpha, size), 0.0, 128.0)
        out = np.zeros((size[1], size[0], 4), dtype=np.uint8)
        for c in range(3):
            premultiplied = upscale_channel(color[:, :, c] * alpha, size)
            out[:, :, 2 - c] = np.clip(premultiplied / np.maximum(scaled_alpha, 1.0), 0.0, 255.0)
        edge = np.clip((scaled_alpha / 128.0 - 0.5) * 1.6 + 0.5, 0.0, 1.0)
        out[:, :, 3] = np.rint(edge * edge * (3.0 - 2.0 * edge) * 128.0)
        for key in hashes:
            with open(os.path.join(out_dir, key + '.hd'), 'wb') as f:
                f.write(struct.pack('<HH', size[0], size[1]))
                f.write(out.tobytes())
        print(os.path.basename(path), size, hashes)


main()
