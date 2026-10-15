# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression tests for qualification input closure, using real source copies."""
import importlib.util
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
PROJECT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('qualification', PROJECT / 'tests/qualify_bounded_toml.py')
qualification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qualification)


class SourceClosureTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='athena-qualification-inputs-')
        self.addCleanup(self.directory.cleanup)
        self.source = pathlib.Path(self.directory.name)
        for path in qualification.qualification_sources(PROJECT):
            destination = self.source / path.relative_to(PROJECT)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
        self.manifest = qualification.source_manifest(self.source)

    def test_real_source_copy_qualifies_without_historical_cli_document(self):
        self.assertFalse((self.source / 'docs/m2.5-d8b-cli-integration.md').exists())
        self.assertEqual(self.manifest, qualification.source_manifest(PROJECT))
        qualification.verify_snapshot(self.source, self.manifest)

    def test_missing_required_input_fails(self):
        (self.source / 'CMakeLists.txt').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing or symlinked qualification input'):
            qualification.verify_snapshot(self.source, self.manifest)

    def test_new_unlisted_source_fails(self):
        (self.source / 'src/parsing/unreviewed.cpp').write_text('// unreviewed input\n')
        with self.assertRaisesRegex(ValueError, 'unlisted=.*unreviewed.cpp'):
            qualification.verify_snapshot(self.source, self.manifest)

    def test_modified_input_fails(self):
        path = self.source / 'src/parsing/bounded_toml.cpp'
        path.write_bytes(path.read_bytes() + b'\n')
        with self.assertRaisesRegex(ValueError, 'source contents changed'):
            qualification.verify_snapshot(self.source, self.manifest)

    def test_removed_input_fails(self):
        (self.source / 'src/parsing/bounded_toml.cpp').unlink()
        with self.assertRaisesRegex(ValueError, 'missing=.*bounded_toml.cpp'):
            qualification.verify_snapshot(self.source, self.manifest)

    def test_context_document_cannot_become_a_required_source(self):
        inventory = self.source / qualification.SOURCE_INVENTORY
        inventory.write_text(inventory.read_text() + 'docs/m2.5-d8b-cli-integration.md\n')
        with self.assertRaisesRegex(ValueError, 'outside declared qualification scope'):
            qualification.qualification_sources(self.source)

    def test_symlinked_source_fails(self):
        path = self.source / 'src/parsing/bounded_toml.cpp'
        path.unlink()
        path.symlink_to(PROJECT / 'src/parsing/bounded_toml.cpp')
        with self.assertRaisesRegex(ValueError, 'Symlinked qualification input'):
            qualification.qualification_sources(self.source)


if __name__ == '__main__':
    unittest.main(verbosity=2)
