"""Lista (e opcionalmente extrai) arquivos de um pacote "Dave" do Red Dead Revolver (FRONT.DAT, SWAT.DAT...).

Formato (FUN_002CAB88 no ELF, FUNCOES.md #76):
  cabecalho 'Dave', n entradas, tamanho do diretorio, tamanho da tabela de nomes; diretorio em 0x800
  (16 bytes por entrada: nome, dado, tamanho, tamanho comprimido); nomes logo depois do diretorio.
  Nome = caracteres de 6 bits (LE) da tabela " #$()-./?0-9_a-z~". Primeiro valor >= 0x38: reaproveita
  (v0 - 0x38) + (v1 & 31) * 8 caracteres do nome da entrada anterior; o resto segue a partir do 3o valor.
  Dado comprimido (tamanho comprimido != tamanho) = deflate cru.

Uso: python tools/dave_list.py <arquivo.DAT> [filtro] [--extract <pasta>]
Saida so em pastas ignoradas pelo git (out/), nunca no repositorio.
"""
import struct
import sys
import zlib

CHARS = "\0 #$()-./?0123456789_abcdefghijklmnopqrstuvwxyz~"


def six(data, off, count):
    v = int.from_bytes(data[off:off + (count * 6 + 7) // 8 + 1], 'little')
    return [(v >> (6 * i)) & 63 for i in range(count)]


def read_names(d, n, dir_size):
    base = 0x800 + dir_size
    names = []
    for i in range(n):
        name_off = struct.unpack_from('<I', d, 0x800 + 16 * i)[0]
        vals = six(d, base + name_off, 200)
        prefix, start = '', 0
        if vals[0] >= 0x38:
            keep = (vals[0] - 0x38) + (vals[1] & 31) * 8
            prefix, start = names[-1][:keep], 2
        s = []
        for x in vals[start:]:
            if x == 0:
                break
            s.append(CHARS[x] if x < len(CHARS) else '?')
        names.append(prefix + ''.join(s))
    return names


def main():
    args = sys.argv[1:]
    out = None
    if '--extract' in args:
        k = args.index('--extract')
        out = args[k + 1]
        del args[k:k + 2]
    path, filt = args[0], (args[1] if len(args) > 1 else '')
    d = open(path, 'rb').read()
    assert d[:4] in (b'Dave', b'DAVE'), d[:4]
    n, dir_size, _ = struct.unpack_from('<3I', d, 4)
    names = read_names(d, n, dir_size)
    for i, name in enumerate(names):
        if filt and filt not in name:
            continue
        _, data_off, size, csize = struct.unpack_from('<4I', d, 0x800 + 16 * i)
        print(f'{i:5d} {data_off:#010x} {size:9d} {csize:9d} {name}')
        if out:
            import os
            blob = d[data_off:data_off + csize]
            if csize != size:
                blob = zlib.decompress(blob, -15)
            dst = os.path.join(out, name.replace('/', os.sep))
            os.makedirs(os.path.dirname(dst) or out, exist_ok=True)
            open(dst, 'wb').write(blob)


if __name__ == '__main__':
    main()
