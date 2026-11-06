"""Extrai os textos dos .strtbl para um arquivo-base de traducao (docs/TRADUCAO.md).

Formato do .strtbl (FUN_0017BC78, confirmado pela leitura de todos os arquivos):
  u32 idiomas (10), u32 inicio de cada idioma (so o primeiro existe; os outros apontam para o fim do arquivo),
  u32 512, u32 n, n vezes [u32 tamanho, chave, NUL]
  em cada idioma: u32 n, n vezes [u32 hash, u16 opcoes, u32 tamanho, fonte, u32 caracteres com o NUL, UTF-16,
  float escala X, float escala Y, u16].

Saida: uma linha por texto, "chave<TAB>texto", UTF-8. Escapes: \\n, \\t, \\\\ e \\uXXXX. Os glifos de botao (0x80 a 0xBF) ficam
como \\uXXXX para sobreviverem a editores de texto. A saida deriva do jogo: fica em pasta ignorada pelo git.

Uso: python tools/text_extract.py <pasta_do_disco> <saida.txt> [--all]
Sem --all, ignora textos do Xbox e copias de reserva (bak, backup, xb_, _xbx).
"""
import glob
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dave_list import read_names  # noqa: E402


def key_hash(key):
    # FUN_0017C1C8: PJW; diferencia maiusculas de minusculas
    h = 0
    for b in key.encode('latin1'):
        h = (h << 4) + (b - 256 if b >= 128 else b) & 0xFFFFFFFF
        top = h & 0xF0000000
        if top:
            h ^= top >> 24
            h ^= top
    return h


def parse(d):
    count = struct.unpack_from('<I', d, 0)[0]
    starts = struct.unpack_from('<%dI' % count, d, 4)
    o = 4 + 4 * count + 4
    n = struct.unpack_from('<I', d, o)[0]
    o += 4
    keys = {}
    for _ in range(n):
        size = struct.unpack_from('<I', d, o)[0]
        key = d[o + 4:o + 4 + size].decode('latin1')
        keys[key_hash(key)] = key
        o += 4 + size + 1
    if starts[0] != o:
        raise ValueError('inicio do idioma: %x != %x' % (starts[0], o))
    n = struct.unpack_from('<I', d, o)[0]
    o += 4
    out = []
    for _ in range(n):
        h, _flags, size = struct.unpack_from('<IHI', d, o)
        o += 10
        font = d[o:o + size].decode('latin1')
        o += size
        chars = struct.unpack_from('<I', d, o)[0]
        o += 4
        text = d[o:o + (chars - 1) * 2].decode('utf-16-le')
        o += chars * 2 + 10
        out.append((keys.get(h, '?%08x' % h), font, text))
    return out


def escape(text):
    out = []
    for ch in text:
        c = ord(ch)
        if ch == '\\':
            out.append('\\\\')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\t':
            out.append('\\t')
        elif c < 0x20 or 0x7F <= c < 0xC0:
            out.append('\\u%04X' % c)
        else:
            out.append(ch)
    return ''.join(out)


def dave_files(path):
    d = open(path, 'rb').read()
    if d[:4] not in (b'Dave', b'DAVE'):
        return
    n, dir_size = struct.unpack_from('<II', d, 4)
    for i, name in enumerate(read_names(d, n, dir_size)):
        if not name.endswith('.strtbl'):
            continue
        _, data, size, packed = struct.unpack_from('<4I', d, 0x800 + 16 * i)
        raw = d[data:data + packed]
        yield name, raw if packed == size else zlib.decompress(raw, -15)


def main():
    disc, out_path = sys.argv[1], sys.argv[2]
    everything = '--all' in sys.argv
    skip = ('bak', 'backup', 'xb_', '_xbx')
    seen = {}
    sources = glob.glob(os.path.join(disc, '*.DAT')) + glob.glob(os.path.join(disc, 'LVLS_*', '*'))
    for path in sorted(sources):
        if not os.path.isfile(path):
            continue
        for name, data in dave_files(path):
            base = os.path.basename(name)
            if not everything and any(s in base for s in skip):
                continue
            try:
                entries = parse(data)
            except (ValueError, struct.error, UnicodeDecodeError) as e:
                print('falhou:', name, e)
                continue
            for key, font, text in entries:
                seen.setdefault((key, text), (base, font))
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# chave<TAB>texto (fonte, arquivo de origem)\n')
        for (key, text), (base, font) in sorted(seen.items(), key=lambda kv: (kv[1][0], kv[0][0])):
            f.write('# %s %s\n%s\t%s\n' % (base, font, key, escape(text)))
    fonts = {}
    for base, font in seen.values():
        fonts[font] = fonts.get(font, 0) + 1
    words = sum(len(text.split()) for _, text in seen)
    print(len(seen), 'textos,', words, 'palavras; fontes:', fonts)


if __name__ == '__main__':
    main()
