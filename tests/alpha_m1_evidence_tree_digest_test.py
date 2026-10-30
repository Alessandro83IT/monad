from __future__ import annotations

import hashlib
import importlib.util
import os
from pathlib import Path
import tempfile
import time
import unittest

MODULE_PATH = Path(__file__).resolve().parents[1] / "qualification" / "alpha-m1-evidence-tree-digest.py"
spec = importlib.util.spec_from_file_location("evidence_tree_digest", MODULE_PATH)
digest_tool = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(digest_tool)


class EvidenceTreeDigestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "corpus"
        self.root.mkdir()
        self.original_root = digest_tool.EXPECTED_ROOT
        digest_tool.EXPECTED_ROOT = str(self.root)
        self.addCleanup(setattr, digest_tool, "EXPECTED_ROOT", self.original_root)

    def _calculate(self):
        return digest_tool.calculate_tree(str(self.root))

    def test_documented_framing_is_deterministic_and_path_sensitive(self):
        (self.root / "b.txt").write_bytes(b"B\x00\xff")
        (self.root / "sub").mkdir()
        (self.root / "sub" / "a.txt").write_bytes(b"alpha")
        rows, count, actual = self._calculate()

        expected = hashlib.sha256()
        entries = [(b"b.txt", b"B\x00\xff"), (b"sub/a.txt", b"alpha")]
        for relative, payload in entries:
            expected.update(len(relative).to_bytes(8, "big"))
            expected.update(relative)
            expected.update(len(payload).to_bytes(8, "big"))
            expected.update(payload)
        self.assertEqual(actual, expected.hexdigest())
        self.assertEqual(count, 2)
        self.assertEqual([r[1] for r in rows], ["b.txt", "sub/a.txt"])
        self.assertEqual(self._calculate()[2], actual)

    def test_missing_and_extra_file_change_result(self):
        first = self.root / "first"
        second = self.root / "second"
        first.write_bytes(b"one")
        rows, count, digest_one = self._calculate()
        self.assertEqual((len(rows), count), (1, 1))
        first.unlink()
        second.write_bytes(b"two")
        _, count, digest_two = self._calculate()
        self.assertEqual(count, 1)
        self.assertNotEqual(digest_one, digest_two)
        second.write_bytes(b"two")
        (self.root / "extra").write_bytes(b"extra")
        _, count, _ = self._calculate()
        self.assertEqual(count, 2)

    def test_symlink_and_special_file_are_blocked(self):
        target = self.root / "target"
        target.write_bytes(b"safe")
        (self.root / "link").symlink_to(target)
        with self.assertRaisesRegex(digest_tool.Blocked, "symlink"):
            self._calculate()
        (self.root / "link").unlink()
        if hasattr(os, "mkfifo"):
            os.mkfifo(self.root / "fifo")
            with self.assertRaisesRegex(digest_tool.Blocked, "special file"):
                self._calculate()

    def test_unexpected_root_and_noncanonical_root_are_rejected(self):
        with self.assertRaisesRegex(digest_tool.Blocked, "unexpected evidence root"):
            digest_tool.calculate_tree(str(self.root) + "/../corpus")
        with self.assertRaisesRegex(digest_tool.Blocked, "unexpected evidence root"):
            digest_tool.calculate_tree("/tmp/another-tree")

    def test_changed_file_during_hash_is_detected(self):
        path = self.root / "changing"
        path.write_bytes(b"A" * 32768)
        original_read = digest_tool.os.read
        changed = False

        def racing_read(fd, size):
            nonlocal changed
            data = original_read(fd, min(size, 4096))
            if data and not changed:
                changed = True
                time.sleep(0.02)
                with path.open("r+b") as stream:
                    stream.seek(8192)
                    stream.write(b"Z" * 16)
            return data

        digest_tool.os.read = racing_read
        try:
            with self.assertRaises(digest_tool.Blocked):
                self._calculate()
        finally:
            digest_tool.os.read = original_read

    def test_file_added_during_directory_walk_is_detected(self):
        (self.root / "known").write_bytes(b"known")
        original_listdir = digest_tool.os.listdir
        first = True

        def racing_listdir(fd):
            nonlocal first
            names = original_listdir(fd)
            if first:
                first = False
                (self.root / "unexpected").write_bytes(b"new")
            return names

        digest_tool.os.listdir = racing_listdir
        try:
            with self.assertRaisesRegex(digest_tool.Blocked, "directory changed"):
                self._calculate()
        finally:
            digest_tool.os.listdir = original_listdir


if __name__ == "__main__":
    unittest.main()
