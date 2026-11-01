"""Memoria do PS2 ao vivo no jogo recompilado (estilo Cheat Engine), para achar e alterar vida, balas, fase etc.

O runtime registra no log "[mem] pid=N rdram=0x..." (RAM do PS2, 32 MB, dentro do processo). Este script le o
ultimo log com essa linha e acessa a memoria do processo com ReadProcessMemory/WriteProcessMemory.
Enderecos sao do PS2 (ex.: 0x0073BE80); kseg0/kseg1 (0x8.../0xA...) sao aceitos e reduzidos.

  python tools/ee_mem.py info
  python tools/ee_mem.py read 0x73BE80 [--n 4] [--type u32|s32|f32|u16|s16|u8]
  python tools/ee_mem.py write 0x73BE80 100 [--type ...]
  python tools/ee_mem.py freeze 0x73BE80 100 [--type ...] [--seconds 600]     # reescreve a cada 50 ms
  python tools/ee_mem.py scan 6 [--type u32]          # nova busca: guarda os candidatos em logs/ee_scan.npz
  python tools/ee_mem.py next 5                        # filtra: candidatos que agora valem 5
  python tools/ee_mem.py next changed|unchanged|dec|inc
  python tools/ee_mem.py list [--max 30]               # candidatos atuais e valores
  python tools/ee_mem.py snap nome                     # salva a RAM inteira em logs/ee_snap_nome.bin

Registre o que for confirmado em docs/MEMORIA.md (endereco, tipo, significado, como foi achado).
"""
import argparse
import ctypes
import glob
import os
import re
import sys
import time

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGS = os.path.join(ROOT, 'logs')
SCAN = os.path.join(LOGS, 'ee_scan.npz')
RAM_SIZE = 32 << 20
TYPES = {'u32': np.uint32, 's32': np.int32, 'f32': np.float32, 'u16': np.uint16, 's16': np.int16, 'u8': np.uint8}

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.OpenProcess.restype = ctypes.c_void_p
k32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.argtypes = k32.ReadProcessMemory.argtypes


def find_target():
    """(pid, rdram) do log mais recente com a linha [mem]."""
    files = sorted(glob.glob(os.path.join(LOGS, 'run_*.log*')) + glob.glob(os.path.join(LOGS, 'play_*.log*')), key=os.path.getmtime, reverse=True)
    rx = re.compile(rb'\[mem\] pid=(\d+) rdram=0x([0-9a-f]+)')
    for f in files[:20]:
        with open(f, 'rb') as h:
            m = None
            for m in rx.finditer(h.read()):
                pass
        if m:
            return int(m.group(1)), int(m.group(2), 16), f
    raise SystemExit('nenhum log com "[mem] pid=" (o jogo esta rodando com o build atual?)')


class Proc:
    def __init__(self):
        self.pid, self.rdram, self.log = find_target()
        self.h = k32.OpenProcess(0x0010 | 0x0020 | 0x0008 | 0x0400, False, self.pid)  # VM_READ|WRITE|OPERATION|QUERY
        if not self.h:
            raise SystemExit('nao abriu o processo %d (o jogo fechou?)' % self.pid)

    def read(self, addr, size):
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t(0)
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(self.rdram + addr), buf, size, ctypes.byref(got)):
            raise SystemExit('ReadProcessMemory falhou (%d)' % ctypes.get_last_error())
        return buf.raw

    def write(self, addr, data):
        got = ctypes.c_size_t(0)
        if not k32.WriteProcessMemory(self.h, ctypes.c_void_p(self.rdram + addr), data, len(data), ctypes.byref(got)):
            raise SystemExit('WriteProcessMemory falhou (%d)' % ctypes.get_last_error())

    def ram(self):
        return np.frombuffer(self.read(0, RAM_SIZE), dtype=np.uint8)


def ps2addr(text):
    return int(text, 0) & 0x1FFFFFF


def parse_value(text, t):
    return np.array([float(text) if t == 'f32' else int(text, 0)]).astype(TYPES[t])


def view(ram, t):
    return ram.view(TYPES[t])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--type', default='u32', choices=TYPES)
    ap.add_argument('--n', type=int, default=1)
    ap.add_argument('--max', type=int, default=30)
    ap.add_argument('--seconds', type=float, default=600.0)
    a = ap.parse_args()
    p = Proc()
    size = np.dtype(TYPES[a.type]).itemsize

    if a.cmd == 'info':
        print('pid=%d rdram=0x%x log=%s' % (p.pid, p.rdram, p.log))
    elif a.cmd == 'read':
        addr = ps2addr(a.args[0])
        vals = np.frombuffer(p.read(addr, size * a.n), dtype=TYPES[a.type])
        for i, v in enumerate(vals):
            print('0x%08x = %s' % (addr + i * size, v))
    elif a.cmd in ('write', 'freeze'):
        addr = ps2addr(a.args[0])
        data = parse_value(a.args[1], a.type).tobytes()
        p.write(addr, data)
        if a.cmd == 'freeze':
            end = time.time() + a.seconds
            print('congelando 0x%08x = %s por %.0f s (Ctrl+C para parar)' % (addr, a.args[1], a.seconds))
            try:
                while time.time() < end:
                    p.write(addr, data)
                    time.sleep(0.05)
            except KeyboardInterrupt:
                pass
        print('ok')
    elif a.cmd == 'scan':
        v = view(p.ram(), a.type)
        target = parse_value(a.args[0], a.type)[0]
        idx = np.nonzero(v == target)[0]
        np.savez(SCAN, idx=idx, vals=v[idx], type=a.type)
        print('%d candidatos (%s == %s)' % (len(idx), a.type, a.args[0]))
    elif a.cmd in ('next', 'list'):
        d = np.load(SCAN)
        idx, old, t = d['idx'], d['vals'], str(d['type'])
        v = view(p.ram(), t)[idx]
        if a.cmd == 'next':
            how = a.args[0]
            keep = {'changed': v != old, 'unchanged': v == old, 'dec': v < old, 'inc': v > old}.get(how)
            if keep is None:
                keep = v == parse_value(how, t)[0]
            idx, v = idx[keep], v[keep]
            np.savez(SCAN, idx=idx, vals=v, type=t)
            print('%d candidatos' % len(idx))
        sz = np.dtype(TYPES[t]).itemsize
        for i in range(min(len(idx), a.max)):
            print('0x%08x = %s' % (int(idx[i]) * sz, v[i]))
    elif a.cmd == 'snap':
        out = os.path.join(LOGS, 'ee_snap_%s.bin' % a.args[0])
        open(out, 'wb').write(p.read(0, RAM_SIZE))
        print(out)
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
