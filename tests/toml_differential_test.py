# SPDX-License-Identifier: GPL-3.0-or-later
"""Run separate original-upstream and Athena processes over a versioned corpus."""
import pathlib
import subprocess
import sys

upstream, athena, directory = sys.argv[1:]
root = pathlib.Path(directory)
count = 0
for row in (root / 'matrix.tsv').read_text().splitlines():
    if not row or row.startswith('#'):
        continue
    filename, profile, expected = row.split()
    def run(executable, *extra):
        return subprocess.run([executable, profile, str(root / filename), *extra],
                              check=True, capture_output=True, text=True, timeout=15).stdout
    original = run(upstream)
    guarded = run(athena)
    if expected == 'ACCEPT':
        assert original.startswith('ACCEPT\n'), (filename, original)
        assert original == guarded, (filename, 'type/value/acceptance mismatch', original, guarded)
        assert run(athena, 'zero-nodes') == 'REJECT_BUDGET\n', filename
    elif expected == 'REJECT_GRAMMAR':
        assert original.startswith('REJECT_GRAMMAR\n') and original == guarded, (filename, original, guarded)
    else:
        # A syntactically valid document rejected by the typed contract. This
        # is intentional authorization, not evidence of a grammar difference.
        assert original.startswith('ACCEPT\n'), (filename, original)
        assert guarded == expected + '\n', (filename, guarded, expected)
    print(f'PASS {filename} profile={profile} expected={expected}')
    count += 1
print(f'PASS {count} independent differential fixtures; budget rejection checked for every valid fixture')
