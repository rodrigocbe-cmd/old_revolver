"""Desmonta os blocos ActionScript (AVM1) de um .pck de Flash do Red Dead Revolver.

Procura cada ConstantPool (0x88) e decodifica ate o fim do bloco. A saida vem do pacote do jogo: use so para analise e
nao grave no repositorio.

Uso: python tools/avm1_dump.py <arquivo.pck> [filtro1 filtro2 ...]   (so blocos que citam algum filtro)
"""
import re
import struct
import sys

NAMES = {
    0x00: 'end', 0x04: 'nextFrame', 0x05: 'prevFrame', 0x06: 'play', 0x07: 'stop', 0x0A: 'add', 0x0B: 'sub', 0x0C: 'mul',
    0x0D: 'div', 0x0E: 'eq', 0x0F: 'lt', 0x10: 'and', 0x11: 'or', 0x12: 'not', 0x17: 'pop', 0x18: 'toInt',
    0x1C: 'getVar', 0x1D: 'setVar', 0x22: 'getProp', 0x23: 'setProp', 0x26: 'trace', 0x34: 'getTime', 0x3C: 'defLocal',
    0x3D: 'callFunc', 0x3E: 'return', 0x3F: 'mod', 0x40: 'new', 0x41: 'defLocal2', 0x42: 'initArray', 0x47: 'add2',
    0x48: 'less2', 0x49: 'eq2', 0x4C: 'dup', 0x4E: 'getMember', 0x4F: 'setMember', 0x50: 'inc', 0x51: 'dec',
    0x52: 'callMethod', 0x53: 'newMethod', 0x54: 'instanceOf', 0x60: 'bitAnd', 0x67: 'greater',
}


def decode(d, start):
    o = start
    pool = []
    out = []
    while o < len(d):
        op = d[o]
        if op < 0x80:
            o += 1
            if op == 0:
                break
            if op not in NAMES:
                return None
            out.append(NAMES[op])
            continue
        size = struct.unpack_from('<H', d, o + 1)[0]
        body = d[o + 3:o + 3 + size]
        pos = o
        o += 3 + size
        if op == 0x88:
            n = struct.unpack_from('<H', body, 0)[0]
            pool = [s.decode('latin1') for s in body[2:].split(b'\0')[:n]]
        elif op == 0x96:
            i = 0
            vals = []
            while i < size:
                t = body[i]
                i += 1
                if t == 0:
                    e = body.index(b'\0', i)
                    vals.append(repr(body[i:e].decode('latin1')))
                    i = e + 1
                elif t == 1:
                    vals.append('%g' % struct.unpack_from('<f', body, i)[0])
                    i += 4
                elif t == 2:
                    vals.append('null')
                elif t == 3:
                    vals.append('undefined')
                elif t == 4:
                    vals.append('r%d' % body[i])
                    i += 1
                elif t == 5:
                    vals.append('true' if body[i] else 'false')
                    i += 1
                elif t == 6:
                    v = body[i + 4:i + 8] + body[i:i + 4]
                    vals.append('%g' % struct.unpack('<d', v)[0])
                    i += 8
                elif t == 7:
                    vals.append(str(struct.unpack_from('<i', body, i)[0]))
                    i += 4
                elif t == 8:
                    idx = body[i]
                    vals.append(pool[idx] if idx < len(pool) else '?%d' % idx)
                    i += 1
                elif t == 9:
                    idx = struct.unpack_from('<H', body, i)[0]
                    vals.append(pool[idx] if idx < len(pool) else '?%d' % idx)
                    i += 2
                else:
                    return None
            out.append('push ' + ', '.join(vals))
        elif op == 0x99:
            out.append('jump %d' % struct.unpack('<h', body)[0])
        elif op == 0x9D:
            out.append('if %d' % struct.unpack('<h', body)[0])
        elif op == 0x81:
            out.append('gotoFrame %d' % struct.unpack('<H', body)[0])
        elif op == 0x8C:
            out.append('gotoLabel ' + body.split(b'\0')[0].decode('latin1'))
        elif op == 0x9F:
            out.append('gotoFrame2 flags=%d' % body[0])
        elif op == 0x9B:
            out.append('defFunc ' + body.split(b'\0')[0].decode('latin1'))
        else:
            return None
        if pos - start > 200000:
            return None
    return out, o


d = open(sys.argv[1], 'rb').read()
want = sys.argv[2:]
last = 0
for m in re.finditer(b'\x88', d):
    if m.start() < last:
        continue
    try:
        r = decode(d, m.start())
    except (struct.error, ValueError, IndexError, UnicodeDecodeError):
        r = None
    if not r or len(r[0]) < 2:
        continue
    text = ' ; '.join(r[0])
    last = r[1]
    if not want or any(w in text for w in want):
        print('@%x: %s\n' % (m.start(), text))
