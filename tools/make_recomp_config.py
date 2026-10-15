"""Merges the Ghidra-exported TOML with config/project.toml into the TOML consumed by ps2_recomp.

Usage: python tools/make_recomp_config.py <ghidra_export.toml> <project.toml> <elf> <funcmap.csv> <out_dir> <dest.toml>
"""
import os
import sys
import tomllib


def q(s):
    return '"' + str(s).replace('\\', '/').replace('"', '\\"') + '"'


def value(v):
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, int):
        return str(v)
    if isinstance(v, str):
        return q(v)
    if isinstance(v, list):
        if not v:
            return '[]'
        return '[\n' + ''.join('  %s,\n' % value(x) for x in v) + ']'
    if isinstance(v, dict):
        return '{ ' + ', '.join('%s = %s' % (k, value(x)) for k, x in v.items()) + ' }'
    raise TypeError(type(v))


def merged_list(a, b):
    out = list(a)
    out += [x for x in b if x not in out]
    return out


def main():
    export_path, project_path, elf, csv, out_dir, dest = sys.argv[1:7]
    with open(export_path, 'rb') as f:
        export = tomllib.load(f)
    with open(project_path, 'rb') as f:
        project = tomllib.load(f)

    eg, pg = export.get('general', {}), project.get('general', {})
    general = {
        'input': elf,
        'output': out_dir,
        'ghidra_output': csv,
    }
    for key, val in pg.items():
        if not isinstance(val, list):
            general[key] = val
    general['stubs'] = merged_list(eg.get('stubs', []), pg.get('stubs', []))
    general['entry_points'] = merged_list(
        eg.get('untracked_stubs', []) + eg.get('entry_points', []), pg.get('entry_points', []))
    general['skip'] = merged_list(eg.get('skip', []), pg.get('skip', []))

    # Inicios de funcao fora do mapa do Ghidra, gerados por tools/find_code_gaps.py
    gaps_path = os.path.join(os.path.dirname(project_path), 'code_gaps.toml')
    if os.path.exists(gaps_path):
        with open(gaps_path, 'rb') as f:
            gaps = tomllib.load(f).get('general', {}).get('entry_points', [])
        general['entry_points'] = merged_list(general['entry_points'], gaps)

    lines = ['# Gerado por tools/make_recomp_config.py -- nao editar; edite config/project.toml', '',
             '[general]']
    lines += ['%s = %s' % (k, value(v)) for k, v in general.items()]
    for section in ('patches', 'mmio', 'jump_tables'):
        if section in project:
            lines += ['', '[%s]' % section]
            lines += ['%s = %s' % (k, value(v)) for k, v in project[section].items()]
    with open(dest, 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('%s: %d stubs, %d entry_points, %d skip' % (
        dest, len(general['stubs']), len(general['entry_points']), len(general['skip'])))


if __name__ == '__main__':
    main()
