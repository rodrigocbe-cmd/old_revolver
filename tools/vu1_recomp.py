"""Recompilador de microprogramas do VU1 para C++ (FUNCOES.md #61).

Le os microprogramas gravados por PS2X_VU1_DUMP_MPG (mpg_<destino>_<bytes>_<hash>.bin) e gera um .cpp por
programa para o runner do PS2Recomp. Cada par de instrucoes vira uma chamada a
Vu1Compiled::pair<UPPER, LOWER>() (ps2xRuntime/src/lib/vu/ps2_vu1_compiled.h), que reproduz exatamente o laco
do interpretador; desvios voltam para um switch de despacho (o destino so e conhecido depois do delay slot),
e destinos fora do programa devolvem o controle ao despachante do runtime.

O codigo gerado deriva do jogo: fica fora do repositorio (padrao out/vu1gen/).

Uso: python tools/vu1_recomp.py <pasta_com_mpg> <pasta_saida>
"""
import os
import re
import struct
import sys

NAME = re.compile(r'mpg_([0-9a-f]{4})_(\d+)_([0-9a-f]{16})\.bin$')


def fnv(data, dest):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h ^ (dest << 40)


def generate(path, out_dir):
    m = NAME.search(os.path.basename(path))
    if not m:
        return None
    dest, size, hash_hex = int(m.group(1), 16), int(m.group(2)), m.group(3)
    data = open(path, 'rb').read()
    if len(data) != size or fnv(data, dest) != int(hash_hex, 16):
        print('aviso: hash/tamanho nao confere em', path)
        return None
    fn = 'vu1prog_%04x_%s' % (dest, hash_hex)
    pcs = [dest + off for off in range(0, size - 7, 8)]
    lines = [
        '// Gerado por tools/vu1_recomp.py -- nao editar (deriva do microcodigo do jogo).',
        '#include "../lib/vu/ps2_vu1_compiled.h"',
        '',
        'namespace',
        '{',
        'int %s(VU1Interpreter &vu, Vu1Compiled::Env &e, uint32_t pc)' % fn,
        '{',
        '    uint32_t np = pc;',
        '    int st = -1;',
        '    goto dispatch;',
        'dispatch:',
        '    switch (np)',
        '    {',
    ]
    for pc in pcs:
        lines.append('    case 0x%04xu: goto L_%04x;' % (pc, pc))
    lines += [
        '    default:',
        '        vu.state().pc = np;',
        '        return Vu1Compiled::kExited;',
        '    }',
    ]
    for i, pc in enumerate(pcs):
        lo, up = struct.unpack_from('<II', data, pc - dest)
        lines.append('L_%04x:' % pc)
        lines.append('    st = Vu1Compiled::pair<0x%08xu, 0x%08xu>(vu, e, 0x%04xu, np);' % (up, lo, pc))
        lines.append('    if (st >= 0) return st;')
        if i + 1 < len(pcs):
            lines.append('    if (np != 0x%04xu) goto dispatch;' % (pc + 8))
        else:
            lines.append('    goto dispatch;')
    lines += [
        '}',
        '',
        'struct Register_%s' % fn,
        '{',
        '    Register_%s() { Vu1Compiled::registerProgram(0x%04xu, %uu, 0x%sull, &%s, "%s"); }' % (
            fn, dest, size, hash_hex, fn, fn),
        '} s_register_%s;' % fn,
        '}',
        '',
    ]
    out = os.path.join(out_dir, fn + '.cpp')
    text = '\n'.join(lines)
    if not os.path.exists(out) or open(out, encoding='utf-8').read() != text:
        open(out, 'w', encoding='utf-8', newline='\n').write(text)
    return fn, len(pcs)


def main():
    src, out_dir = sys.argv[1:3]
    os.makedirs(out_dir, exist_ok=True)
    total = 0
    made = []
    for name in sorted(os.listdir(src)):
        r = generate(os.path.join(src, name), out_dir)
        if r:
            made.append(r[0])
            total += r[1]
    keep = {f + '.cpp' for f in made}
    for name in os.listdir(out_dir):
        if name.startswith('vu1prog_') and name.endswith('.cpp') and name not in keep:
            os.remove(os.path.join(out_dir, name))
    print('%d programas, %d pares -> %s' % (len(made), total, out_dir))


if __name__ == '__main__':
    main()
