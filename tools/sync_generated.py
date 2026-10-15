"""Sincroniza o C++ gerado com o runner do PS2Recomp copiando so o que mudou de conteudo.

Copiar tudo atualiza a data de ~75 mil arquivos e forca o Ninja a recompilar o runner inteiro
(~47 min). Arquivos identicos ficam intocados; .cpp que sumiram de origem sao removidos.

Uso: python tools/sync_generated.py <out/generated> <runner_dir> <include_dir> [<src_extra> ...]
"""
import filecmp
import os
import shutil
import sys


def sync(src_files, dst_dir, prune_ext=None):
    copied = 0
    for src in src_files:
        dst = os.path.join(dst_dir, os.path.basename(src))
        if not os.path.exists(dst) or not filecmp.cmp(src, dst, shallow=False):
            shutil.copy2(src, dst)
            copied += 1
    removed = 0
    if prune_ext:
        keep = {os.path.basename(f) for f in src_files}
        for name in os.listdir(dst_dir):
            if name.endswith(prune_ext) and name not in keep:
                os.remove(os.path.join(dst_dir, name))
                removed += 1
    return copied, removed


def main():
    gen, runner, include, *extra = sys.argv[1:]
    cpps = [os.path.join(gen, f) for f in os.listdir(gen) if f.endswith('.cpp')]
    for d in extra:
        if os.path.isdir(d):
            cpps += [os.path.join(d, f) for f in os.listdir(d) if f.endswith('.cpp')]
    headers = [os.path.join(gen, f) for f in os.listdir(gen) if f.endswith('.h')]
    c_copied, c_removed = sync(cpps, runner, prune_ext='.cpp')
    h_copied, _ = sync(headers, include)
    print(f'{len(cpps)} .cpp no runner: {c_copied} copiados, {c_removed} removidos; headers: {h_copied} copiados')


if __name__ == '__main__':
    main()
