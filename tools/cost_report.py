"""Custo normalizado do EE por rodada (independe da cena): ms ocupados por quadro e por mil MSCAL do VU1.

O roteiro do measure.ps1 nao mexe o jogador depois que a fase carrega, entao o FPS de uma rodada depende do que
acontece na cena (o tutorial alterna entre ~29 e ~13 fps de uma rodada para outra). Para comparar versoes, use o
custo por unidade de trabalho das ultimas janelas.

Uso: python tools/cost_report.py <log> [<log> ...]   (janelas [run:draws] de 600 ticks = 10 s)
"""
import re
import sys

WINDOW_MS = 10000.0


def last_windows(path, n=5):
    rows = []
    with open(path, encoding='utf-8', errors='ignore') as f:
        for line in f:
            if 'ee.idle_ms=' in line and '[run:draws]' in line:
                rows.append(dict((k, int(v)) for k, v in re.findall(r'([\w.>=<]+?)=(\d+)(?=\s|$)', line)))
    rows = [r for r in rows if r.get('gs.finish', 0) > 0]
    return rows[-n - 1:-1] if len(rows) > n else rows


for path in sys.argv[1:]:
    rows = last_windows(path)
    if not rows:
        print('%s: sem janelas' % path)
        continue
    fin = sum(r.get('gs.finish', 0) for r in rows)
    busy = sum(WINDOW_MS - r.get('ee.idle_ms', 0) for r in rows)
    mscal = sum(r.get('vif1.mscal', 0) for r in rows)
    print('%s: fps=%.1f  ms/quadro=%.1f  ms/1000mscal=%.2f  mscal/quadro=%d' % (
        path.replace('\\', '/').split('/')[-1], fin / (len(rows) * 10.0), busy / fin, 1000.0 * busy / max(1, mscal),
        mscal / fin))
