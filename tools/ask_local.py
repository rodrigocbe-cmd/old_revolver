"""Pergunta ao modelo local (Ollama) sobre arquivos grandes e devolve uma resposta curta.

Serve para economizar tokens da IA principal: o modelo local lê o log/código inteiro e devolve
só o que interessa.

  python tools/ask_local.py logs/run_X.log -q "Qual o primeiro erro e o que veio logo antes?"
  python tools/ask_local.py src/rdr_overrides.cpp -q "Liste as variáveis de ambiente lidas"
  python tools/ask_local.py logs/run_X.log --filter "gs-parallel|ERROR" -q "Resuma"
  git diff | python tools/ask_local.py - -q "Resuma esta mudança em 5 linhas"

Opções: --model (padrão: env RDR_LOCAL_MODEL ou qwen3-coder:30b), --lines N (tamanho máximo da
resposta), --ctx N (contexto do modelo em tokens), --host (padrão http://localhost:11434).

Limites: o modelo local erra e pode inventar. Use a resposta para decidir onde olhar, não como
prova; confira endereços, números e nomes no arquivo original antes de agir.
"""
import argparse
import json
import os
import re
import sys
import urllib.request
from collections import OrderedDict

SYSTEM = (
    "Você analisa arquivos de um projeto de recompilação estática de um jogo de PlayStation 2 "
    "(MIPS R5900 traduzido para C++, runtime que emula GS/VU1/IOP). Responda em português, "
    "de forma direta, em no máximo {lines} linhas. Use apenas o que está no conteúdo fornecido; "
    "cite trechos/linhas exatos quando relevante. Se a informação não estiver no conteúdo, "
    "responda 'não encontrado no conteúdo'. Não invente endereços, nomes ou números."
)


def normalize(line):
    line = re.sub(r'0x[0-9a-fA-F]+', '0x#', line)
    return re.sub(r'\d+', '#', line).strip()


def compact(text, pattern=None):
    """Remove linhas repetidas (mesma forma, números diferentes), mantendo a primeira e a contagem."""
    lines = text.splitlines()
    if pattern:
        rx = re.compile(pattern, re.IGNORECASE)
        lines = [l for l in lines if rx.search(l)]
    groups = OrderedDict()
    for i, l in enumerate(lines):
        key = normalize(l)
        if key in groups:
            groups[key][1] += 1
        else:
            groups[key] = [f'{i + 1}: {l}', 1]
    out = []
    for first, n in groups.values():
        out.append(first if n == 1 else f'{first}   [repetida {n}x]')
    return '\n'.join(out)


def chat(host, model, system, user, ctx):
    body = {
        'model': model,
        'stream': False,
        'messages': [{'role': 'system', 'content': system}, {'role': 'user', 'content': user}],
        'options': {'temperature': 0.2, 'num_ctx': ctx},
    }
    req = urllib.request.Request(host.rstrip('/') + '/api/chat', data=json.dumps(body).encode(),
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=1800) as r:
        resp = json.load(r)
    text = resp.get('message', {}).get('content', '')
    return re.sub(r'<think>.*?</think>', '', text, flags=re.DOTALL).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+', help="arquivos ('-' = entrada padrão)")
    ap.add_argument('-q', '--question', required=True)
    ap.add_argument('--model', default=os.environ.get('RDR_LOCAL_MODEL', 'qwen3-coder:30b'))
    ap.add_argument('--lines', type=int, default=12)
    ap.add_argument('--ctx', type=int, default=32768)
    ap.add_argument('--filter', help='regex: mantém só as linhas que casam')
    ap.add_argument('--host', default=os.environ.get('OLLAMA_HOST', 'http://localhost:11434'))
    a = ap.parse_args()
    if not a.host.startswith('http'):
        a.host = 'http://' + a.host

    parts = []
    for f in a.files:
        raw = sys.stdin.read() if f == '-' else open(f, encoding='utf-8', errors='replace').read()
        parts.append((f, compact(raw, a.filter)))

    # ~3 caracteres por token; reserva espaço para a pergunta e a resposta
    budget = int(a.ctx * 3 * 0.8)
    system = SYSTEM.format(lines=a.lines)
    chunks = []
    for name, text in parts:
        for start in range(0, max(len(text), 1), budget):
            chunks.append((name, text[start:start + budget]))

    if len(chunks) == 1:
        name, text = chunks[0]
        print(chat(a.host, a.model, system, f'Arquivo: {name}\n```\n{text}\n```\n\nPergunta: {a.question}', a.ctx))
        return

    # Conteúdo maior que o contexto: responde por pedaço e depois junta
    notes = []
    for i, (name, text) in enumerate(chunks):
        ans = chat(a.host, a.model, system,
                   f'Arquivo: {name} (parte {i + 1}/{len(chunks)})\n```\n{text}\n```\n\n'
                   f'Pergunta: {a.question}\nSe esta parte não tiver nada relevante, responda só "nada".', a.ctx)
        if ans.strip().lower().strip('.') != 'nada':
            notes.append(f'[parte {i + 1}] {ans}')
    if not notes:
        print('não encontrado no conteúdo')
        return
    print(chat(a.host, a.model, system,
               'Respostas parciais sobre partes de um conteúdo grande:\n' + '\n'.join(notes) +
               f'\n\nJunte numa resposta final à pergunta: {a.question}', a.ctx))


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
