"""Desmonta um trecho de um modulo IRX do IOP pelo endereco em que o runtime o carregou.

  python tools/irx_dis.py game/disc/SYSTEM/AGESND.IRX 0x3cb00 0x44180 0x442c0
  python tools/irx_dis.py game/disc/SYSTEM/AGESND.IRX 0x3cb00 --imports

O endereco de carga (base) aparece no log como "[IOP] loaded IRX id=N entry=... base=0x...". O primeiro
segmento carregavel do ELF (IRX = ELF tipo 0xFF80) e copiado para a base, entao endereco - base = offset no
segmento. Nao aplica relocacoes: alvos de jal e pares lui/addiu aparecem relativos ao modulo (sem a base);
os jal para stubs de importacao sao anotados com biblioteca:indice (ex.: thbase:20).
Precisa de capstone (pip install capstone).
"""
import struct
import sys

import capstone


def load_image(path):
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF':
        raise SystemExit('nao e ELF')
    phoff, = struct.unpack_from('<I', data, 0x1C)
    phentsize, phnum = struct.unpack_from('<HH', data, 0x2A)
    for i in range(phnum):
        ptype, off, vaddr, paddr, filesz, memsz = struct.unpack_from('<6I', data, phoff + i * phentsize)
        if ptype == 1:  # PT_LOAD
            return data[off:off + filesz]
    raise SystemExit('sem segmento PT_LOAD')


def import_stubs(image):
    """offset do stub -> 'biblioteca:indice' (tabelas de importacao com magic 0x41e00000)."""
    stubs = {}
    for off in range(0, len(image) - 20, 4):
        if struct.unpack_from('<I', image, off)[0] != 0x41E00000:
            continue
        name = image[off + 12:off + 20].split(b'\0')[0].decode('ascii', 'replace')
        p = off + 20
        while p + 8 <= len(image):
            a, b = struct.unpack_from('<II', image, p)
            if a != 0x03E00008 or (b >> 16) != 0x2400:
                break
            stubs[p] = '%s:%d' % (name, b & 0xFFFF)
            p += 8
    return stubs


def main():
    path, base = sys.argv[1], int(sys.argv[2], 0)
    image = load_image(path)
    stubs = import_stubs(image)
    if sys.argv[3] == '--imports':
        for off, name in sorted(stubs.items()):
            print('%08x (modulo 0x%x)  %s' % (base + off, off, name))
        return
    start, end = int(sys.argv[3], 0), int(sys.argv[4], 0)
    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)
    for ins in md.disasm(image[start - base:end - base], start):
        note = ''
        if ins.mnemonic in ('jal', 'j'):
            target = int(ins.op_str, 16)
            if target in stubs:
                note = '   ; ' + stubs[target]
        print('%08x  %-8s %s%s' % (ins.address, ins.mnemonic, ins.op_str, note))


if __name__ == '__main__':
    main()
