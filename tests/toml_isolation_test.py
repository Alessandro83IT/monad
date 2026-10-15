# SPDX-License-Identifier: GPL-3.0-or-later
"""Structural/symbol evidence for the namespace boundary, not a universal ODR proof."""
import json
import pathlib
import re
import shlex
import subprocess
import sys

source, build, library, original_probe = map(pathlib.Path, sys.argv[1:])
commands = json.loads((build / 'compile_commands.json').read_text())
original_name = re.compile(r'(?<![A-Za-z_])toml::')
fork_name = re.compile(r'(?<![A-Za-z_])athena_toml::')

def symbols(file):
    return subprocess.run(['nm', '-C', '--defined-only', str(file)], check=True,
                          capture_output=True, text=True).stdout

fork_objects = 0
for entry in commands:
    file = pathlib.Path(entry['file'])
    args = shlex.split(entry['command'])
    if file.name == 'toml_upstream_consumer_test.cpp':
        assert not any('third_party' in a or a.endswith('/src') for a in args), entry
    if file.name == 'toml_probe.cpp' and 'ATHENA_TOML_PROBE' not in entry['command']:
        assert 'third_party' not in entry['command'], entry
    if file.is_relative_to(source / 'src') and 'athena_toml' in file.read_text():
        obj = pathlib.Path(entry['directory']) / args[args.index('-o') + 1]
        defined = symbols(obj)
        assert not original_name.search(defined), f'upstream namespace defined in fork TU: {file}'
        fork_objects += 1
assert fork_objects >= 8, fork_objects
original = symbols(original_probe)
assert original_name.search(original), 'upstream probe has no original symbols'
assert not fork_name.search(original), 'upstream probe contains fork definitions'
core = symbols(library)
assert fork_name.search(core), 'library has no isolated fork symbols'
# Core also legitimately uses the unchanged upstream in config/package/manifest.
# The extended parser and observer must never exist in that namespace.
assert not re.search(r'(?<![A-Za-z_])toml::[^\n]*(athena_parser|parse_observer)', core)
for header in (source / 'include').rglob('*.hpp'):
    assert 'athena_toml' not in header.read_text(), f'fork exposed in public header: {header}'
print(f'PASS {fork_objects} fork translation units define no upstream toml symbols')
print('PASS original probe, public consumer include isolation, private headers, observer/parser namespace separation')
print('LIMIT: finite structural/symbol evidence; not a mathematical proof for every external consumer/configuration')
