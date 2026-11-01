"""Recompilador de microprogramas do VU1 para C++ (FUNCOES.md #61, #67).

Le os microprogramas gravados por PS2X_VU1_DUMP_MPG (mpg_<destino>_<bytes>_<hash>.bin) e gera um .cpp por
programa para o runner do PS2Recomp.

Modo estatico (padrao, #67): C++ direto por par, com o tempo resolvido aqui (Vu1Compiled::S em
ps2xRuntime/src/lib/vu/ps2_vu1_static.h): esperas so onde podem ocorrer (VI logo apos ILW/ILWR, FDIV, EFU,
WAITQ/WAITP, XGKICK), desvios com destino fixo viram goto, delay slots e E-bit resolvidos na geracao, regra do
VI antigo nos desvios condicionais. Programas com algo nao suportado caem no modo por par (#61): cada par chama
Vu1Compiled::pair<UPPER, LOWER>(), que reproduz o laco do interpretador.

O codigo gerado deriva do jogo: fica fora do repositorio (padrao out/vu1gen/).

Uso: python tools/vu1_recomp.py <pasta_com_mpg> <pasta_saida> [--pair]
"""
import os
import re
import struct
import sys

NAME = re.compile(r'mpg_([0-9a-f]{4})_(\d+)_([0-9a-f]{16})\.bin$')
CODE_MASK = 0x3FFF
# Eliminacao de flags mortas (#71) so com --dead-flags: com ela o jogo travou 2 de 2 vezes ao entrar no gameplay
# (thread principal do EE com PC lixo), sem divergencia nas 12 mil execucoes verificadas. Investigar antes de ligar.
NO_DEAD_FLAGS = '--dead-flags' not in sys.argv


class Unsupported(Exception):
    pass


def fnv(data, dest):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h ^ (dest << 40)


def imm11(l):
    v = l & 0x7FF
    return v - 0x800 if v & 0x400 else v


# ---- decodificacao (espelha VU1Interpreter::decodeLowerUsage / decodeUpperUsage) ----------------------------

def upper_writes_flags(u):
    """O upper grava MAC/status (FMAC com dest != 0)? MAX/MINI/ITOF/FTOI/ABS/CLIP/NOP nao."""
    if ((u >> 21) & 0xF) == 0:
        return False
    op = u & 0x3F
    if op < 0x3C:
        return op <= 0x2F and not (0x10 <= op <= 0x17 or op in (0x1D, 0x1F, 0x2B, 0x2F))
    sp = (u & 3) | ((u >> 4) & 0x7C)
    if 0x10 <= sp <= 0x17 or sp in (0x1D, 0x1F, 0x2B, 0x2F, 0x30):
        return False
    return sp <= 0x2E


def lower_touches_flags(l):
    """FSEQ/FSSET/FSAND/FSOR/FMEQ/FMAND/FMOR leem (ou FSSET reescreve) MAC/status."""
    if l in (0, 0x8000033C):
        return False
    return ((l >> 25) & 0x7F) in (0x14, 0x15, 0x16, 0x17, 0x18, 0x1A, 0x1B)


def upper_write(u):
    """Registrador VF escrito pelo upper (0 = nenhum/ACC). Levanta Unsupported para ops desconhecidas."""
    op = u & 0x3F
    ft, fd = (u >> 16) & 0x1F, (u >> 6) & 0x1F
    if op < 0x3C:
        if op > 0x2F:
            raise Unsupported('upper op 0x%02x' % op)
        return fd
    sp = (u & 3) | ((u >> 4) & 0x7C)
    if sp in (0x2F, 0x30, 0x1F):
        return 0
    if 0x10 <= sp <= 0x17 or sp == 0x1D:
        return ft
    if sp <= 0x2E and sp != 0x2B:
        return 0  # ACC
    raise Unsupported('upper special 0x%02x' % sp)


class Lower:
    def __init__(self, l):
        self.l = l
        self.vi_read = set()
        self.vi_write = {}       # reg -> latencia
        self.delays_branch = False
        self.vf_write = 0
        self.branch = None       # 'b', 'bal', 'jr', 'jalr', 'cond'
        self.cond = None         # (op, is, it) para desvios condicionais
        self.xgkick = False
        self.wait = False        # FDIV/EFU/WAITQ/WAITP: S::lowerWait
        self.decode()

    def decode(self):
        l = self.l
        if l in (0, 0x8000033C):
            return
        op = (l >> 25) & 0x7F
        vfT, vfS = (l >> 16) & 0x1F, (l >> 11) & 0x1F
        viT, viS, viD = (l >> 16) & 0xF, (l >> 11) & 0xF, (l >> 6) & 0xF

        def rd(r):
            if r:
                self.vi_read.add(r)

        def wr(r, lat):
            if r:
                self.vi_write[r] = lat

        if op == 0x00:
            rd(viS); self.vf_write = vfT
        elif op == 0x01:
            rd(viT)
        elif op == 0x04:
            rd(viS); wr(viT, 4)
        elif op == 0x05:
            rd(viS); rd(viT)
        elif op in (0x08, 0x09):
            rd(viS); wr(viT, 1); self.delays_branch = True
        elif op in (0x10, 0x12, 0x13):
            wr(1, 1)
        elif op in (0x11, 0x15):
            pass
        elif op in (0x14, 0x16, 0x17):
            wr(viT, 1)
        elif op in (0x18, 0x1A, 0x1B):
            rd(viS); wr(viT, 1)
        elif op == 0x1C:
            wr(viT, 1)
        elif op == 0x20:
            self.branch = 'b'
        elif op == 0x21:
            self.branch = 'bal'; wr(viT, 1)
        elif op == 0x24:
            self.branch = 'jr'; rd(viS)
        elif op == 0x25:
            self.branch = 'jalr'; rd(viS); wr(viT, 1)
        elif op in (0x28, 0x29):
            self.branch = 'cond'; self.cond = (op, viS, viT); rd(viS); rd(viT)
        elif op in (0x2C, 0x2D, 0x2E, 0x2F):
            self.branch = 'cond'; self.cond = (op, viS, 0); rd(viS)
        elif op == 0x40:
            f = l & 0x3F
            if f in (0x30, 0x31, 0x34, 0x35):
                rd(viS); rd(viT); wr(viD, 1); self.delays_branch = True
            elif f == 0x32:
                rd(viS); wr(viT, 1); self.delays_branch = True
            elif f < 0x3C:
                raise Unsupported('lower 0x40 funct 0x%02x' % f)
            else:
                sp = (l & 3) | ((l >> 4) & 0x7C)
                if sp in (0x30, 0x31):
                    self.vf_write = vfT
                elif sp in (0x34, 0x36):
                    rd(viS); wr(viS, 1); self.delays_branch = True; self.vf_write = vfT
                elif sp in (0x35, 0x37):
                    rd(viT); wr(viT, 1); self.delays_branch = True
                elif sp in (0x38, 0x39, 0x3A, 0x3B):
                    self.wait = True
                elif sp == 0x3C:
                    wr(viT, 1); self.delays_branch = True
                elif sp == 0x3D:
                    rd(viS); self.vf_write = vfT
                elif sp == 0x3E:
                    rd(viS); wr(viT, 4)
                elif sp == 0x3F:
                    rd(viS); rd(viT)
                elif sp in (0x40, 0x41):
                    self.vf_write = vfT
                elif sp in (0x42, 0x43):
                    pass
                elif sp == 0x64:
                    self.vf_write = vfT
                elif sp in (0x68, 0x69):
                    wr(viT, 1)
                elif sp == 0x6C:
                    rd(viS); self.xgkick = True
                elif sp in (0x70, 0x71, 0x72, 0x73, 0x76, 0x77, 0x78, 0x7A, 0x7B):
                    self.wait = True
                else:
                    raise Unsupported('lower special 0x%02x' % sp)
        else:
            raise Unsupported('lower op 0x%02x' % op)


# ---- modo estatico ----------------------------------------------------------------------------------------

def gen_static(fn, dest, data):
    n = len(data) // 8
    pcs = [dest + 8 * i for i in range(n)]
    lo = [struct.unpack_from('<I', data, 8 * i)[0] for i in range(n)]
    up = [struct.unpack_from('<I', data, 8 * i + 4)[0] for i in range(n)]
    ibit = [bool(u & 0x80000000) for u in up]
    ebit = [bool(u & 0x40000000) for u in up]
    dbit = [bool(u & 0x10000000) for u in up]
    tbit = [bool(u & 0x08000000) for u in up]
    lows = [None if ibit[i] else Lower(lo[i]) for i in range(n)]
    for u in up:
        upper_write(u)

    def in_prog(pc):
        return dest <= pc < dest + 8 * n

    # desvios: delay slot dentro do programa e sem outro desvio
    delay_of = {}  # indice do delay slot -> indice do desvio
    for i, lw in enumerate(lows):
        if lw and lw.branch:
            if i + 1 >= n:
                raise Unsupported('desvio sem delay slot no programa')
            if lows[i + 1] and lows[i + 1].branch:
                raise Unsupported('desvio no delay slot')
            if ebit[i]:
                raise Unsupported('E-bit em desvio')
            delay_of[i + 1] = i
    end_after = {}  # par seguinte ao E-bit -> par com E-bit
    for i in range(n):
        if ebit[i]:
            if i in delay_of:
                raise Unsupported('E-bit em delay slot')
            if i + 1 >= n:
                raise Unsupported('E-bit no ultimo par')
            end_after[i + 1] = i

    for i in range(n):
        if (dbit[i] or tbit[i]) and (ebit[i] or i in end_after):
            raise Unsupported('D/T junto de E-bit')

    def halt_cond(i):
        parts = []
        if dbit[i]:
            parts.append('s.dBitEnabled')
        if tbit[i]:
            parts.append('s.tBitEnabled')
        return ' || '.join(parts)

    def halt_flags(i):
        return 's.stoppedByD = %s; s.stoppedByT = %s;' % ('s.dBitEnabled' if dbit[i] else 'false',
                                                         's.tBitEnabled' if tbit[i] else 'false')

    # Flags mortas (FUNCOES.md #71): a gravacao de MAC/status do par i e invisivel se, por TODOS os caminhos
    # a partir de i, outra gravacao (par w) acontece pelo menos 4 pares antes de qualquer leitura de flags
    # (cada par custa >= 1 ciclo, entao a leitura 4+ pares depois de w ja ve w ou algo mais novo). E viva se algum
    # caminho chega, antes disso, a: leitura/FSSET, saida do programa, JR/JALR (destino desconhecido), E-bit, D/T.
    # Os bits sticky das gravacoes mortas seguem para a proxima entrada (carrySticky), entao o status final e igual.
    def succ(j):
        """Pares seguintes de j; None = saida/fim (flags ficam visiveis fora)."""
        if ebit[j] or dbit[j] or tbit[j] or j + 1 >= n and j not in delay_of:
            return None
        if j in delay_of:
            b = delay_of[j]
            kind = lows[b].branch
            if kind in ('jr', 'jalr'):
                return None
            tgt = (pcs[b] + 8 + imm11(lo[b]) * 8) & CODE_MASK
            out = []
            if in_prog(tgt):
                out.append((tgt - dest) // 8)
            else:
                return None
            if kind == 'cond':
                if j + 1 >= n:
                    return None
                out.append(j + 1)
            return out
        return [j + 1]

    def flag_live(i):
        if j_end_after_bad(i):
            return True
        stack = []
        first = succ(i)
        if first is None:
            return True
        for x in first:
            stack.append((x, -1))
        seen = set()
        while stack:
            j, since = stack.pop()
            if (j, since) in seen:
                continue
            seen.add((j, since))
            if j in end_after or ebit[j]:
                return True
            lw = lows[j]
            if lw is not None and lower_touches_flags(lo[j]) and (since < 0 or since < 4):
                return True
            nsince = since
            if since >= 0:
                nsince = since + 1
            if upper_writes_flags(up[j]) and since < 0:
                nsince = 1  # a leitura no proprio par w acontece antes de w ficar visivel; w + k ve w a partir de k = 4
            if nsince >= 4:
                continue
            nxt = succ(j)
            if nxt is None:
                if nsince < 0 or nsince < 4:
                    return True
                continue
            for x in nxt:
                stack.append((x, nsince))
        return False

    def j_end_after_bad(i):
        return i in delay_of or ebit[i] or dbit[i] or tbit[i] or i in end_after

    dead_flags = set() if NO_DEAD_FLAGS else {i for i in range(n) if upper_writes_flags(up[i]) and not flag_live(i)}

    ilw_regs = set()
    for lw in lows:
        if lw:
            ilw_regs |= {r for r, lat in lw.vi_write.items() if lat > 1}
    cond_regs = set()
    for lw in lows:
        if lw and lw.branch == 'cond':
            cond_regs |= {r for r in lw.cond[1:] if r}

    out = []
    w = out.append
    w('// Gerado por tools/vu1_recomp.py (modo estatico) -- nao editar (deriva do microcodigo do jogo).')
    w('#include "../lib/vu/ps2_vu1_static.h"')
    w('')
    w('namespace')
    w('{')
    w('using S = Vu1Compiled::S;')
    w('int %s(VU1Interpreter &vu, Vu1Compiled::Env &e, uint32_t pc)' % fn)
    w('{')
    w('    (void)vu;')
    w('    S::Ctx &c = *static_cast<S::Ctx *>(e.staticCtx); // criado por tryRun, compartilhado entre programas')
    w('    VU1State &s = c.s;')
    w('    (void)s;')
    for i in sorted(delay_of.values()):
        w('    bool bt_%d = false;' % i)
        if lows[i].branch in ('jr', 'jalr'):
            w('    uint32_t jt_%d = 0u;' % i)
    for i in sorted(end_after.values()):
        w('    bool end_%d = false;' % i)
    for i in sorted(delay_of.values()):
        if dbit[i] or tbit[i]:
            w('    bool halt_%d = false;' % i)
    w('    uint32_t np = pc;')
    w('dispatch:')
    w('    switch (np)')
    w('    {')
    for i, pc in enumerate(pcs):
        w('    case 0x%04xu: goto L_%04x;' % (pc, pc))
    w('    default: return S::exitTo(c, np);')
    w('    }')

    for i, pc in enumerate(pcs):
        lw = lows[i]
        u = up[i]
        uw = upper_write(u)
        w('L_%04x:' % pc)
        w('{')
        if lw and lw.branch == 'cond':
            w('    const uint64_t c0 = c.cyc;')
        if lw:
            for r in sorted(lw.vi_read & ilw_regs):
                w('    c.stallVi(%du);' % r)
            if lw.wait:
                w('    S::lowerWait<0x%08xu>(c);' % lo[i])
            if lw.xgkick:
                w('    S::xgkickWait(c);')
        w('    S::upper<0x%08xu%s>(c);' % (u, ', false' if i in dead_flags else ''))
        shadow = lw is not None and uw != 0 and lw.vf_write == uw
        if shadow:
            w('    float sh[4];')
            w('    std::memcpy(sh, s.vf[%d], 16);' % uw)
        backup = []
        if lw and lw.delays_branch:
            backup = [r for r in lw.vi_write if r in cond_regs]
            for r in backup:
                w('    const int32_t ob%d = s.vi[%d];' % (r, r))
        # lower
        if lw is None:
            pass
        elif lw.branch is None:
            if lo[i] not in (0, 0x8000033C):
                w('    S::lower<0x%08xu>(c);' % lo[i])
        else:
            l = lo[i]
            viT, viS = (l >> 16) & 0xF, (l >> 11) & 0xF
            target = (pc + 8 + imm11(l) * 8) & CODE_MASK
            if lw.branch in ('b', 'bal'):
                if lw.branch == 'bal' and viT:
                    w('    s.vi[%d] = %d;' % (viT, (pc + 16) // 8))
                w('    bt_%d = true;' % i)
            elif lw.branch in ('jr', 'jalr'):
                w('    jt_%d = (static_cast<uint32_t>(static_cast<uint16_t>(%s)) * 8u) & 0x3FFFu;' % (
                    i, 's.vi[%d]' % viS if viS else '0'))
                if lw.branch == 'jalr' and viT:
                    w('    s.vi[%d] = %d;' % (viT, (pc + 16) // 8))
                w('    bt_%d = true;' % i)
            else:
                op, ra, rb = lw.cond

                def bv(r):
                    if r == 0:
                        return '0'
                    if r in cond_regs:
                        return '((c.bkReg == %du && c.bkCyc + 1u == c0) ? c.bkVal : s.vi[%d])' % (r, r)
                    return 's.vi[%d]' % r
                a = 'static_cast<int16_t>(%s)' % bv(ra)
                b = 'static_cast<int16_t>(%s)' % bv(rb)
                expr = {0x28: '%s == %s' % (a, b), 0x29: '%s != %s' % (a, b), 0x2C: '%s < 0' % a,
                        0x2D: '%s > 0' % a, 0x2E: '%s <= 0' % a, 0x2F: '%s >= 0' % a}[op]
                w('    bt_%d = %s;' % (i, expr))
        if shadow:
            w('    std::memcpy(s.vf[%d], sh, 16);' % uw)
        w('    S::upperStore<0x%08xu>(c);' % u)
        if ibit[i]:
            w('    { float imm; const uint32_t b = 0x%08xu; std::memcpy(&imm, &b, 4); s.i = vuNormalizeOperandFast(imm); }' % lo[i])
        for r in backup:
            w('    c.bkReg = %du; c.bkVal = ob%d; c.bkCyc = c.cyc;' % (r, r))
        if lw:
            for r, lat in sorted(lw.vi_write.items()):
                if r in ilw_regs and lat == 1:
                    w('    c.viReady[%d] = c.cyc + 1u;' % r)
        if ebit[i]:
            w('    end_%d = true;' % i)
        is_branch = lw is not None and lw.branch is not None
        if is_branch and (dbit[i] or tbit[i]):
            # D/T num desvio: para depois do delay slot (haltAfterDelaySlot)
            w('    halt_%d = %s;' % (i, halt_cond(i)))
        w('    ++c.cyc;')
        nxt = (pc + 8) & CODE_MASK
        # proximo PC deste par (destino do desvio se for delay slot)
        np_expr = '0x%04xu' % nxt
        if i in delay_of:
            b = delay_of[i]
            if lows[b].branch in ('jr', 'jalr'):
                np_expr = '(bt_%d ? jt_%d : 0x%04xu)' % (b, b, nxt)
            else:
                np_expr = '(bt_%d ? 0x%04xu : 0x%04xu)' % (b, (pcs[b] + 8 + imm11(lo[b]) * 8) & CODE_MASK, nxt)
            if dbit[b] or tbit[b]:
                w('    if (halt_%d) { %s return S::finish(c, %s); }' % (b, halt_flags(b), np_expr))
        if i in end_after:
            w('    if (end_%d) return S::finish(c, 0x%04xu);' % (end_after[i], nxt))
        if (dbit[i] or tbit[i]) and not is_branch:
            w('    if (%s) { %s return S::finish(c, %s); }' % (halt_cond(i), halt_flags(i), np_expr))
        if i in delay_of:
            b = delay_of[i]
            bl = lows[b]
            if bl.branch in ('jr', 'jalr'):
                w('    if (bt_%d) { bt_%d = false; np = jt_%d; goto dispatch; }' % (b, b, b))
            else:
                bpc = pcs[b]
                tgt = (bpc + 8 + imm11(lo[b]) * 8) & CODE_MASK
                if in_prog(tgt):
                    w('    if (bt_%d) { bt_%d = false; goto L_%04x; }' % (b, b, tgt))
                else:
                    w('    if (bt_%d) return S::exitTo(c, 0x%04xu);' % (b, tgt))
        if i + 1 >= n:
            w('    return S::exitTo(c, 0x%04xu);' % nxt)
        w('}')
    w('}')
    w('')
    w('struct Register_%s' % fn)
    w('{')
    w('    Register_%s() { Vu1Compiled::registerStaticProgram(0x%04xu, %uu, 0x%sull, &%s, "%s"); }' % (
        fn, dest, len(data), '%016x' % fnv(data, dest), fn, fn))
    w('} s_register_%s;' % fn)
    w('}')
    w('')
    return '\n'.join(out)


# ---- modo por par (#61) -------------------------------------------------------------------------------------

def gen_pair(fn, dest, data, hash_hex):
    size = len(data)
    pcs = [dest + off for off in range(0, size - 7, 8)]
    lines = [
        '// Gerado por tools/vu1_recomp.py (modo por par) -- nao editar (deriva do microcodigo do jogo).',
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
    return '\n'.join(lines)


def generate(path, out_dir, pair_only):
    m = NAME.search(os.path.basename(path))
    if not m:
        return None
    dest, size, hash_hex = int(m.group(1), 16), int(m.group(2)), m.group(3)
    data = open(path, 'rb').read()
    if len(data) != size or fnv(data, dest) != int(hash_hex, 16):
        print('aviso: hash/tamanho nao confere em', path)
        return None
    fn = 'vu1prog_%04x_%s' % (dest, hash_hex)
    mode = 'par'
    text = None
    if not pair_only:
        try:
            text = gen_static(fn, dest, data)
            mode = 'estatico'
        except Unsupported as ex:
            print('  %s: modo por par (%s)' % (fn, ex))
    files = []
    if text is not None:
        files.append((fn, text))
    # a versao por par fica sempre registrada: o runtime usa quando o estatico recusa a entrada
    files.append((fn + '_p', gen_pair(fn + '_p', dest, data, hash_hex)))
    for name, body in files:
        out = os.path.join(out_dir, name + '.cpp')
        if not os.path.exists(out) or open(out, encoding='utf-8').read() != body:
            open(out, 'w', encoding='utf-8', newline='\n').write(body)
    return [name for name, _ in files], size // 8, mode


def merged_chains(src):
    """Blocos MPG contiguos (o fim de um e o destino do proximo) que formam um programa so.

    Os jogos enviam programas grandes em varios MPG; os desvios atravessam a divisa entre blocos. Junta so
    enderecos com uma unica variante (sem ambiguidade de qual bloco segue qual)."""
    by_dest = {}
    for name in sorted(os.listdir(src)):
        m = NAME.search(name)
        if m:
            by_dest.setdefault(int(m.group(1), 16), []).append(os.path.join(src, name))
    single = {d: open(v[0], 'rb').read() for d, v in by_dest.items() if len(v) == 1}
    chains = []
    starts = [d for d in sorted(single) if not any(o + len(single[o]) == d for o in single)]
    for d in starts:
        chain = [d]
        while chain[-1] + len(single[chain[-1]]) in single:
            chain.append(chain[-1] + len(single[chain[-1]]))
        if len(chain) > 1:
            chains.append((d, b''.join(single[x] for x in chain), chain))
    return chains


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    pair_only = '--pair' in sys.argv
    src, out_dir = args[:2]
    os.makedirs(out_dir, exist_ok=True)
    made = []
    for name in sorted(os.listdir(src)):
        r = generate(os.path.join(src, name), out_dir, pair_only)
        if r:
            made.append(r)
    if not pair_only:
        for dest, data, chain in merged_chains(src):
            fn = 'vu1prog_%04x_%016x_m' % (dest, fnv(data, dest))
            try:
                text = gen_static(fn, dest, data)
            except Unsupported as ex:
                print('  %s (blocos %s): nao compilado (%s)' % (fn, ', '.join('%04x' % c for c in chain), ex))
                continue
            out = os.path.join(out_dir, fn + '.cpp')
            if not os.path.exists(out) or open(out, encoding='utf-8').read() != text:
                open(out, 'w', encoding='utf-8', newline='\n').write(text)
            made.append((fn, len(data) // 8, 'estatico'))
            print('  %s: blocos %s juntos' % (fn, ', '.join('%04x' % c for c in chain)))
    keep = set()
    for f, _, _ in made:
        for name in (f if isinstance(f, list) else [f]):
            keep.add(name + '.cpp')
    for name in os.listdir(out_dir):
        if name.startswith('vu1prog_') and name.endswith('.cpp') and name not in keep:
            os.remove(os.path.join(out_dir, name))
    static = sum(1 for _, _, m in made if m == 'estatico')
    print('%d programas (%d estaticos), %d pares -> %s' % (len(made), static, sum(p for _, p, _ in made), out_dir))


if __name__ == '__main__':
    main()
