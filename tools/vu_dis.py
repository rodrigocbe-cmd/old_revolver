"""Disassembler de microcodigo do VU0/VU1 (pares de 64 bits: lower nos 32 bits baixos, upper nos altos).

Uso:
  python tools/vu_dis.py <prog.bin> [--base 0xADDR]     # listagem
  python tools/vu_dis.py --hist <prog.bin> [...]        # histograma de opcodes (para dimensionar um recompilador)

Os .bin vem de PS2X_VU1_DUMP_MPG (dados do jogo: ficam fora do repositorio).
Tabelas conferidas com ps2xRuntime/src/lib/vu/ps2_vu1_upper.cpp e ps2_vu1_lower.cpp.
"""
import collections
import struct
import sys

BC = 'xyzw'


def dest(w):
    d = (w >> 21) & 0xF
    return ''.join(c for i, c in enumerate('xyzw') if d & (8 >> i))


UPPER = {}
for i in range(4):
    UPPER[0x00 + i] = 'ADD' + BC[i]
    UPPER[0x04 + i] = 'SUB' + BC[i]
    UPPER[0x08 + i] = 'MADD' + BC[i]
    UPPER[0x0C + i] = 'MSUB' + BC[i]
    UPPER[0x10 + i] = 'MAX' + BC[i]
    UPPER[0x14 + i] = 'MINI' + BC[i]
    UPPER[0x18 + i] = 'MUL' + BC[i]
UPPER.update({0x1C: 'MULq', 0x1D: 'MAXi', 0x1E: 'MULi', 0x1F: 'MINIi', 0x20: 'ADDq', 0x21: 'MADDq',
              0x22: 'ADDi', 0x23: 'MADDi', 0x24: 'SUBq', 0x25: 'MSUBq', 0x26: 'SUBi', 0x27: 'MSUBi',
              0x28: 'ADD', 0x29: 'MADD', 0x2A: 'MUL', 0x2B: 'MAX', 0x2C: 'SUB', 0x2D: 'MSUB', 0x2E: 'OPMSUB',
              0x2F: 'MINI'})
UPPER_SPECIAL = {}
for i in range(4):
    UPPER_SPECIAL[0x00 + i] = 'ADDA' + BC[i]
    UPPER_SPECIAL[0x04 + i] = 'SUBA' + BC[i]
    UPPER_SPECIAL[0x08 + i] = 'MADDA' + BC[i]
    UPPER_SPECIAL[0x0C + i] = 'MSUBA' + BC[i]
    UPPER_SPECIAL[0x18 + i] = 'MULA' + BC[i]
UPPER_SPECIAL.update({0x10: 'ITOF0', 0x11: 'ITOF4', 0x12: 'ITOF12', 0x13: 'ITOF15', 0x14: 'FTOI0', 0x15: 'FTOI4',
                      0x16: 'FTOI12', 0x17: 'FTOI15', 0x1C: 'MULAq', 0x1D: 'ABS', 0x1E: 'MULAi', 0x1F: 'CLIP',
                      0x20: 'ADDAq', 0x21: 'MADDAq', 0x22: 'ADDAi', 0x23: 'MADDAi', 0x24: 'SUBAq', 0x25: 'MSUBAq',
                      0x26: 'SUBAi', 0x27: 'MSUBAi', 0x28: 'ADDA', 0x29: 'MADDA', 0x2A: 'MULA', 0x2C: 'SUBA',
                      0x2D: 'MSUBA', 0x2E: 'OPMULA', 0x2F: 'NOP', 0x30: 'NOP'})

LOWER = {0x00: 'LQ', 0x01: 'SQ', 0x04: 'ILW', 0x05: 'ISW', 0x08: 'IADDIU', 0x09: 'ISUBIU', 0x10: 'FCEQ',
         0x11: 'FCSET', 0x12: 'FCAND', 0x13: 'FCOR', 0x14: 'FSEQ', 0x15: 'FSSET', 0x16: 'FSAND', 0x17: 'FSOR',
         0x18: 'FMEQ', 0x1A: 'FMAND', 0x1B: 'FMOR', 0x1C: 'FCGET', 0x20: 'B', 0x21: 'BAL', 0x24: 'JR',
         0x25: 'JALR', 0x28: 'IBEQ', 0x29: 'IBNE', 0x2C: 'IBLTZ', 0x2D: 'IBGTZ', 0x2E: 'IBLEZ', 0x2F: 'IBGEZ'}
LOWER1 = {0x30: 'IADD', 0x31: 'ISUB', 0x32: 'IADDI', 0x34: 'IAND', 0x35: 'IOR'}
LOWER_SPECIAL = {0x30: 'MOVE', 0x31: 'MR32', 0x34: 'LQI', 0x35: 'SQI', 0x36: 'LQD', 0x37: 'SQD', 0x38: 'DIV',
                 0x39: 'SQRT', 0x3A: 'RSQRT', 0x3B: 'WAITQ', 0x3C: 'MTIR', 0x3D: 'MFIR', 0x3E: 'ILWR',
                 0x3F: 'ISWR', 0x40: 'RNEXT', 0x41: 'RGET', 0x42: 'RINIT', 0x43: 'RXOR', 0x64: 'MFP', 0x68: 'XTOP',
                 0x69: 'XITOP', 0x6C: 'XGKICK', 0x70: 'ESADD', 0x71: 'ERSADD', 0x72: 'ELENG', 0x73: 'ERLENG',
                 0x74: 'EATANxy', 0x75: 'EATANxz', 0x76: 'ESUM', 0x77: 'ERSQRT', 0x78: 'ESQRT', 0x79: 'ESIN',
                 0x7A: 'ERCPR', 0x7B: 'WAITP', 0x7C: 'EATAN', 0x7D: 'EEXP'}


def upper_name(w):
    op = w & 0x3F
    if op >= 0x3C:
        sp = (w & 3) | ((w >> 4) & 0x7C)
        return UPPER_SPECIAL.get(sp, 'U?%02x' % sp)
    return UPPER.get(op, 'U?%02x' % op)


def lower_name(w):
    hi = (w >> 25) & 0x7F
    if hi == 0x40:
        fn = w & 0x3F
        if fn >= 0x3C:
            sp = (w & 3) | ((w >> 4) & 0x7C)
            return LOWER_SPECIAL.get(sp, 'L?s%02x' % sp)
        return LOWER1.get(fn, 'L?1%02x' % fn)
    return LOWER.get(hi, 'L?%02x' % hi)


def flags(upper):
    return ''.join(c for bit, c in ((31, 'I'), (30, 'E'), (29, 'M'), (28, 'D'), (27, 'T')) if upper & (1 << bit))


def pairs(data):
    for off in range(0, len(data) - 7, 8):
        lo, up = struct.unpack_from('<II', data, off)
        yield off, lo, up


def listing(path, base):
    data = open(path, 'rb').read()
    for off, lo, up in pairs(data):
        f = flags(up)
        un = upper_name(up)
        u = '%s.%s vf%d,vf%d,vf%d' % (un, dest(up), (up >> 6) & 31, (up >> 11) & 31, (up >> 16) & 31)
        if f and 'I' in f:
            l = 'LOI 0x%08x (%g)' % (lo, struct.unpack('<f', struct.pack('<I', lo))[0])
        else:
            ln = lower_name(lo)
            l = '%s.%s ft%d fs%d fd%d imm11=%d' % (ln, dest(lo), (lo >> 16) & 31, (lo >> 11) & 31, (lo >> 6) & 31,
                                                   lo & 0x7FF)
        print('%04x  [%-5s] %-34s | %s' % ((base + off) // 8, f, u, l))


def histogram(paths):
    up, lo, fl = collections.Counter(), collections.Counter(), collections.Counter()
    total = 0
    for path in paths:
        for off, l, u in pairs(open(path, 'rb').read()):
            total += 1
            up[upper_name(u)] += 1
            f = flags(u)
            for c in f:
                fl[c] += 1
            if 'I' not in f:
                lo[lower_name(l)] += 1
    print('pares:', total)
    print('flags:', dict(fl))
    print('upper (%d tipos):' % len(up), ', '.join('%s=%d' % kv for kv in up.most_common()))
    print('lower (%d tipos):' % len(lo), ', '.join('%s=%d' % kv for kv in lo.most_common()))


if __name__ == '__main__':
    args = sys.argv[1:]
    if args and args[0] == '--hist':
        histogram(args[1:])
    else:
        base = int(args[args.index('--base') + 1], 16) if '--base' in args else 0
        listing(args[0], base)
