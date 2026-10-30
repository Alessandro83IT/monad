#!/usr/bin/env python3
"""Rebuild the separate M2 source-closure candidate inventory."""
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
ROOTS = ('src', 'include', 'tests', 'cmake', 'third_party', 'packaging', 'packages')
FIXED = (
    'CMakeLists.txt',
    'docs/m2.5-d8-b1-parser-boundary.md',
    'docs/m2.5-d8-b1-request-runtime.md',
    'docs/m2.5-d8-b2-request-archive.md',
    'docs/m2.5-d8-b3-generation-operations.md',
    'docs/m2.5-d8-b4-package-operations.md',
)
UNTRACKED_AT_M2_START = {
    'src/process/run.cpp', 'src/process/run.hpp',
    'tests/alpha_m1_collector_v2_test.py',
    'tests/alpha_m1_collector_v2_trust_test.py',
    'tests/alpha_m1_ctest_inventory_test.py',
    'tests/alpha_m1_evidence_tree_digest_test.py',
    'tests/alpha_m1_installer_transaction_test.py',
    'tests/alpha_m1_receipt_path_test.py',
    'tests/alpha_m1_verified_evidence_runner_test.py',
    'tests/alpha_m1_verified_execution_bootstrap_test.py',
    'tests/alpha_m1_vm_evidence_collector_test.py',
    'tests/process_invocation_test.cpp',
}


def main():
    names = set(FIXED)
    for root_name in ROOTS:
        directory = ROOT / root_name
        if directory.is_symlink() or not directory.is_dir():
            raise SystemExit(f'missing or symlinked source root: {root_name}')
        for path in directory.rglob('*'):
            if '__pycache__' in path.relative_to(directory).parts:
                continue
            if path.is_symlink():
                raise SystemExit(f'symlinked source input: {path.relative_to(ROOT)}')
            if path.is_file():
                names.add(path.relative_to(ROOT).as_posix())

    entries = []
    for name in sorted(names):
        section = name.split('/', 1)[0]
        category = ('functional' if section in ('src', 'include') or name == 'CMakeLists.txt'
                    else 'test' if section == 'tests' else 'build-or-package-input')
        entries.append({
            'path': name,
            'sha256': hashlib.sha256((ROOT / name).read_bytes()).hexdigest(),
            'category': category,
            'untracked_before_m2': name in UNTRACKED_AT_M2_START,
        })
    document = {
        'format': 'monad-alpha-m2-candidate-source-inventory-v1',
        'status': 'CANDIDATE_ONLY_NOT_PROMOTED',
        'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'source_roots': list(ROOTS),
        'fixed_root_files': list(FIXED),
        'generated_omission': '__pycache__ directories',
        'entry_count': len(entries),
        'entries': entries,
    }
    output = ROOT / 'docs/development/monad-0.1-alpha-m2-candidate-inventory-20261027.json'
    output.write_text(json.dumps(document, indent=2) + '\n')
    print(f'{output}: {len(entries)} inputs')


if __name__ == '__main__':
    main()
