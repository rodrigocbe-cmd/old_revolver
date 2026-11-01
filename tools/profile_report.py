"""Resume amostras de pilha do cdb (tools/profile.ps1) em poucas linhas.

  python tools/profile_report.py logs/prof_X            # thread do jogo ("GameThread")
  python tools/profile_report.py logs/prof_X --thread 0 # outra thread (índice do cdb, ex.: 0 = janela/apresentação)
  python tools/profile_report.py logs/prof_X --all      # resumo de todas as threads ocupadas

Para cada amostra pega a pilha da thread, e conta:
  - categoria: o primeiro quadro (do topo para baixo) que casa com uma categoria conhecida;
  - exclusivo: a função no topo da pilha;
  - inclusivo: cada função presente na pilha (uma vez por amostra).
"""
import argparse
import glob
import os
import re
import sys
from collections import Counter, defaultdict

CATEGORIES = [
    ('espera', r'ZwWaitFor|NtWaitFor|ZwDelayExecution|SleepEx|WaitForSingle|WaitForMultiple|SleepConditionVariable|_Cnd_wait|_Thrd_sleep|WaitTime|NtYield|SwitchToThread'),
    ('vu1', r'Vu1Compiled|VU1Interpreter|vu1prog_'),
    ('gs-gpu', r'ps2_gs_parallel|ParallelGS::|Vulkan::|nvoglv|nvwgf|nvvk|vulkan-1'),
    ('gs-cpu', r'GSCpuBackend|GSThreadedBackend|GS::|gs_cpu|SubmitBand'),
    ('gif/vif/dma', r'GifArbiter|PS2Memory::process|VIF|Vif|drainMfifo|tryProcessScratchpadDma|submitGifPacket'),
    ('iop', r'Iop|iop_|ps2x::iop'),
    ('kernel/sched', r'EeScheduler|handleSyscall|Syscalls|ps2_syscalls'),
    ('mem-acesso', r'PS2Runtime::(Load|Store)\d+|PS2Memory::(read|write)\d+|writeIORegister|readIORegister'),
    ('jogo-recompilado', r'FUN_[0-9a-f]+|dispatchGuestBranch|lookupFunction'),
    ('audio', r'ps2_audio|Spu|SPU|Audio'),
    ('apresentacao', r'UploadFrame|DrawTexture|EndDrawing|rlgl|glfw'),
]
CAT_RE = [(name, re.compile(rx)) for name, rx in CATEGORIES]
FRAME_RE = re.compile(r'^(?:[0-9a-f]{8}`[0-9a-f]{8}|\(Inline Function\))\s+\S+\s+(\S+)')
THREAD_RE = re.compile(r'^[.#]?\s*(\d+)\s+Id:\s+\S+.*?(?:"([^"]*)")?\s*$')


def func_name(site):
    site = re.sub(r'\+0x[0-9a-f]+$', '', site)
    return site.split('!', 1)[1] if '!' in site else site


def parse_sample(path):
    threads = {}
    cur = None
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n').lstrip('﻿')
            m = THREAD_RE.match(line)
            if m and 'Id:' in line:
                cur = (int(m.group(1)), m.group(2) or '')
                threads[cur] = []
                continue
            if cur is None:
                continue
            m = FRAME_RE.match(line)
            if m:
                threads[cur].append(func_name(m.group(1)))
    return threads


def categorize(stack):
    for fn in stack:
        for name, rx in CAT_RE:
            if rx.search(fn):
                return name
    return 'outros'


def summarize(stacks, title, top):
    n = len(stacks)
    if not n:
        print('%s: nenhuma amostra' % title)
        return
    cats = Counter(categorize(s) for s in stacks)
    excl = Counter(s[0] for s in stacks if s)
    incl = Counter()
    for s in stacks:
        for fn in set(s):
            incl[fn] += 1
    print('## %s (%d amostras)' % (title, n))
    print('Categorias: ' + ', '.join('%s %d%%' % (c, round(100 * k / n)) for c, k in cats.most_common()))
    print('Topo da pilha (exclusivo):')
    for fn, k in excl.most_common(top):
        print('  %3d%% %s' % (round(100 * k / n), fn[:140]))
    print('Inclusivo (fora std::/ntdll):')
    shown = 0
    for fn, k in incl.most_common():
        if fn.startswith('std::') or fn.startswith('Rtl') or fn in ('wcsrchr', 'BaseThreadInitThunk', 'RtlUserThreadStart', 'invoke_main', '__scrt_common_main_seh'):
            continue
        print('  %3d%% %s' % (round(100 * k / n), fn[:140]))
        shown += 1
        if shown >= top:
            break


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir')
    ap.add_argument('--thread', type=int)
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--top', type=int, default=12)
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.dir, 'sample_*.txt')))
    samples = [parse_sample(f) for f in files]
    if not samples:
        print('nenhuma amostra em ' + a.dir)
        return

    if a.all:
        per = defaultdict(list)
        for s in samples:
            for key, stack in s.items():
                per[key].append(stack)
        busy = []
        for key, stacks in per.items():
            active = [st for st in stacks if st and categorize(st) != 'espera']
            busy.append((len(active), key, stacks))
        busy.sort(reverse=True)
        for nact, key, stacks in busy[:6]:
            if nact == 0:
                break
            summarize(stacks, 'thread %d %s (ocupada em %d/%d)' % (key[0], key[1], nact, len(stacks)), 5)
        return

    stacks = []
    for s in samples:
        for (idx, name), stack in s.items():
            if (a.thread is not None and idx == a.thread) or (a.thread is None and name == 'GameThread'):
                stacks.append(stack)
    summarize(stacks, 'thread %s' % (a.thread if a.thread is not None else 'GameThread'), a.top)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
