"""Resume um log de execução (logs/run_*.log) ou de build (logs/build_*.log) em poucas linhas.

Feito para economizar contexto de IA: em vez de ler milhares de linhas, leia este relatório.

  python tools/run_report.py logs/run_X.log                 # relatório de execução
  python tools/run_report.py logs/run_X.log --last 5        # janelas finais usadas na média (padrão 5)
  python tools/run_report.py logs/run_X.log --baseline logs/report_Y.json   # compara com outro relatório
  python tools/run_report.py logs/run_X.log --json logs/report_X.json       # grava também em JSON
  python tools/run_report.py logs/build_X.log --build       # só os erros de compilação/link

Métricas vêm das linhas "[run:draws] tick=N chave=valor ..." (PS2X_STATS=1), uma a cada 600 ticks
(≈10 s de jogo). gs.finish = quadros completos na janela; 600 = 60 fps.
"""
import argparse
import json
import re
import statistics
import sys
from collections import Counter

ERROR_PATTERNS = [
    r'\[ERROR\]', r'function not found', r'\bTODO\b', r'exception', r'Unhandled', r'unhandled',
    r'assert', r'DEVICE_LOST', r'access violation', r'\bcrash', r'\bfalha\b', r'\bfailed\b(?!=)',
]
ERROR_RE = re.compile('|'.join(ERROR_PATTERNS), re.IGNORECASE)
KEY_METRICS = ['gs.finish', 'ee.idle_ms', 'vu1.compiled', 'cd.read', 'vif1.direct']


def normalize(line):
    """Troca números/endereços por # para agrupar mensagens repetidas."""
    line = re.sub(r'0x[0-9a-fA-F]+', '0x#', line)
    line = re.sub(r'\d+', '#', line)
    return line.strip()[:160]


def read_lines(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return [l.rstrip('\n').replace('\x00', '') for l in f]


def parse_windows(lines):
    windows = []
    for l in lines:
        if not l.startswith('[run:draws]'):
            continue
        w = {}
        for k, v in re.findall(r'(\S+?)=(\S+)', l):
            try:
                w[k] = float(v) if '.' in v and k != 'tick' else int(v, 0)
            except ValueError:
                pass
        if 'tick' in w:
            windows.append(w)
    return windows


def build_report(path, last):
    lines = read_lines(path)
    rep = {'log': path, 'lines': len(lines)}

    for l in lines:
        m = re.search(r'\[gs-parallel\] ativo: (.+)', l)
        if m:
            rep['gs_backend'] = 'parallel (%s)' % m.group(1).strip()
    rep.setdefault('gs_backend', 'software')

    windows = parse_windows(lines)
    rep['windows'] = len(windows)
    if windows:
        rep['last_tick'] = windows[-1]['tick']
        tail = windows[-last:]
        rep['steady'] = {k: statistics.median(w.get(k, 0) for w in tail) for k in KEY_METRICS}
        rep['fps_est'] = round(rep['steady']['gs.finish'] / 10.0, 1)
        rep['finish_series'] = [w.get('gs.finish', 0) for w in windows]

    errs = Counter(normalize(l) for l in lines if ERROR_RE.search(l))
    rep['errors_total'] = sum(errs.values())
    rep['errors_top'] = errs.most_common(8)
    first = next((l.strip()[:200] for l in lines if ERROR_RE.search(l)), None)
    rep['first_error'] = first

    spam = Counter(normalize(l) for l in lines if l and not l.startswith('[run:draws]'))
    rep['most_frequent'] = spam.most_common(5)
    rep['tail'] = [l[:200] for l in lines[-5:]]
    return rep


def print_report(rep, baseline=None):
    out = []
    out.append('# Relatório: %s (%d linhas)' % (rep['log'], rep['lines']))
    out.append('GS: %s' % rep['gs_backend'])
    if rep.get('windows'):
        s = rep['steady']
        out.append('Janelas: %d, último tick %d' % (rep['windows'], rep['last_tick']))
        out.append('Regime (mediana das últimas janelas): gs.finish=%g (~%.1f fps)  ee.idle_ms=%g  vu1.compiled=%g'
                   % (s['gs.finish'], rep['fps_est'], s['ee.idle_ms'], s['vu1.compiled']))
        series = rep['finish_series']
        if len(series) > 24:
            series = series[:6] + ['…'] + series[-16:]
        out.append('gs.finish por janela: ' + ' '.join(str(x) for x in series))
        if baseline and baseline.get('steady'):
            b = baseline['steady']['gs.finish']
            d = s['gs.finish'] - b
            pct = (100.0 * d / b) if b else 0.0
            out.append('Comparação com %s: %g -> %g (%+g, %+.0f%%)' % (baseline.get('log', '?'), b, s['gs.finish'], d, pct))
    else:
        out.append('Sem linhas [run:draws] (rode com PS2X_STATS=1).')
    out.append('Erros: %d' % rep['errors_total'])
    if rep['first_error']:
        out.append('  primeiro: %s' % rep['first_error'])
    for msg, n in rep['errors_top']:
        out.append('  %5dx %s' % (n, msg))
    out.append('Mensagens mais repetidas:')
    for msg, n in rep['most_frequent']:
        out.append('  %5dx %s' % (n, msg))
    out.append('Fim do log:')
    for l in rep['tail']:
        out.append('  ' + l)
    print('\n'.join(out))


def build_errors(path):
    lines = read_lines(path)
    pat = re.compile(r'error [A-Z]+\d+|: error|fatal error|LNK\d+|FAILED:|ninja: build stopped|CMake Error', re.IGNORECASE)
    hits = [l.strip() for l in lines if pat.search(l)]
    exit_line = next((l for l in reversed(lines) if 'build exit' in l), None)
    seen = []
    for h in hits:
        if h not in seen:
            seen.append(h)
    print('# Build: %s' % path)
    print(exit_line or 'última linha: ' + (lines[-1].strip()[:200] if lines else '(vazio)'))
    print('%d linhas de erro (%d distintas)' % (len(hits), len(seen)))
    for h in seen[:20]:
        print('  ' + h[:300])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--last', type=int, default=5)
    ap.add_argument('--baseline')
    ap.add_argument('--json')
    ap.add_argument('--build', action='store_true')
    a = ap.parse_args()
    if a.build:
        build_errors(a.log)
        return
    rep = build_report(a.log, a.last)
    base = None
    if a.baseline:
        with open(a.baseline, encoding='utf-8') as f:
            base = json.load(f)
    print_report(rep, base)
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump(rep, f, ensure_ascii=False, indent=1)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
