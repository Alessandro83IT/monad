#!/usr/bin/env python3
"""Non-privileged path-walk tests for the Alpha M1 receipt directory."""

import importlib.util
import os
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
CANARY_PATH = ROOT / "qualification/athena-alpha-m1-systemd-canary.py"
SPEC = importlib.util.spec_from_file_location("alpha_m1_canary", CANARY_PATH)
CANARY = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(CANARY)


class ReceiptDirectoryPathTest(unittest.TestCase):
    def test_opens_normalized_absolute_directory(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-receipt-") as temporary:
            target = pathlib.Path(temporary) / "state" / "receipts"
            target.mkdir(parents=True)
            fd = CANARY._open_directory_nofollow(target)
            try:
                self.assertEqual(os.stat(target).st_ino, os.fstat(fd).st_ino)
                self.assertEqual(os.stat(target).st_dev, os.fstat(fd).st_dev)
            finally:
                os.close(fd)

    def test_rejects_symlink_in_ancestor_component(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-receipt-") as temporary:
            root = pathlib.Path(temporary)
            real = root / "real"
            real.mkdir()
            target = real / "receipts"
            target.mkdir()
            alias = root / "alias"
            alias.symlink_to(real, target_is_directory=True)
            with self.assertRaises(OSError):
                CANARY._open_directory_nofollow(alias / "receipts")

    def test_open_fd_remains_bound_after_path_replacement(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-receipt-") as temporary:
            root = pathlib.Path(temporary)
            target = root / "receipts"
            target.mkdir()
            fd = CANARY._open_directory_nofollow(target)
            original = os.fstat(fd)
            moved = root / "receipts-original"
            target.rename(moved)
            target.mkdir()
            replacement = os.stat(target)
            try:
                pinned = os.fstat(fd)
                self.assertEqual((original.st_dev, original.st_ino), (pinned.st_dev, pinned.st_ino))
                self.assertNotEqual((replacement.st_dev, replacement.st_ino),
                                    (pinned.st_dev, pinned.st_ino))
                # A descriptor-relative lookup must resolve under the original inode,
                # even though the pathname now names a replacement directory.
                marker = os.open("marker", os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600, dir_fd=fd)
                os.close(marker)
                self.assertTrue((moved / "marker").exists())
                self.assertFalse((target / "marker").exists())
            finally:
                os.close(fd)

    def test_rejects_relative_or_dot_component_paths(self):
        with self.assertRaises(RuntimeError):
            CANARY._open_directory_nofollow(pathlib.Path("relative/path"))
        with tempfile.TemporaryDirectory(prefix="alpha-m1-receipt-") as temporary:
            with self.assertRaises(RuntimeError):
                CANARY._open_directory_nofollow(pathlib.Path(temporary) / ".." / "outside")


if __name__ == "__main__":
    unittest.main()
