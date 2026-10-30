#!/usr/bin/env python3
"""Synthetic positive and tamper-detection checks for monad_build_attest."""
import importlib.util
import json
import pathlib
import subprocess
import tempfile
import unittest

MODULE = pathlib.Path(__file__).resolve().parents[1] / 'scripts/monad_build_attest.py'
spec = importlib.util.spec_from_file_location('monad_build_attest', MODULE)
attest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(attest)


class AttestationTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name) / 'repo'
        self.root.mkdir()
        for name in attest.ROOTS:
            (self.root / name).mkdir()
        (self.root / 'scripts').mkdir()
        (self.root / 'scripts/monad_build_attest.py').write_text('# synthetic tool\n')
        (self.root / 'tests/monad_build_attest_test.py').write_text('# synthetic test\n')
        (self.root / 'src/input.cpp').write_text('int value = 1;\n')
        (self.root / 'CMakeLists.txt').write_text('project(synthetic)\n')
        subprocess.run(['git', 'init', '-q'], cwd=self.root, check=True)
        subprocess.run(['git', 'config', 'user.email', 'test@example.invalid'], cwd=self.root, check=True)
        subprocess.run(['git', 'config', 'user.name', 'Test'], cwd=self.root, check=True)
        subprocess.run(['git', 'add', '.'], cwd=self.root, check=True)
        subprocess.run(['git', 'commit', '-qm', 'synthetic'], cwd=self.root, check=True)
        self.build = self.root / 'isolated-build'
        self.build.mkdir()
        (self.build / 'CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Debug\n')
        (self.build / 'fake_test').write_bytes(b'first artifact')
        (self.build / 'fake_test').chmod(0o755)
        self.record = self.root / 'evidence/record.json'
        attest.create(self.root, self.build, self.record, ['cmake --build isolated-build'],
                      ['build.log'], ['OpenSSL synthetic'])

    def tearDown(self):
        self.tmp.cleanup()

    def test_valid_record_verifies(self):
        self.assertEqual(attest.verify(self.root, self.record)[0], True)

    def test_source_edit_is_detected(self):
        (self.root / 'src/input.cpp').write_text('int value = 2;\n')
        self.assertIn('source input', attest.verify(self.root, self.record)[1])

    def test_artifact_replacement_is_detected(self):
        (self.build / 'fake_test').write_bytes(b'replaced artifact')
        self.assertIn('artifact', attest.verify(self.root, self.record)[1])

    def test_record_edit_is_detected(self):
        doc = json.loads(self.record.read_text())
        doc['build']['commands'].append('unrecorded')
        self.record.write_text(json.dumps(doc))
        self.assertIn('record digest', attest.verify(self.root, self.record)[1])

    def test_symlink_input_is_rejected(self):
        link = self.root / 'src/linked.cpp'
        link.symlink_to(self.root / 'src/input.cpp')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            attest.source_files(self.root)

    def test_create_refuses_overwrite(self):
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            attest.create(self.root, self.build, self.record, [], [], [])


if __name__ == '__main__':
    unittest.main()
