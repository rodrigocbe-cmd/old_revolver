"""Finds code in .text that the Ghidra function map does not cover and writes entry-point hints.

Functions reached only through pointers (vtables, callbacks) are often missed by the Ghidra
auto-analysis; at runtime they show up as `[guest-branch:missing-target]` and kill the calling
thread. A gap between mapped functions is treated as code when it contains `jr $ra`; inside it, a
function starts at the first non-zero word, right after each `jr $ra` + delay slot, and at a
`jr $ra` that follows a nop (minimal "return x" functions glued to orphan fragments).

Usage: python tools/find_code_gaps.py <elf> <funcmap.csv> <out.toml>
"""
import csv
import struct
import sys

TEXT_START = 0x00100000
TEXT_END = 0x004FEA80          # end of .text of the unpacked ELF (docs/ESTADO.md)
DATA_IN_TEXT = [
    (0x004F02E0, 0x004FB340),  # bloco de dados antes das tabelas da libipu (1321 instrucoes invalidas)
    (0x004FB340, 0x004FD800),  # tabelas da libipu (FUNCOES.md #1)
]
# Dados em lacunas (ex.: 0x4FE6D8-0x4FE798) continuam como limite de funcao, para a funcao
# anterior nao avancar sobre eles; o ps2_recomp reporta as instrucoes invalidas, que nunca sao
# executadas (FUNCOES.md #15).
JR_RA = 0x03E00008


SCRATCH_REGS = {1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25}


def reads_scratch(w):
    """Instrucao inteira que le at/v0/v1/t* (sem valor definido na entrada de uma funcao)."""
    op = w >> 26
    rs, rt = (w >> 21) & 0x1F, (w >> 16) & 0x1F
    if op == 0:
        return rs in SCRATCH_REGS or rt in SCRATCH_REGS
    if op in (0x0F, 0x10, 0x11, 0x12, 0x13):  # lui e coprocessadores
        return False
    if op >= 0x28:  # stores: base e valor
        return rs in SCRATCH_REGS or rt in SCRATCH_REGS
    return rs in SCRATCH_REGS


def is_control(w):
    """Desvio ou salto MIPS (j/jal/jr/jalr/beq.../bc1x/regimm)."""
    op = w >> 26
    if op == 0:
        return (w & 0x3F) in (8, 9)
    return op in (1, 2, 3, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or (op == 0x11 and ((w >> 21) & 0x1F) == 8)


def main():
    elf_path, csv_path, out_path = sys.argv[1:4]
    data = open(elf_path, 'rb').read()
    phoff = struct.unpack_from('<I', data, 0x1C)[0]
    _, off, vaddr, _, filesz, _, _, _ = struct.unpack_from('<8I', data, phoff)

    def word(addr):
        return struct.unpack_from('<I', data, off + addr - vaddr)[0]

    ranges = []
    with open(csv_path, encoding='utf-8') as f:
        for row in csv.reader(f):
            try:
                ranges.append((int(row[1], 16), int(row[2], 16)))
            except (ValueError, IndexError):
                pass
    ranges.sort()

    gaps, cur = [], TEXT_START
    for start, end in ranges:
        if start > cur:
            gaps.append((cur, start))
        cur = max(cur, end)
    gaps.append((cur, TEXT_END))

    starts = []
    for gap_start, gap_end in gaps:
        gap_end = min(gap_end, TEXT_END)
        if gap_end <= gap_start:
            continue
        addrs = list(range(gap_start, gap_end, 4))
        if not any(word(a) == JR_RA for a in addrs):
            # Lacuna sem `jr $ra` (a funcao termina dentro do rotulo seguinte, ex.: 0x3FAC68): o
            # primeiro word nao nulo apos o padding inicia funcao se o codigo anterior terminou num
            # `jr $ra` + delay slot. Sem padding (ex.: 0x2BB100, logo apos FUN_002baf18) so com 4+
            # words: lacunas de 2 words sao sobras de epilogo (`addiu sp; nop`), nao funcoes.
            first = next((a for a in addrs if word(a) != 0), None)
            if (first is not None and word(gap_start - 8) == JR_RA
                    and (first > gap_start or gap_end - first >= 16)
                    and not any(s <= first < e for s, e in DATA_IN_TEXT)):
                starts.append(first)
            continue
        expect_start = True
        i = 0
        while i < len(addrs):
            w = word(addrs[i])
            if expect_start:
                # sobra de delay slot duplicado (instrucao isolada + nop: `addiu sp,sp,+N`,
                # `sltiu v0,v0,1`...) nao inicia funcao: a funcao real vem depois (ex.: 0x2B7FE0
                # apos 0x2B7FD8, 0x234500 apos 0x2344F8). So conta como sobra `addiu sp,sp,+N` ou
                # instrucao que le registrador sem valor na entrada (at/v0/v1/t*); desvios ficam de fora.
                if (w != 0 and not is_control(w) and i + 2 < len(addrs)
                        and word(addrs[i + 1]) == 0 and word(addrs[i + 2]) != 0
                        and (((w >> 16) == 0x27BD and (w & 0x8000) == 0) or reads_scratch(w))):
                    i += 2
                    continue
                if w != 0 and not any(s <= addrs[i] < e for s, e in DATA_IN_TEXT):
                    starts.append(addrs[i])
                    expect_start = False
                    if w == JR_RA:
                        # funcao que comeca no proprio `jr $ra` ("jr ra; lw v0,..", ex.: 0x4C2628)
                        i += 2
                        expect_start = True
                        continue
            elif w == JR_RA:
                # `jr $ra` logo apos um nop costuma ser uma funcao minima ("return x") colada num
                # fragmento orfao: tambem e inicio (ex.: 0x19A6B0, chamado por ponteiro)
                if i > 0 and word(addrs[i - 1]) == 0 and addrs[i] not in starts:
                    starts.append(addrs[i])
                i += 1  # delay slot
                expect_start = True
            i += 1

    # Regioes cobertas so por rotulos entry_ (codigo que o Ghidra viu mas nao organizou em
    # funcoes): cada instrucao apos `jr $ra` + delay slot tambem inicia funcao (ex.: 0x405458,
    # dentro de entry_00405450, chamado por ponteiro).
    named = []
    with open(csv_path, encoding='utf-8') as f:
        for row in csv.reader(f):
            try:
                named.append((int(row[1], 16), int(row[2], 16), row[0]))
            except (ValueError, IndexError):
                pass
    fun_spans = sorted((s, e) for s, e, n in named if n.startswith('FUN_'))

    def in_fun(x):
        lo, hi = 0, len(fun_spans)
        while lo < hi:
            mid = (lo + hi) // 2
            if fun_spans[mid][0] <= x:
                lo = mid + 1
            else:
                hi = mid
        return lo > 0 and fun_spans[lo - 1][0] <= x < fun_spans[lo - 1][1]

    known_starts = set(s for s, _, _ in named) | set(starts)
    for s, e, n in named:
        if n.startswith('FUN_') or in_fun(s):
            continue
        a = s
        while a < e:
            if word(a) == JR_RA:
                # funcao de 2 instrucoes ("lui v0; jr ra; addiu v0" = return "nome") apos padding de
                # nops colado num fragmento orfao (ex.: 0x4BF098, chamado por ponteiro de vtable)
                prev = a - 4
                if (a - 8 >= s and word(a - 8) == 0 and (word(prev) >> 26) == 0x0F and (word(a + 4) >> 26) == 0x09
                        and prev not in known_starts
                        and not in_fun(prev) and not any(ds <= prev < de for ds, de in DATA_IN_TEXT)):
                    starts.append(prev)
                    known_starts.add(prev)
                nxt = a + 8
                # pula o padding de nops entre funcoes minimas (ex.: 0x4BF0A8 em entry_004bf044)
                while nxt < e and nxt < TEXT_END and word(nxt) == 0:
                    nxt += 4
                if (nxt < TEXT_END and word(nxt) != 0 and nxt not in known_starts and not in_fun(nxt)
                        and not any(ds <= nxt < de for ds, de in DATA_IN_TEXT)):
                    starts.append(nxt)
                    known_starts.add(nxt)
                a += 8
            else:
                a += 4

    # Ponteiros no .data (vtables, callbacks) que apontam para .text sem ser inicio conhecido.
    # Tabelas de salto de switch (vizinhos apontando para perto) ficam de fora; so entram alvos
    # logo apos `jr $ra` + delay slot, ou prologos `addiu sp,sp,-N` precedidos de nop.
    known = set(s for s, _ in ranges) | set(starts)
    fun_starts = set(s for s, _, n in named if n.startswith('FUN_'))
    pointer_starts = []
    count = struct.unpack_from('<H', data, 0x2C)[0]
    for k in range(count):
        _, doff, dvaddr, _, dfilesz, _, _, _ = struct.unpack_from('<8I', data, phoff + k * 32)
        if dvaddr != TEXT_END:
            continue

        def dword(addr):
            return struct.unpack_from('<I', data, doff + addr - dvaddr)[0]

        def is_text(x):
            return TEXT_START <= x < TEXT_END and x % 4 == 0

        for a in range(dvaddr, dvaddr + dfilesz - 3, 4):
            x = dword(a)
            if not is_text(x) or x in known or any(s <= x < e for s, e in DATA_IN_TEXT):
                continue
            neighbours = [dword(b) for b in (a - 4, a + 4) if dvaddr <= b < dvaddr + dfilesz - 3]
            # vizinho que ja e inicio de funcao (FUN_ do Ghidra) indica vtable, nao tabela de salto
            # (ex.: 0x4D6620 na vtable 0x5133F4, entre FUN_004d63d0 e FUN_004da0c8)
            if any(is_text(y) and abs(y - x) < 0x400 and y not in fun_starts for y in neighbours):
                continue  # provavel tabela de salto
            w0 = word(x)
            after_ret = word(x - 8) == JR_RA and w0 != 0
            prologue = (w0 >> 16) == 0x27BD and (w0 & 0x8000) != 0 and word(x - 4) == 0
            # funcao folha sem prologo apos um nop, terminando num `jr $ra` proximo (ex.: 0x4C2DE8,
            # volume de capsula chamado pela vtable 0x506B4C, depois de pares "lw v0,N(a1); nop")
            leaf = (word(x - 4) == 0 and w0 != 0 and not is_control(w0)
                    and any(word(x + 4 * j) == JR_RA for j in range(1, 64)))
            if (after_ret or prologue or leaf) and x not in pointer_starts:
                pointer_starts.append(x)
    starts = sorted(set(starts) | set(pointer_starts))

    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Gerado por tools/find_code_gaps.py -- nao editar.\n')
        f.write('# Inicios de funcao em codigo fora do mapa do Ghidra (alcancado so por ponteiro).\n')
        f.write('[general]\nentry_points = [\n')
        for a in starts:
            f.write('  "gap_%08x@0x%08X",\n' % (a, a))
        f.write(']\n')
    print('%d inicios de funcao (%d alvos de ponteiro do .data) -> %s' % (
        len(starts), len(pointer_starts), out_path))


if __name__ == '__main__':
    main()
