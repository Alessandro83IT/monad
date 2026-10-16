#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Clean Debug/Release qualification, without privileged/scanner-stress tests.

Use fresh build-root/output directories. Logs, source/config hashes, commands,
CTest JSON inventory and individual JUnit results are retained in output.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import xml.etree.ElementTree as ET


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


SOURCE_ROOTS = ('src', 'include', 'tests', 'cmake', 'third_party', 'packaging', 'packages')
ROOT_FILES = ('CMakeLists.txt', 'docs/m2.5-d8-b1-parser-boundary.md',
              'docs/m2.5-d8-b1-request-runtime.md', 'docs/m2.5-d8-b2-request-archive.md',
              'docs/m2.5-d8-b3-generation-operations.md', 'docs/m2.5-d8-b4-package-operations.md')
SOURCE_INVENTORY = 'tests/bounded_toml_sources.txt'


def qualification_sources(source):
    """Fail closed against the reviewed, versionable build/test input inventory.

    The architecture document describes this qualification. Historical reports
    outside this scope are context, not required security/build/test inputs.
    Generated Python bytecode is the only omitted content inside SOURCE_ROOTS.
    """
    names = []
    for line in (source / SOURCE_INVENTORY).read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        path = pathlib.PurePosixPath(line)
        if (str(path) != line or path.is_absolute() or '..' in path.parts or
                not (line in ROOT_FILES or path.parts[0] in SOURCE_ROOTS)):
            raise ValueError(f'Input outside declared qualification scope: {line}')
        names.append(line)
    if names != sorted(set(names)):
        raise ValueError('Qualification inventory must be sorted and unique')
    required = set(ROOT_FILES) | {SOURCE_INVENTORY}
    if not required.issubset(names):
        raise ValueError(f'Missing mandatory inventory inputs: {sorted(required - set(names))}')
    discovered = set(ROOT_FILES)
    for name in SOURCE_ROOTS:
        directory = source / name
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError(f'Missing or symlinked source directory: {name}')
        for path in directory.rglob('*'):
            if '__pycache__' in path.relative_to(directory).parts:
                continue
            if path.is_symlink():
                raise ValueError(f'Symlinked qualification input: {path.relative_to(source)}')
            if path.is_file():
                discovered.add(path.relative_to(source).as_posix())
    missing = sorted(set(names) - discovered)
    unlisted = sorted(discovered - set(names))
    if missing or unlisted:
        raise ValueError(f'Qualification inventory differs: missing={missing}; unlisted={unlisted}')
    paths = [source / name for name in names]
    for path in paths:
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Missing or symlinked qualification input: {path.relative_to(source)}')
    return paths


def source_manifest(source):
    return ''.join(f'{sha(path)}  {path.relative_to(source).as_posix()}\n'
                   for path in qualification_sources(source))


def verify_snapshot(source, manifest):
    # Re-discover too: additions/deletions must not evade the end-of-run check.
    if source_manifest(source) != manifest:
        raise ValueError('Qualification source contents changed during the run')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    source = pathlib.Path(__file__).resolve().parent.parent
    build_root = args.build_root.resolve()
    output = args.output.resolve()
    if build_root.exists() or output.exists():
        raise SystemExit('Use new output and build-root paths for clean evidence')
    if not 1 <= args.jobs <= 8:
        raise SystemExit('jobs must be between 1 and 8')
    output.mkdir(parents=True)
    build_root.mkdir(parents=True)
    commands = []
    def run(command, filename, cwd=source):
        commands.append({'command': list(map(str, command)), 'cwd': str(cwd), 'log': filename})
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        print('RUN', *command, flush=True)
        with (output / filename).open('w') as log:
            result = subprocess.run(list(map(str, command)), cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SystemExit(f'FAILED {command}: see {output / filename}')
    manifest = source_manifest(source)
    (output / 'SOURCE.sha256').write_text(manifest)
    environment = {
        'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'platform': platform.platform(),
        'source': str(source), 'build_root': str(build_root),
        'source_manifest_sha256': sha(output / 'SOURCE.sha256'),
        'source_inventory': SOURCE_INVENTORY,
        'source_scope': {'roots': SOURCE_ROOTS, 'root_files': ROOT_FILES,
                         'generated_omission': '__pycache__ directories'},
        'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip(),
        'environment': {k: os.environ[k] for k in ('CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'CMAKE_GENERATOR') if k in os.environ},
        'excluded': 'label privileged; exact artifact_scanner_test; unregistered network test; OOM stress',
    }
    (output / 'environment.json').write_text(json.dumps(environment, indent=2) + '\n')
    run(['c++', '--version'], 'compiler.txt')
    run(['cmake', '--version'], 'cmake-version.txt')
    run(['git', 'status', '--short'], 'git-status-before.txt')
    summary = {}
    for config in ('Debug', 'Release'):
        build = build_root / config.lower()
        run(['cmake', '-S', source, '-B', build, f'-DCMAKE_BUILD_TYPE={config}', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], f'{config}-configure.txt')
        run(['cmake', '--build', build, '-j', str(args.jobs)], f'{config}-build.txt')
        run(['ctest', '--test-dir', build, '--show-only=json-v1'], f'{config}-inventory.json')
        run(['ctest', '--test-dir', build, '-LE', 'privileged', '-E', '^artifact_scanner_test$',
             '--output-on-failure', '--output-junit', output / f'{config}-junit.xml', '-j', '2'], f'{config}-tests.txt')
        (output / f'{config}-ctest-full.txt').write_bytes((build / 'Testing/Temporary/LastTest.log').read_bytes())
        inventory = json.loads((output / f'{config}-inventory.json').read_text())['tests']
        junit = ET.parse(output / f'{config}-junit.xml').getroot()
        results = {}
        for test in junit.iter('testcase'):
            results[test.attrib['name']] = 'FAIL' if test.find('failure') is not None else 'SKIP' if test.find('skipped') is not None else 'PASS'
        for test in inventory:
            results.setdefault(test['name'], 'NOT EXECUTED')
        summary[config] = {'counts': {status: list(results.values()).count(status) for status in ('PASS', 'FAIL', 'SKIP', 'NOT EXECUTED')},
                           'results': dict(sorted(results.items()))}
        (output / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')
        for name in ('CMakeCache.txt', 'compile_commands.json'):
            (output / f'{config}-{name}').write_bytes((build / name).read_bytes())
        artifacts = ('libathena_core.so', 'toml_upstream_probe', 'toml_athena_probe', 'toml_upstream_consumer_test',
                     'bounded_toml_test', 'store_metadata_errors_test', 'operational_request_codec_test',
                     'operational_plan_test', 'operational_activation_test', 'operational_request_runtime_test',
                     'operational_request_archive_test', 'generation_operation_test', 'package_operation_test',
                     'package_operation_scanner_test')
        (output / f'{config}-BUILD.sha256').write_text(''.join(f'{sha(build / name)}  {name}\n' for name in artifacts))
        run(['cmake', '-LA', '-N', build], f'{config}-cache.txt')
    # Confirm that the exact compiled source set stayed fixed during both builds.
    verify_snapshot(source, manifest)
    run(['git', 'diff', '--check'], 'git-diff-check.txt')
    run(['git', 'status', '--short'], 'git-status-after.txt')
    (output / 'ARTIFACTS.sha256').write_text(''.join(f'{sha(p)}  {p.name}\n' for p in sorted(output.iterdir()) if p.is_file() and p.name != 'ARTIFACTS.sha256'))
    print(json.dumps({k: v['counts'] for k, v in summary.items()}, indent=2), flush=True)
    print('SOURCE MANIFEST SHA256', environment['source_manifest_sha256'], flush=True)


if __name__ == '__main__':
    main()
