#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reproduce/check the internal fork from an exact installed upstream 3.4.0 tree.

No download is performed. Default mode compares every generated header. --write
regenerates headers only; observer.patch and UPSTREAM.sha256 remain review inputs.
"""
import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import tempfile


def isolate(content):
    # Include paths are strings/header-name tokens. Rewrite these explicitly;
    # preserve every other literal/comment, especially legacy diagnostics.
    def include(match):
        path = match[2].replace('toml++/', 'athena_toml/')
        if path == 'toml.hpp':
            path = 'athena_toml.hpp'
        return match[1] + path + match[3]
    content = re.sub(r'(?m)^(\s*#\s*include\s*[<"])([^>"\r\n]+)([>"])', include, content)
    tokens = re.compile(
        r'//[^\r\n]*|/\*[\s\S]*?\*/|'
        r'(?:u8|u|U|L)?R"(?P<delim>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delim)"|'
        r'(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|'
        r"(?:u8|u|U|L)?'(?:\\[^\r\n]|[^'\\\r\n])*'|"
        r'\b[A-Za-z_]\w*\b')
    def identifier(match):
        token = match[0]
        if token == 'toml':
            return 'athena_toml'
        if token.startswith('TOML'):
            return 'ATHENA_' + token
        if token == 'INCLUDE_TOMLPLUSPLUS_H':
            return 'ATHENA_INCLUDE_TOMLPLUSPLUS_H'
        return token
    return tokens.sub(identifier, content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream', type=pathlib.Path)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent
    files = []
    for line in (root / 'UPSTREAM.sha256').read_text().splitlines():
        expected, name = line.split('  ', 1)
        source = args.upstream / name
        actual = hashlib.sha256(source.read_bytes()).hexdigest()
        if expected != actual:
            raise SystemExit(f'upstream hash mismatch: {name}')
        files.append(name)
    with tempfile.TemporaryDirectory(prefix='athena-toml-vendor-') as directory:
        staging = pathlib.Path(directory)
        for name in files:
            dest = staging / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.upstream / name, dest)
        subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(root / 'observer.patch')],
                       cwd=staging, check=True)
        generated = set()
        for name in files:
            target_name = 'athena_toml.hpp' if name == 'toml.hpp' else name
            generated.add(target_name)
            target = root / target_name
            content = isolate((staging / name).read_text())
            if args.write:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(content)
            elif target.read_text() != content:
                raise SystemExit(f'fork differs from reproducible transform: {target_name}')
        actual_headers = {str(p.relative_to(root)) for p in root.rglob('*') if p.suffix in ('.hpp', '.inl', '.h')}
        if actual_headers != generated:
            raise SystemExit(f'unexpected/missing fork headers: {actual_headers ^ generated}')
    print(f'PASS {len(files)} exact upstream hashes; observer patch; complete namespace/macro transform')


if __name__ == '__main__':
    main()
