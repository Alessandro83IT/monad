#!/usr/bin/env python3
"""Read-only verifier for the fixed historical Session 35 evidence tree."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

EXPECTED_ROOT = "/home/alessandro/monad-alpha-m1-evidence/alpha-m1-vm-evidence-20261021T145020Z-86810"
EXPECTED_FILES = 149
EXPECTED_TREE_SHA256 = "f229f1b367f31053a283f564b3d2fa3edd713bfa1ede5134c899466f1bb8f67f"
DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW


class Blocked(RuntimeError):
    pass


def _open_root(path: str) -> int:
    if path != EXPECTED_ROOT or not os.path.isabs(path):
        raise Blocked(f"unexpected evidence root; only {EXPECTED_ROOT!r} is accepted")
    components = path.split("/")[1:]
    if not components or any(p in ("", ".", "..") for p in components):
        raise Blocked("evidence root must be canonical absolute path")
    fd = os.open("/", DIR_FLAGS)
    try:
        for component in components:
            child = os.open(component, DIR_FLAGS, dir_fd=fd)
            os.close(fd)
            fd = child
        if not stat.S_ISDIR(os.fstat(fd).st_mode):
            raise Blocked("evidence root is not a directory")
        return fd
    except BaseException:
        os.close(fd)
        raise


def calculate_tree(root: str = EXPECTED_ROOT) -> tuple[list[tuple[str, str]], int, str]:
    """Reproduce Session 38's documented algorithm without following links.

    The aggregate stream, in depth-first sorted order, is for each regular file:
    uint64-be path-byte-length, relative path bytes, uint64-be opened file size,
    then the exact file bytes. Directories and metadata are not themselves
    hashed. Names are ordered by os.fsencode bytes; path components use `/`.
    """
    root_fd = _open_root(root)
    tree = hashlib.sha256()
    rows: list[tuple[str, str]] = []

    def walk(directory_fd: int, prefix: bytes) -> None:
        try:
            directory_before = os.fstat(directory_fd)
            names = sorted(os.listdir(directory_fd), key=os.fsencode)
        except OSError as exc:
            raise Blocked(f"cannot list evidence directory: {exc}") from exc
        for name in names:
            name_b = os.fsencode(name)
            relative = name_b if not prefix else prefix + b"/" + name_b
            try:
                before = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
                if stat.S_ISLNK(before.st_mode):
                    raise Blocked(f"symlink in evidence tree: {os.fsdecode(relative)}")
                if stat.S_ISDIR(before.st_mode):
                    child_fd = os.open(name, DIR_FLAGS, dir_fd=directory_fd)
                    try:
                        opened_dir = os.fstat(child_fd)
                        if (opened_dir.st_dev, opened_dir.st_ino) != (before.st_dev, before.st_ino):
                            raise Blocked(f"directory changed during walk: {os.fsdecode(relative)}")
                        walk(child_fd, relative)
                    finally:
                        os.close(child_fd)
                    continue
                if not stat.S_ISREG(before.st_mode):
                    raise Blocked(f"special file in evidence tree: {os.fsdecode(relative)}")
                file_fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                                  dir_fd=directory_fd)
            except OSError as exc:
                raise Blocked(f"cannot safely open {os.fsdecode(relative)}: {exc}") from exc
            try:
                opened = os.fstat(file_fd)
                if not stat.S_ISREG(opened.st_mode) or (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
                    raise Blocked(f"file changed during walk: {os.fsdecode(relative)}")
                path_hash = hashlib.sha256()
                tree.update(len(relative).to_bytes(8, "big"))
                tree.update(relative)
                tree.update(opened.st_size.to_bytes(8, "big"))
                total = 0
                while True:
                    block = os.read(file_fd, 1024 * 1024)
                    if not block:
                        break
                    total += len(block)
                    path_hash.update(block)
                    tree.update(block)
                after = os.fstat(file_fd)
                identity_before = (opened.st_dev, opened.st_ino, opened.st_size,
                                   opened.st_mtime_ns, opened.st_ctime_ns)
                identity_after = (after.st_dev, after.st_ino, after.st_size,
                                  after.st_mtime_ns, after.st_ctime_ns)
                if total != opened.st_size or identity_before != identity_after:
                    raise Blocked(f"file changed while hashing: {os.fsdecode(relative)}")
                rows.append((path_hash.hexdigest(), os.fsdecode(relative)))
            except OSError as exc:
                raise Blocked(f"read failed for {os.fsdecode(relative)}: {exc}") from exc
            finally:
                os.close(file_fd)
        try:
            names_after = sorted(os.listdir(directory_fd), key=os.fsencode)
            directory_after = os.fstat(directory_fd)
        except OSError as exc:
            raise Blocked(f"cannot recheck evidence directory: {exc}") from exc
        before_identity = (directory_before.st_dev, directory_before.st_ino,
                           directory_before.st_mtime_ns, directory_before.st_ctime_ns)
        after_identity = (directory_after.st_dev, directory_after.st_ino,
                          directory_after.st_mtime_ns, directory_after.st_ctime_ns)
        if names_after != names or before_identity != after_identity:
            raise Blocked(f"directory changed while walking: {os.fsdecode(prefix) or '.'}")

    try:
        walk(root_fd, b"")
    finally:
        os.close(root_fd)
    return rows, len(rows), tree.hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", default=EXPECTED_ROOT,
                        help="fixed historical evidence path; any other path is rejected")
    parser.add_argument("--files-only", action="store_true", help="emit per-file hashes without final PASS/FAIL comparison")
    ns = parser.parse_args(argv)
    try:
        rows, count, digest = calculate_tree(ns.root)
        for file_digest, name in rows:
            print(json.dumps({"sha256": file_digest, "path": name}, ensure_ascii=True, sort_keys=True))
        matches = count == EXPECTED_FILES and digest == EXPECTED_TREE_SHA256
        print(json.dumps({"files": count, "tree_sha256": digest,
                          "expected_files": EXPECTED_FILES,
                          "expected_tree_sha256": EXPECTED_TREE_SHA256,
                          "result": "NOT_COMPARED" if ns.files_only else ("PASS" if matches else "FAIL")},
                         sort_keys=True))
        return 0 if ns.files_only or matches else 1
    except (OSError, Blocked) as exc:
        print(f"BLOCKED: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
