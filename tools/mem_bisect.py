"""Acha qual endereco controla algo visivel na tela por busca binaria (com o jogo aberto).

Usa os candidatos da ultima busca do tools/ee_mem.py (logs/ee_scan.npz). Em cada rodada escreve um valor de teste
em metade dos candidatos, captura a regiao da tela (fracoes da janela) e compara com a captura original; restaura
os valores e segue com a metade que mudou a tela. Ao final escreve o valor de teste no endereco achado por 2 s
(e restaura) e salva as capturas em logs/bisect_*.png.

  python tools/ee_mem.py scan 156 --type s32
  python tools/mem_bisect.py --value 157 --region 0.14,0.80,0.25,0.85     # x0,y0,x1,y1 da janela

Cuidado: escrever em enderecos aleatorios pode afetar o jogo; os valores sao restaurados a cada rodada.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

import numpy as np
from PIL import ImageGrab

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ee_mem  # noqa: E402

user32 = ctypes.WinDLL('user32', use_last_error=True)
user32.SetProcessDPIAware()


def window_of(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(cb, 0)
    if not found:
        raise SystemExit('janela do jogo nao encontrada')
    return found[0]


def grab(hwnd, region):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    x0, y0, x1, y1 = region
    box = (r.left + int(x0 * w), r.top + int(y0 * h), r.left + int(x1 * w), r.top + int(y1 * h))
    return ImageGrab.grab(bbox=box, all_screens=True)


def diff(a, b):
    return float(np.abs(np.asarray(a, dtype=np.int16) - np.asarray(b, dtype=np.int16)).mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--value', required=True)
    ap.add_argument('--region', required=True)
    ap.add_argument('--settle', type=float, default=0.6)
    ap.add_argument('--threshold', type=float, default=2.0)
    a = ap.parse_args()
    region = [float(x) for x in a.region.split(',')]
    p = ee_mem.Proc()
    hwnd = window_of(p.pid)
    d = np.load(ee_mem.SCAN)
    t = str(d['type'])
    dt = ee_mem.TYPES[t]
    size = np.dtype(dt).itemsize
    cands = [int(i) * size for i in d['idx']]
    test = ee_mem.parse_value(a.value, t).tobytes()
    base = grab(hwnd, region)
    base.save(os.path.join(ee_mem.LOGS, 'bisect_base.png'))
    # ruido da regiao sem escrever nada (animacoes, cintilacao): o limiar fica acima dele
    noise = max(diff(base, grab(hwnd, region)) for _ in range(5) if not time.sleep(a.settle / 2))
    a.threshold = max(a.threshold, noise * 3.0 + 0.05)
    print('ruido %.2f, limiar %.2f' % (noise, a.threshold))

    def changes(addrs):
        old = {x: p.read(x, size) for x in addrs}
        for x in addrs:
            p.write(x, test)
        time.sleep(a.settle)
        img = grab(hwnd, region)
        for x in addrs:
            p.write(x, old[x])
        time.sleep(a.settle)
        return diff(base, img), img

    rnd = 0
    while len(cands) > 1:
        rnd += 1
        half = cands[:len(cands) // 2]
        dval, _ = changes(half)
        print('rodada %d: %d candidatos, metade A muda %.2f' % (rnd, len(cands), dval))
        if dval > a.threshold:
            cands = half
        else:
            rest = cands[len(cands) // 2:]
            dval2, _ = changes(rest)
            if dval2 <= a.threshold:
                print('nenhuma metade muda a tela (valor pode ser lido so em eventos, ou nao esta entre os candidatos)')
                return
            cands = rest
    dval, img = changes(cands)
    img.save(os.path.join(ee_mem.LOGS, 'bisect_found.png'))
    print('achado: 0x%08x (%s), mudanca %.2f' % (cands[0], t, dval) if dval > a.threshold else 'ultimo candidato nao muda a tela')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
