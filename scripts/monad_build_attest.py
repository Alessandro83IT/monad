#!/usr/bin/env python3
"""Create and verify a local, non-authenticating source/build record.

The record detects accidental drift. It is not signed and cannot establish
who approved or produced the bytes.
"""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time

FORMAT = 'monad-local-build-attestation-v1'
ROOTS = ('src', 'include', 'tests', 'cmake', 'third_party', 'packaging', 'packages')
FIXED = ('CMakeLists.txt', 'scripts/monad_build_attest.py', 'tests/monad_build_attest_test.py')
EXCLUDED_PARTS = {'__pycache__'}


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def run(argv, cwd, timeout=1200):
    return subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=timeout, check=False)


def source_files(root):
    names = set(FIXED)
    for part in ROOTS:
        base = root / part
        if not base.is_dir() or base.is_symlink():
            raise ValueError(f'missing or symlinked source root: {part}')
        for path in base.rglob('*'):
            rel = path.relative_to(base)
            if EXCLUDED_PARTS.intersection(rel.parts):
                continue
            if path.is_symlink():
                raise ValueError(f'symlink in source closure: {path.relative_to(root)}')
            if path.is_file():
                names.add(path.relative_to(root).as_posix())
    result = []
    for name in sorted(names):
        path = root / name
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'invalid fixed source input: {name}')
        result.append({'path': name, 'sha256': sha256(path), 'size': path.stat().st_size})
    return result


def digest(value):
    data = json.dumps(value, sort_keys=True, separators=(',', ':')).encode()
    return hashlib.sha256(data).hexdigest()


def git(root, *args):
    p = run(['git', *args], root)
    if p.returncode:
        raise RuntimeError(p.stdout)
    return p.stdout


def compiler_info(root, build):
    cache = build / 'CMakeCache.txt'
    cmake = run(['cmake', '--version'], root)
    result = {'cache_sha256': sha256(cache) if cache.is_file() else None,
              'cmake_version': cmake.stdout.strip(), 'cmake_version_exit': cmake.returncode}
    if cache.is_file():
        text = cache.read_text(errors='replace')
        for key in ('CMAKE_CXX_COMPILER:FILEPATH', 'CMAKE_CXX_COMPILER:STRING',
                    'CMAKE_BUILD_TYPE:STRING', 'CMAKE_GENERATOR:INTERNAL',
                    'CMAKE_CXX_FLAGS:STRING', 'CMAKE_PREFIX_PATH:STRING'):
            for line in text.splitlines():
                if line.startswith(key + '='):
                    result[key.split(':')[0]] = line.split('=', 1)[1]
                    break
    cc = result.get('CMAKE_CXX_COMPILER')
    if cc:
        p = run([cc, '--version'], root)
        result['compiler_version'] = p.stdout.strip()
        result['compiler_version_exit'] = p.returncode
    return result


def artifacts(build):
    found = []
    for path in sorted(build.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        try:
            mode = path.stat().st_mode
        except OSError:
            continue
        if (os.access(path, os.X_OK) or '.so' in path.name or path.suffix in ('.a', '.dylib')
                or path.name == 'CTestTestfile.cmake'):
            found.append({'path': path.relative_to(build).as_posix(),
                          'sha256': sha256(path), 'size': path.stat().st_size})
    return found


def create(root, build, output, commands, logs, dependencies):
    root, build, output = root.resolve(), build.resolve(), output.resolve()
    if not build.is_dir() or not build.is_relative_to(root) and build == root:
        raise ValueError('build directory must exist and must not be the repository root')
    if output.exists():
        raise ValueError(f'refusing to overwrite attestation: {output}')
    inputs = source_files(root)
    status = git(root, 'status', '--porcelain=v1', '--untracked-files=all')
    diff = git(root, 'diff', '--no-ext-diff', '--binary', 'HEAD', '--', '.')
    evidence = {
        'format': FORMAT,
        'claim': 'LOCAL_HASH_LINK_ONLY_NOT_AUTHENTICATED_PROVENANCE',
        'created_epoch_utc': int(time.time()),
        'repository': {'head': git(root, 'rev-parse', 'HEAD').strip(),
                       'branch': git(root, 'branch', '--show-current').strip(),
                       'status_porcelain': status,
                       'tracked_diff_sha256': hashlib.sha256(diff.encode()).hexdigest(),
                       'tracked_diff_bytes': len(diff.encode())},
        'source_scope': {'roots': list(ROOTS), 'fixed_files': list(FIXED),
                         'omissions': ['__pycache__ directories', 'files outside listed roots/fixed files'],
                         'entry_count': len(inputs), 'entries': inputs,
                         'manifest_sha256': digest(inputs)},
        'build': {'directory': str(build), 'cmake_cache': compiler_info(root, build),
                  'commands': commands,
                  'logs': [{'path': str(pathlib.Path(item)),
                            'sha256': sha256(pathlib.Path(item)) if pathlib.Path(item).is_file() else None}
                           for item in logs],
                  'dependencies': dependencies,
                  'artifacts': artifacts(build)},
        'limits': ['unsigned local record', 'local hashes do not establish identity, approval, or independent custody',
                   'source closure is explicit and may omit relevant inputs',
                   'artifact hashes do not prove source-to-object causality'],
    }
    evidence['record_digest_sha256'] = digest(evidence)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + '\n')
    return evidence


def verify(root, record):
    root = root.resolve()
    doc = json.loads(record.read_text())
    claimed = doc.pop('record_digest_sha256', None)
    if doc.get('format') != FORMAT or doc.get('claim') != 'LOCAL_HASH_LINK_ONLY_NOT_AUTHENTICATED_PROVENANCE':
        return False, 'unsupported format or claim'
    if claimed != digest(doc):
        return False, 'attestation record digest mismatch'
    current = source_files(root)
    if digest(current) != doc['source_scope']['manifest_sha256']:
        return False, 'source input set or content changed'
    build = pathlib.Path(doc['build']['directory'])
    actual = artifacts(build)
    if actual != doc['build']['artifacts']:
        return False, 'build artifact set or content changed'
    for item in doc['build']['logs']:
        if item['sha256'] is not None and (not pathlib.Path(item['path']).is_file() or sha256(pathlib.Path(item['path'])) != item['sha256']):
            return False, 'build log changed or missing'
    if sha256(build / 'CMakeCache.txt') != doc['build']['cmake_cache']['cache_sha256']:
        return False, 'CMake cache changed'
    return True, 'local record, inputs, cache, and artifacts are internally consistent; not authenticated'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    c = sub.add_parser('create')
    c.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    c.add_argument('--build', type=pathlib.Path, required=True)
    c.add_argument('--output', type=pathlib.Path, required=True)
    c.add_argument('--command', action='append', default=[])
    c.add_argument('--log', action='append', default=[])
    c.add_argument('--dependency', action='append', default=[])
    v = sub.add_parser('verify')
    v.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    v.add_argument('--record', type=pathlib.Path, required=True)
    a = parser.parse_args()
    try:
        if a.action == 'create':
            doc = create(a.root, a.build, a.output, a.command, a.log, a.dependency)
            print(f"created {a.output}; inputs={doc['source_scope']['entry_count']} artifacts={len(doc['build']['artifacts'])}")
            print(doc['claim'])
            return 0
        ok, message = verify(a.root, a.record)
        print(('PASS: ' if ok else 'FAIL: ') + message)
        return 0 if ok else 1
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
