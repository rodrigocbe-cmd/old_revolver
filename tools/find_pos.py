"""Acha a posicao do jogador por comparacao de RAM (tools/walk_test.ps1): snapshots s0 (parado), s1 (depois de
andar), s2 (parado de novo), s3 (depois de andar de novo), feitos com "ee_mem.py snap". Candidatos: floats finitos,
|v| < 1e5, que mudam ao andar (s0->s1, s2->s3) e nao mudam parado (s1->s2), em trios x, y, z (x e z mudam;
z = x + 8). Imprime os melhores enderecos (do x) e o deslocamento em cada passo.

  python tools/find_pos.py logs/ee_snap_s0.bin logs/ee_snap_s1.bin logs/ee_snap_s2.bin logs/ee_snap_s3.bin
"""
import sys

import numpy as np


def main():
    s = [np.fromfile(p, dtype=np.float32) for p in sys.argv[1:5]]
    with np.errstate(invalid='ignore', over='ignore'):
        ok = np.ones_like(s[0], dtype=bool)
        for a in s:
            ok &= np.isfinite(a) & (np.abs(a) < 1e5)
        moved1 = np.abs(s[1] - s[0]) > 0.05
        still = s[1] == s[2]
        moved2 = np.abs(s[3] - s[2]) > 0.05
    cand = ok & still & (moved1 | moved2)
    # trio: x (i) e z (i+2) candidatos, y (i+1) valido
    idx = np.nonzero(cand[:-2] & cand[2:] & ok[1:-1])[0]
    rows = []
    for i in idx:
        d1 = float(np.hypot(s[1][i] - s[0][i], s[1][i + 2] - s[0][i + 2]))
        d2 = float(np.hypot(s[3][i] - s[2][i], s[3][i + 2] - s[2][i + 2]))
        # fora das pilhas (0x1E00000+), passos parecidos (mesmo tempo andando) e plausiveis
        if i * 4 < 0x1E00000 and 0.05 < d1 < 100 and 0.05 < d2 < 100 and 0.33 < d1 / d2 < 3:
            rows.append((i * 4, d1, d2, s[3][i], s[3][i + 1], s[3][i + 2]))
    print('candidatos: %d floats, %d trios' % (int(cand.sum()), len(rows)))
    for addr, d1, d2, x, y, z in rows[:15]:
        print('0x%07X d1=%.3f d2=%.3f  xyz=(%.2f, %.2f, %.2f)' % (addr, d1, d2, x, y, z))


if __name__ == '__main__':
    main()
