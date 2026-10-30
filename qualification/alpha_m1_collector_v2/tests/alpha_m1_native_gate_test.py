"""Session 60 native trust-gate tests; all keys and targets are synthetic."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import sys
import unittest
from qualification.alpha_m1_collector_v2.candidate_identity import (
    AUTHORIZED_COMPONENT_PATHS, aggregate_digest as python_aggregate_digest,
    validate_identity_manifest,
)
from qualification.alpha_m1_collector_v2.aap_v1 import (
    AAPError, canonical_bytes, ledger_authorization_digest, parse_canonical,
    parse_reservation_ledger, signing_bytes, verify_authorization_bundle, verify_envelope,
)

ROOT = Path(__file__).resolve().parents[3]
NATIVE = ROOT / "qualification/alpha_m1_collector_v2/native/verified_bootstrap.cpp"
PKG = ROOT / "qualification/alpha_m1_collector_v2"
FILES = (
    "__init__.py", "aap_v1.py", "adapters.py", "authorization.py", "budget.py",
    "candidate_identity.py", "contracts.py", "nonce_ledger.py", "orchestrator.py",
    "results.py", "safe_fs.py", "synthetic_entry.py", "target_attestation.py",
    "trust_chain.py", "verified_launcher.py",
)
SEED_ROOT = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
ROLE_SEEDS = [bytes([n]) * 32 for n in (0x11, 0x22, 0x33, 0x44)]
ROLE_NAMES = ["candidate-approver", "read-set-approver", "execution-approver", "operator-target-attestor"]
BUDGETS = {
    "bytes_read": 603979776, "bytes_persisted": 4194304, "files": 256,
    "records": 8192, "processes": 4096, "units": 32,
    "duration_seconds": 180, "bytes_per_file": 134217728,
    "bytes_per_source": 268435456, "output_file_bytes": 65536,
    "manifest_bytes": 65536, "seal_bytes": 4096,
}
MODULES = ["identity", "systemd-state", "process-cgroups"]


def canonical(value):
    return canonical_bytes(value)


class Keys:
    def __init__(self, directory: Path):
        self.directory = directory

    def make(self, seed: bytes, name: str):
        key = self.directory / f"{name}.der"
        key.write_bytes(bytes.fromhex("302e020100300506032b657004220420") + seed)
        public_der = subprocess.check_output(["openssl", "pkey", "-inform", "DER", "-in", str(key), "-pubout", "-outform", "DER"], stderr=subprocess.DEVNULL)
        return key, public_der[-32:]

    @staticmethod
    def sign(key: Path, message: bytes):
        source = key.parent / (key.name + ".message")
        target = key.parent / (key.name + ".signature")
        source.write_bytes(message)
        subprocess.run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(key), "-keyform", "DER", "-in", str(source), "-out", str(target)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return target.read_bytes().hex()


class Fixture:
    def __init__(self, base: Path, executable: Path, entry_source: bytes | None = None,
                 component_overrides: dict[str, bytes] | None = None):
        self.base = base
        self.root = base / "candidate"
        self.executable = executable
        self.package = self.root / "qualification/alpha_m1_collector_v2"
        self.package.mkdir(parents=True)
        for name in FILES:
            shutil.copyfile(PKG / name, self.package / name)
        if entry_source is not None:
            (self.package / "synthetic_entry.py").write_bytes(entry_source)
        for name, data in (component_overrides or {}).items():
            if name not in FILES:
                raise ValueError("fixture component override is not allowlisted")
            (self.package / name).write_bytes(data)
        shutil.copytree(PKG / "native", self.package / "native")
        self.keys = Keys(base)
        self.role_keys = []
        self.role_public = []
        for i, seed in enumerate(ROLE_SEEDS):
            key, public = self.keys.make(seed, f"role{i}")
            self.role_keys.append(key); self.role_public.append(public)
        self.root_key, self.root_public = self.keys.make(SEED_ROOT, "root")
        self.now = int(time.time())
        self.nonce = "session60-synthetic-nonce-0001"
        self.target = "athena-synthetic"
        self.candidate_digest = self.write_candidate_manifest()
        self.write_gate()

    def write_candidate_manifest(self):
        records = []
        for name in FILES:
            path = f"qualification/alpha_m1_collector_v2/{name}"
            data = (self.package / name).read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            records.append({"path": path, "bytes": len(data), "sha256": digest})
        aggregate = python_aggregate_digest(records)
        value = {
            "schema_version": 1, "candidate_id": "alpha-m1-collector-v2-core",
            "authorization_contract_version": "alpha-m1-authz-v2", "components": records,
            "aggregate_sha256": aggregate,
            "entrypoint": {"path": "qualification/alpha_m1_collector_v2/synthetic_entry.py", "callable": "main"},
            "read_set_maximum": MODULES, "budget_maximum": BUDGETS,
        }
        encoded = canonical(value)
        (self.package / "candidate_identity.json").write_bytes(encoded)
        return hashlib.sha256(encoded).hexdigest()

    def signed(self, key_index, payload):
        payload["signature"] = self.keys.sign(self.role_keys[key_index], signing_bytes(payload["kind"], payload))
        return payload

    def resign_file(self, path: Path, key_index: int, changes: dict):
        value = json.loads(path.read_text()); value.pop("signature"); value.update(changes)
        value["signature"] = self.keys.sign(self.role_keys[key_index], signing_bytes(value["kind"], value))
        path.write_bytes(canonical(value))

    def write(self, directory, name, value, mode=0o600):
        path = directory / name
        path.write_bytes(canonical(value)); path.chmod(mode)

    def write_gate(self):
        gate = self.root / ".alpha_m1_native_gate"
        gate.mkdir(mode=0o700)
        keys = []
        for i, role in enumerate(ROLE_NAMES):
            _, pub = self.keys.make(ROLE_SEEDS[i], f"pub{i}")
            keys.append({"key_id": f"key-{i}", "principal_id": f"principal-{i}", "role": role,
                         "public_key_hex": pub.hex(), "not_before": self.now - 60, "not_after": self.now + 3600})
        trust = {"algorithm": "Ed25519", "keys": keys, "kind": "trust-store",
                 "protocol_version": "AAP-v1", "schema_version": 1}
        trust["signature"] = self.keys.sign(self.root_key, signing_bytes("trust-store", trust))
        self.write(gate, "trust-store.json", trust)
        base = {"algorithm": "Ed25519", "schema_version": 1, "protocol_version": "AAP-v1",
                "issued_at": self.now - 1, "expires_at": self.now + 240, "nonce": self.nonce,
                "candidate_sha256": self.candidate_digest, "target_id": self.target}
        self.write(gate, "candidate-approval.json", self.signed(0, {**base, "kind": "candidate-identity",
            "key_id": "key-0", "approver_id": "principal-0", "modules": [], "budgets": {}}))
        self.write(gate, "readset-approval.json", self.signed(1, {**base, "kind": "read-set",
            "key_id": "key-1", "approver_id": "principal-1", "modules": MODULES, "budgets": BUDGETS}))
        self.write(gate, "execution-approval.json", self.signed(2, {**base, "kind": "execution",
            "key_id": "key-2", "approver_id": "principal-2", "modules": MODULES, "budgets": BUDGETS}))
        identity = {"target_id": self.target, "machine_id": "0123456789abcdef0123456789abcdef",
                    "hostname": "synthetic-host", "boot_id": "12345678-1234-1234-1234-123456789abc",
                    "snapshot_id": "synthetic-snapshot"}
        target = {"algorithm": "Ed25519", "schema_version": 1, "protocol_version": "AAP-v1",
                  "kind": "target-attestation", "key_id": "key-3", "operator_id": "principal-3",
                  "candidate_sha256": self.candidate_digest, "request_nonce": self.nonce,
                  "target_id": self.target, "declared_identity": identity, "observed_identity": identity,
                  "provenance": "synthetic-provider-v1", "evidence_ref": "synthetic-evidence-1",
                  "issued_at": self.now - 1, "expires_at": self.now + 240,
                  "snapshot_ref": "synthetic-snapshot"}
        self.write(gate, "target-attestation.json", self.signed(3, target))
        ledger_dir = gate / "nonce-ledger"; ledger_dir.mkdir(mode=0o700)
        (ledger_dir / "nonce-ledger.lock").touch(mode=0o600)
        unsigned = {"schema_version": 2, "protocol_version": "AAP-v1",
                    "ledger_id": "alpha-m1-synthetic-ledger-v1", "records": []}
        self.write(ledger_dir, "nonce-ledger.json", {**unsigned, "sha256": hashlib.sha256(canonical(unsigned)).hexdigest()})

    def run(self, mode="--verify-only", env=None):
        return subprocess.run([str(self.executable), mode, str(self.root)], capture_output=True, text=True, env=env)

    def result(self, mode="--verify-only", env=None):
        proc = self.run(mode, env)
        try: data = json.loads(proc.stdout)
        except Exception: data = {"status": "INVALID_OUTPUT", "stdout": proc.stdout, "stderr": proc.stderr}
        return proc, data

    def reserve(self, env=None):
        return self.result("--test-reserve-nonce", env)

    def consume_reservation(self, env=None):
        return self.result("--test-consume-reservation", env)


class NativeGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = Path(tempfile.mkdtemp(prefix="alpha-m1-session60-build-"))
        cls.addClassCleanup(shutil.rmtree, cls.build)
        cls.production = cls.build / "native-verify-only"
        cls.testing = cls.build / "native-test-only-consume"
        cls.handoff = cls.build / "native-session62-test-handoff"
        flags = ["c++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Wsign-conversion", "-Werror"]
        subprocess.run(flags + [str(NATIVE), "-lcrypto", "-o", str(cls.production)], check=True)
        subprocess.run(flags + ["-DSESSION61_TESTING", str(NATIVE), "-lcrypto", "-o", str(cls.testing)], check=True)
        python_define = f'-DSESSION62_PYTHON_EXECUTABLE="{Path(sys.executable).resolve()}"'
        subprocess.run(flags + ["-DSESSION61_TESTING", "-DSESSION62_TESTING", python_define,
                                str(NATIVE), "-lcrypto", "-o", str(cls.handoff)], check=True)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="alpha-m1-session60-fixture-")
        self.fixture = Fixture(Path(self.temp.name), self.production)

    def tearDown(self): self.temp.cleanup()

    def test_valid_gate_is_verification_only(self):
        p, r = self.fixture.result()
        self.assertEqual((p.returncode, r["status"], r["decision"]), (0, "VERIFIED", "VERIFY_ONLY_NO_EXECUTION"))
        self.assertEqual(r["nonce_state"], "AVAILABLE")
        self.assertFalse(r["python_started"]); self.assertFalse(r["nonce_consumed"])
        gate = self.fixture.root / ".alpha_m1_native_gate"
        trust_raw = (gate / "trust-store.json").read_bytes()
        messages = {name: (gate / name).read_bytes() for name in
                    ("candidate-approval.json", "readset-approval.json",
                     "execution-approval.json", "target-attestation.json")}
        python_result = verify_authorization_bundle(
            trust_raw=trust_raw, root_public_key=self.fixture.root_public,
            messages=messages, candidate_sha256=self.fixture.candidate_digest,
            target_id=self.fixture.target, modules=MODULES, budgets=BUDGETS,
        )
        self.assertEqual(python_result["decision"], r["status"])
        self.assertEqual(python_result["target_id"], r["target_id"])

    def test_synthetic_handoff_executes_only_sealed_verified_bytes_and_consumes_nonce(self):
        self.fixture.executable = self.handoff
        proc, result = self.fixture.result("--test-handoff")
        self.assertEqual((proc.returncode, result["status"], result["nonce_state"]),
                         (0, "SYNTHETIC_EXECUTED", "CONSUMED"), msg=f"stdout={proc.stdout!r} stderr={proc.stderr!r}")
        self.assertTrue(result["python_started"]); self.assertTrue(result["nonce_consumed"])
        runtime = result["runtime_result"]
        self.assertEqual(runtime["entry_result"],
                         {"status": "SYNTHETIC_ONLY", "collector_started": False})
        self.assertEqual(runtime["status"], "SYNTHETIC_EXECUTED")
        self.assertTrue({"qualification/alpha_m1_collector_v2/results.py",
                         "qualification/alpha_m1_collector_v2/contracts.py",
                         "qualification/alpha_m1_collector_v2/synthetic_entry.py"}.issubset(
                             set(runtime["loaded_modules"])))
        self.assertEqual(set(runtime["blocked_imports"]),
                         {"qualification.alpha_m1_collector_v2.not_in_manifest",
                          "qualification.alpha_m1_collector_v2.native.fake_extension",
                          "synthetic_entry"})
        self.assertEqual(set(runtime["component_sha256"]), set(AUTHORIZED_COMPONENT_PATHS))
        self.assertFalse((self.fixture.package / "__pycache__").exists())

    def test_path_replacement_after_verification_does_not_change_handoff_bytes(self):
        self.fixture.executable = self.handoff
        environment = os.environ.copy(); environment["M1_SESSION62_FAULT"] = "replace-path-after-verification"
        proc, result = self.fixture.result("--test-handoff", environment)
        self.assertEqual((proc.returncode, result["status"]), (0, "SYNTHETIC_EXECUTED"))
        self.assertEqual(result["runtime_result"]["entry_result"],
                         {"status": "SYNTHETIC_ONLY", "collector_started": False})
        replaced = self.fixture.package / "synthetic_entry.py"
        original = self.fixture.package / ".synthetic_entry.original"
        self.assertIn(b"pathname source must not execute", replaced.read_bytes())
        self.assertEqual(result["runtime_result"]["entry_sha256"], hashlib.sha256(original.read_bytes()).hexdigest())

    def test_symlink_replacement_after_verification_does_not_change_handoff_bytes(self):
        self.fixture.executable = self.handoff
        environment = os.environ.copy(); environment["M1_SESSION62_FAULT"] = "symlink-after-verification"
        proc, result = self.fixture.result("--test-handoff", environment)
        self.assertEqual((proc.returncode, result["status"]), (0, "SYNTHETIC_EXECUTED"))
        self.assertTrue((self.fixture.package / "synthetic_entry.py").is_symlink())
        self.assertEqual(result["runtime_result"]["entry_sha256"],
                         hashlib.sha256((self.fixture.package / ".synthetic_entry.original").read_bytes()).hexdigest())

    def test_source_mutation_during_descriptor_read_is_rejected(self):
        for fault, source in (("truncate-during-read", b"x" * 200000),
                              ("mutate-during-read", b"x" * 1000)):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory(prefix="alpha-m1-session62-read-") as temp:
                fixture = Fixture(Path(temp), self.handoff,
                                  component_overrides={"adapters.py": source})
                environment = os.environ.copy(); environment["M1_SESSION62_FAULT"] = fault
                proc, result = fixture.result("--test-handoff", environment)
                self.assertNotEqual(proc.returncode, 0)
                self.assertNotEqual(result["status"], "SYNTHETIC_EXECUTED")
                self.assertEqual(result["nonce_state"], "NOT_CHECKED")

    def test_component_over_hard_size_limit_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-session62-size-") as temp:
            fixture = Fixture(Path(temp), self.handoff,
                              component_overrides={"adapters.py": b"x" * (1024 * 1024 + 1)})
            proc, result = fixture.result("--test-handoff")
            self.assertNotEqual(proc.returncode, 0)
            self.assertNotEqual(result["status"], "SYNTHETIC_EXECUTED")
            self.assertEqual(result["nonce_state"], "NOT_CHECKED")

    def test_python_isolated_flags_ignore_contaminated_python_environment(self):
        self.fixture.executable = self.handoff
        environment = os.environ.copy()
        import_root = self.fixture.base / "untrusted-imports"
        app_path = import_root / "qualification/alpha_m1_collector_v2"
        app_path.mkdir(parents=True)
        marker = self.fixture.base / "untrusted-import-ran"
        (app_path / "not_in_manifest.py").write_text(f"open({str(marker)!r}, 'w').write('bad')\n")
        site_path = self.fixture.base / "untrusted-user-site/lib/python3.13/site-packages"
        site_path.mkdir(parents=True)
        (site_path / "sitecustomize.py").write_text(f"open({str(marker)!r}, 'w').write('bad')\n")
        environment.update({"PYTHONPATH": str(import_root),
                            "PYTHONHOME": str(self.fixture.base / "invalid-python-home"),
                            "PYTHONUSERBASE": str(self.fixture.base / "untrusted-user-site")})
        proc, result = self.fixture.result("--test-handoff", environment)
        self.assertEqual((proc.returncode, result["status"]), (0, "SYNTHETIC_EXECUTED"))
        self.assertFalse(marker.exists())

    def test_modified_circular_import_component_is_rejected_before_launch(self):
        base_contracts = (PKG / "contracts.py").read_bytes()
        marker = b"from __future__ import annotations\n"
        changed = base_contracts.replace(marker, marker + b"from . import results\n", 1)
        self.assertNotEqual(changed, base_contracts)
        with tempfile.TemporaryDirectory(prefix="alpha-m1-session62-cycle-") as temp:
            fixture = Fixture(Path(temp), self.handoff, component_overrides={"contracts.py": changed})
            proc, result = fixture.result("--test-handoff")
            self.assertNotEqual(proc.returncode, 0)
            self.assertEqual(result["decision"], "FIXED_IMPORT_COMPONENT_MISMATCH")
            self.assertFalse(result["python_started"])
            self.assertEqual(result["nonce_state"], "NOT_CHECKED")

    def test_short_memfd_writes_preserve_complete_source_bytes(self):
        self.fixture.executable = self.handoff
        environment = os.environ.copy(); environment["M1_SESSION62_FAULT"] = "short-handoff-write"
        proc, result = self.fixture.result("--test-handoff", environment)
        self.assertEqual((proc.returncode, result["status"]), (0, "SYNTHETIC_EXECUTED"))

    def test_parent_closes_verified_source_descriptors_immediately_after_fork(self):
        self.fixture.executable = self.handoff
        environment = dict(os.environ, M1_SESSION62_FAULT="check-parent-source-fds")
        proc, result = self.fixture.result("--test-handoff", environment)
        self.assertEqual((proc.returncode, result["status"]), (0, "SYNTHETIC_EXECUTED"),
                         msg=f"stdout={proc.stdout!r} stderr={proc.stderr!r}")
        self.assertTrue(result["python_started"])

    def test_only_the_fixed_harmless_synthetic_entrypoint_can_execute(self):
        with tempfile.TemporaryDirectory(prefix="alpha-m1-session62-entry-") as temp:
            fixture = Fixture(Path(temp), self.handoff,
                              b"import os\nos._exit(99)\n")
            proc, result = fixture.result("--test-handoff")
            self.assertNotEqual(proc.returncode, 0)
            self.assertEqual(result["decision"], "FIXED_SYNTHETIC_ENTRYPOINT_MISMATCH")
            self.assertFalse(result["python_started"])
            self.assertEqual(result["nonce_state"], "NOT_CHECKED")

    def test_bad_descriptor_handoff_fails_closed_and_leaves_nonce_reserved(self):
        for fault in ("unsealed-first", "incomplete-seals-first", "closed-descriptor", "unexpected-descriptor"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory(prefix="alpha-m1-session62-fd-") as temp:
                fixture = Fixture(Path(temp), self.handoff)
                environment = os.environ.copy(); environment["M1_SESSION62_FAULT"] = fault
                proc, result = fixture.result("--test-handoff", environment)
                self.assertNotEqual(proc.returncode, 0)
                self.assertNotEqual(result["status"], "SYNTHETIC_EXECUTED")
                self.assertEqual(result["nonce_state"], "RESERVED")
                retry, retried = fixture.result("--test-handoff")
                self.assertNotEqual(retry.returncode, 0)
                self.assertEqual(retried["status"], "REPLAY")

    def test_crash_boundaries_after_reservation_never_reenable_nonce(self):
        for fault in ("crash-after-reserved", "crash-during-handoff", "crash-after-first-memfd", "crash-runtime", "timeout-runtime"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory(prefix="alpha-m1-session62-crash-") as temp:
                fixture = Fixture(Path(temp), self.handoff)
                environment = os.environ.copy(); environment["M1_SESSION62_FAULT"] = fault
                proc, result = fixture.result("--test-handoff", environment)
                if fault == "crash-after-reserved" or fault in {"crash-during-handoff", "crash-after-first-memfd"}:
                    self.assertEqual(proc.returncode, 86 if fault == "crash-after-reserved" else 87 if fault == "crash-during-handoff" else 88)
                else:
                    self.assertEqual((result["status"], result["nonce_state"]), ("INCONCLUSIVE", "RESERVED"))
                retry, retried = fixture.result("--test-handoff")
                self.assertNotEqual(retry.returncode, 0)
                self.assertEqual(retried["status"], "REPLAY")

    def test_timeout_is_enforced_after_stdout_eof_and_kills_runtime_group(self):
        self.fixture.executable = self.handoff
        marker = self.fixture.base / "late-descendant-marker"
        env = dict(os.environ, M1_SESSION62_FAULT="timeout-runtime-tree",
                   M1_SESSION62_MARKER_PATH=str(marker))
        proc, result = self.fixture.result("--test-handoff", env)
        self.assertNotEqual(proc.returncode, 0)
        self.assertEqual(result["decision"], "SYNTHETIC_HANDOFF_TIMEOUT")
        self.assertTrue(result["python_started"])
        self.assertFalse(result["runtime_cleanup_incomplete"])
        time.sleep(4.2)
        self.assertFalse(marker.exists())
        retry, retried = self.fixture.result("--test-handoff")
        self.assertNotEqual(retry.returncode, 0)
        self.assertEqual(retried["status"], "REPLAY")

    def test_poll_failure_uses_bounded_kill_and_reap_cleanup(self):
        self.fixture.executable = self.handoff
        environment = dict(os.environ, M1_SESSION62_FAULT="poll-error")
        started = time.monotonic()
        proc, result = self.fixture.result("--test-handoff", environment)
        elapsed = time.monotonic() - started
        self.assertNotEqual(proc.returncode, 0)
        self.assertEqual(result["decision"], "HANDOFF_POLL_FAILED")
        self.assertEqual(result["nonce_state"], "RESERVED")
        self.assertFalse(result["runtime_cleanup_incomplete"])
        self.assertLess(elapsed, 3.0)
        retry, retried = self.fixture.result("--test-handoff")
        self.assertNotEqual(retry.returncode, 0)
        self.assertEqual(retried["status"], "REPLAY")

    def test_consume_fsync_failure_reports_ambiguous_state_and_never_releases_nonce(self):
        self.fixture.executable = self.handoff
        env = dict(os.environ, M1_SESSION62_FAULT="fail-consume-dir-fsync")
        proc, result = self.fixture.result("--test-handoff", env)
        self.assertNotEqual(proc.returncode, 0)
        self.assertEqual((result["status"], result["nonce_state"]),
                         ("INCONCLUSIVE", "UNKNOWN_AFTER_FAILURE"))
        self.assertTrue(result["python_started"])
        retry, retried = self.fixture.result("--test-handoff")
        self.assertNotEqual(retry.returncode, 0)
        self.assertEqual(retried["status"], "REPLAY")

    def test_invalid_runtime_result_reports_that_python_started(self):
        self.fixture.executable = self.handoff
        env = dict(os.environ, M1_SESSION62_FAULT="alter-runtime-result")
        proc, result = self.fixture.result("--test-handoff", env)
        self.assertNotEqual(proc.returncode, 0)
        self.assertEqual(result["decision"], "SYNTHETIC_HANDOFF_RESULT_INVALID")
        self.assertTrue(result["python_started"])
        self.assertEqual(result["python_start_state"], "STARTED")
        self.assertEqual(result["nonce_state"], "RESERVED")

    def test_crash_before_reservation_does_not_consume_nonce(self):
        self.fixture.executable = self.handoff
        env = os.environ.copy(); env["M1_SESSION62_FAULT"] = "crash-before-reserved"
        proc = self.fixture.run("--test-handoff", env)
        self.assertEqual(proc.returncode, 85)
        retry, result = self.fixture.result("--test-handoff")
        self.assertEqual((retry.returncode, result["status"], result["nonce_state"]),
                         (0, "SYNTHETIC_EXECUTED", "CONSUMED"))

    def test_concurrent_synthetic_handoffs_have_only_one_start(self):
        self.fixture.executable = self.handoff
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(self.fixture.result, "--test-handoff") for _ in range(2)]
            results = [future.result() for future in futures]
        self.assertEqual(sum(result.get("python_started", False) for _, result in results), 1)
        statuses = [result["status"] for _, result in results]
        self.assertEqual(statuses.count("SYNTHETIC_EXECUTED"), 1)
        self.assertTrue(all(status in {"SYNTHETIC_EXECUTED", "REPLAY", "INCONCLUSIVE"} for status in statuses))

    def test_cpp_canonical_bytes_are_verified_by_python(self):
        payload = {"algorithm": "Ed25519", "schema_version": 1,
                   "protocol_version": "AAP-v1", "kind": "candidate-identity",
                   "key_id": "key-0", "approver_id": "principal-0",
                   "issued_at": self.fixture.now - 1, "expires_at": self.fixture.now + 120,
                   "nonce": self.fixture.nonce, "candidate_sha256": self.fixture.candidate_digest,
                   "target_id": self.fixture.target, "modules": [], "budgets": {}}
        source = json.dumps(payload, indent=2, ensure_ascii=True).encode("ascii")
        proc = subprocess.run([str(self.testing), "--test-canonicalize"],
                              input=source, capture_output=True, check=True)
        cpp_bytes = proc.stdout[:-1]
        self.assertEqual(cpp_bytes, canonical(payload))
        self.assertEqual(hashlib.sha256(cpp_bytes).hexdigest(), hashlib.sha256(canonical(payload)).hexdigest())
        signature = self.fixture.keys.sign(self.fixture.role_keys[0], signing_bytes("candidate-identity", payload))
        envelope = dict(payload, signature=signature)
        verified = verify_envelope(canonical(envelope), expected_kind="candidate-identity",
                                   public_key=self.fixture.role_public[0],
                                   exact_fields=set(envelope), now=self.fixture.now)
        self.assertEqual(verified["candidate_sha256"], self.fixture.candidate_digest)

    def test_aap_python_rejects_ambiguous_or_noncanonical_inputs(self):
        for raw in (b'{"a":1,"a":2}', b'{ "a":1}', b'{"a":1.0}',
                    b'{"a":"\\u00e9"}', b'{"a":"\xff"}',
                    b'{"a":9223372036854775808}'):
            with self.subTest(raw=raw), self.assertRaises(AAPError):
                parse_canonical(raw)
        with self.assertRaises(AAPError):
            verify_envelope(b'{"algorithm":"Ed25519","expires_at":true,"issued_at":1,"kind":"candidate-identity","protocol_version":"AAP-v1","schema_version":1,"signature":"' + b'0' * 128 + b'"}',
                            expected_kind="candidate-identity", public_key=self.fixture.role_public[0],
                            exact_fields={"algorithm", "expires_at", "issued_at", "kind", "protocol_version", "schema_version", "signature"})
        with self.assertRaises(AAPError):
            verify_envelope(b'{"algorithm":"RSA","kind":"candidate-identity","protocol_version":"AAP-v1","schema_version":1,"signature":"' + b'0' * 128 + b'"}',
                            expected_kind="candidate-identity", public_key=self.fixture.role_public[0],
                            exact_fields={"algorithm", "kind", "protocol_version", "schema_version", "signature"})
        with self.assertRaises(AAPError):
            verify_envelope(b'{"algorithm":"Ed25519","kind":"candidate-identity","protocol_version":"AAP-v1","schema_version":true,"signature":"' + b'0' * 128 + b'"}',
                            expected_kind="candidate-identity", public_key=self.fixture.role_public[0],
                            exact_fields={"algorithm", "kind", "protocol_version", "schema_version", "signature"})

    def test_checked_in_candidate_manifest_matches_aap_component_allowlist(self):
        raw = (PKG / "candidate_identity.json").read_bytes()
        manifest = validate_identity_manifest(json.loads(raw))
        self.assertEqual(tuple(row["path"] for row in manifest["components"]), AUTHORIZED_COMPONENT_PATHS)
        self.assertEqual(len(manifest["components"]), len(FILES))
        self.assertEqual(FILES, tuple(path.rsplit("/", 1)[-1] for path in AUTHORIZED_COMPONENT_PATHS))
        malformed = dict(manifest, schema_version=True)
        with self.assertRaises(ValueError):
            validate_identity_manifest(malformed)

    def _write_ledger(self, records):
        path = self.fixture.root / ".alpha_m1_native_gate/nonce-ledger/nonce-ledger.json"
        unsigned = {"schema_version": 2, "protocol_version": "AAP-v1",
                    "ledger_id": "alpha-m1-synthetic-ledger-v1", "records": records}
        ledger = dict(unsigned, sha256=hashlib.sha256(canonical(unsigned)).hexdigest())
        path.write_bytes(canonical(ledger)); path.chmod(0o600)
        return path

    def test_ledger_rejects_malformed_or_duplicate_reservation_ids(self):
        row = {"authorization_sha256": "a" * 64, "candidate_sha256": "b" * 64,
               "ledger_id": "alpha-m1-synthetic-ledger-v1", "nonce": "synthetic-ledger-nonce-0001",
               "protocol_version": "AAP-v1", "reservation_id": "res-" + "c" * 32,
               "state": "RESERVED", "target_id": "synthetic-target"}
        self._write_ledger([row, dict(row, nonce="synthetic-ledger-nonce-0002")])
        path = self.fixture.root / ".alpha_m1_native_gate/nonce-ledger/nonce-ledger.json"
        with self.assertRaises(AAPError):
            parse_reservation_ledger(path.read_bytes())
        p, result = self.fixture.result()
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual((result["status"], result["nonce_state"]), ("INCONCLUSIVE", "INCONCLUSIVE"))

    def test_maximum_ledger_record_count_is_supported_by_both_parsers(self):
        records = []
        for index in range(4096):
            nonce = self.fixture.nonce if index == 0 else f"synthetic-ledger-nonce-{index:04d}"
            records.append({"authorization_sha256": "a" * 64, "candidate_sha256": "b" * 64,
                           "ledger_id": "alpha-m1-synthetic-ledger-v1", "nonce": nonce,
                           "protocol_version": "AAP-v1", "reservation_id": f"res-{index:032x}",
                           "state": "CONSUMED", "target_id": self.fixture.target})
        path = self._write_ledger(records)
        self.assertEqual(len(parse_reservation_ledger(path.read_bytes())["records"]), 4096)
        p, result = self.fixture.result()
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual((result["status"], result["nonce_state"]), ("REPLAY", "REPLAY"))

    def test_reservation_persists_at_maximum_ledger_record_count(self):
        records = []
        for index in range(4095):
            records.append({"authorization_sha256": "a" * 64, "candidate_sha256": "b" * 64,
                           "ledger_id": "alpha-m1-synthetic-ledger-v1",
                           "nonce": f"synthetic-full-ledger-nonce-{index:04d}",
                           "protocol_version": "AAP-v1", "reservation_id": f"res-{index:032x}",
                           "state": "CONSUMED", "target_id": self.fixture.target})
        self._write_ledger(records)
        self.fixture.executable = self.testing
        proc, result = self.fixture.reserve()
        self.assertEqual((proc.returncode, result["status"], result["nonce_state"]),
                         (0, "RESERVED", "RESERVED"))
        ledger_path = self.fixture.root / ".alpha_m1_native_gate/nonce-ledger/nonce-ledger.json"
        persisted = parse_reservation_ledger(ledger_path.read_bytes())
        self.assertEqual(len(persisted["records"]), 4096)
        self.assertGreater(ledger_path.stat().st_size, 1024 * 1024)

    def test_reservation_at_record_capacity_fails_without_mutation(self):
        records = []
        for index in range(4096):
            records.append({"authorization_sha256": "a" * 64, "candidate_sha256": "b" * 64,
                           "ledger_id": "alpha-m1-synthetic-ledger-v1",
                           "nonce": f"synthetic-full-ledger-nonce-{index:04d}",
                           "protocol_version": "AAP-v1", "reservation_id": f"res-{index:032x}",
                           "state": "CONSUMED", "target_id": self.fixture.target})
        path = self._write_ledger(records)
        before = path.read_bytes()
        self.fixture.executable = self.testing
        proc, result = self.fixture.reserve()
        self.assertNotEqual(proc.returncode, 0)
        self.assertEqual((result["status"], result["nonce_state"]),
                         ("INCONCLUSIVE", "UNKNOWN_AFTER_FAILURE"))
        self.assertEqual(path.read_bytes(), before)

    def test_signed_documents_fail_closed_when_altered_or_noncanonical(self):
        path = self.fixture.root / ".alpha_m1_native_gate/trust-store.json"
        value = json.loads(path.read_text()); value["algorithm"] = "RSA"
        path.write_bytes(canonical(value))
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")
        self.assertRaises(AAPError, verify_envelope, path.read_bytes(), expected_kind="trust-store",
                          public_key=self.fixture.root_public,
                          exact_fields={"algorithm", "keys", "kind", "protocol_version", "schema_version", "signature"})

    def test_missing_and_wrong_role_approvals_denied(self):
        gate = self.fixture.root / ".alpha_m1_native_gate"
        (gate / "execution-approval.json").unlink()
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")

    def test_wrong_approver_role_and_expired_approval_denied(self):
        gate = self.fixture.root / ".alpha_m1_native_gate/candidate-approval.json"
        original = json.loads(gate.read_text())
        wrong = dict(original); wrong["key_id"] = "key-1"
        gate.write_bytes(canonical(wrong))
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")
        wrong["expires_at"] = self.fixture.now - 1
        gate.write_bytes(canonical(wrong))
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertEqual(r["status"], "EXPIRED")

    def test_extreme_signed_timestamps_denied_without_overflow(self):
        approval = self.fixture.root / ".alpha_m1_native_gate/candidate-approval.json"
        self.fixture.resign_file(approval, 0, {"issued_at": -(2**63), "expires_at": 2**63 - 1})
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertEqual(r["status"], "EXPIRED")
        target = self.fixture.root / ".alpha_m1_native_gate/target-attestation.json"
        self.fixture.resign_file(target, 3, {"issued_at": -(2**63), "expires_at": 2**63 - 1})
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")

    def test_wrong_root_and_duplicate_json_keys_denied(self):
        gate = self.fixture.root / ".alpha_m1_native_gate/trust-store.json"
        original = gate.read_bytes()
        value = json.loads(original); value["signature"] = "00" * 64
        gate.write_bytes(canonical(value))
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")
        gate.write_bytes(b'{"schema_version":1,"schema_version":1}')
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")

    def test_coincident_principals_are_denied_even_with_root_resigned_store(self):
        gate = self.fixture.root / ".alpha_m1_native_gate"
        trust_path = gate / "trust-store.json"
        trust = json.loads(trust_path.read_text()); trust.pop("signature")
        trust["keys"][1]["principal_id"] = "principal-0"
        trust["signature"] = self.fixture.keys.sign(self.fixture.root_key, signing_bytes("trust-store", trust))
        trust_path.write_bytes(canonical(trust))
        readset = gate / "readset-approval.json"
        self.fixture.resign_file(readset, 1, {"approver_id": "principal-0"})
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")

    def test_expanded_read_set_denied(self):
        gate = self.fixture.root / ".alpha_m1_native_gate/readset-approval.json"
        value = json.loads(gate.read_text()); value["modules"].append("journal")
        gate.write_bytes(canonical(value))
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")

    def test_expanded_budget_and_noncanonical_document_denied(self):
        path = self.fixture.root / ".alpha_m1_native_gate/readset-approval.json"
        value = json.loads(path.read_text()); value["budgets"]["files"] += 1
        path.write_bytes(canonical(value))
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")
        path.write_bytes(b'{ }')
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")

    def test_boolean_budget_is_rejected_identically_by_python_and_cpp(self):
        approval = self.fixture.root / ".alpha_m1_native_gate/readset-approval.json"
        self.fixture.resign_file(approval, 1, {"budgets": {**BUDGETS, "units": True}})
        p, result = self.fixture.result()
        self.assertNotEqual(p.returncode, 0)
        self.assertNotEqual(result["status"], "VERIFIED")
        gate = self.fixture.root / ".alpha_m1_native_gate"
        messages = {name: (gate / name).read_bytes() for name in
                    ("candidate-approval.json", "readset-approval.json",
                     "execution-approval.json", "target-attestation.json")}
        with self.assertRaises(AAPError):
            verify_authorization_bundle(
                trust_raw=(gate / "trust-store.json").read_bytes(),
                root_public_key=self.fixture.root_public, messages=messages,
                candidate_sha256=self.fixture.candidate_digest,
                target_id=self.fixture.target, modules=MODULES, budgets=BUDGETS,
            )

    def test_aap_python_request_cannot_expand_fixed_budget(self):
        gate = self.fixture.root / ".alpha_m1_native_gate"
        messages = {name: (gate / name).read_bytes() for name in
                    ("candidate-approval.json", "readset-approval.json", "execution-approval.json", "target-attestation.json")}
        with self.assertRaises(AAPError):
            verify_authorization_bundle(trust_raw=(gate / "trust-store.json").read_bytes(),
                root_public_key=self.fixture.root_public, messages=messages,
                candidate_sha256=self.fixture.candidate_digest, target_id=self.fixture.target,
                modules=MODULES, budgets={**BUDGETS, "files": 257})

    def test_component_change_and_extra_entry_denied(self):
        source = self.fixture.package / "adapters.py"; source.write_bytes(source.read_bytes() + b"#mutated")
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertEqual(r["status"], "CANDIDATE_MISMATCH")

    def test_symlink_rejected(self):
        source = self.fixture.package / "adapters.py"; backup = self.fixture.base / "adapters-copy"
        shutil.copyfile(source, backup); source.unlink(); source.symlink_to(backup)
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")

    def test_hardlink_and_fifo_rejected(self):
        source = self.fixture.package / "adapters.py"; backup = self.fixture.base / "adapters-copy"
        shutil.copyfile(source, backup); source.unlink(); os.link(backup, source)
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")
        source.unlink(); os.mkfifo(source)
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")

    def test_component_addition_denied(self):
        (self.fixture.package / "unexpected.py").write_text("pass\n")
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertEqual(r["status"], "CANDIDATE_MISMATCH")

    def test_target_mismatch_and_expiry_rejected(self):
        target_file = self.fixture.root / ".alpha_m1_native_gate/target-attestation.json"
        value = json.loads(target_file.read_text()); value["target_id"] = "other-target"
        target_file.write_bytes(canonical(value))
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertEqual(r["status"], "TARGET_MISMATCH")
        self.fixture.resign_file(target_file, 3, {"target_id": self.fixture.target,
                                                  "expires_at": self.fixture.now - 1})
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertEqual(r["status"], "EXPIRED")

    def test_bad_candidate_signature_binding_rejected(self):
        approval = self.fixture.root / ".alpha_m1_native_gate/candidate-approval.json"
        value = json.loads(approval.read_text()); value["candidate_sha256"] = "0" * 64
        approval.write_bytes(canonical(value))
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertEqual(r["status"], "CANDIDATE_MISMATCH")

    def test_corrupt_ledger_inconclusive(self):
        path = self.fixture.root / ".alpha_m1_native_gate/nonce-ledger/nonce-ledger.json"
        path.write_text("{}")
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0)
        self.assertEqual((r["status"], r["nonce_state"]), ("INCONCLUSIVE", "INCONCLUSIVE"))

    def test_pre_ledger_failure_reports_nonce_not_checked(self):
        trust = self.fixture.root / ".alpha_m1_native_gate/trust-store.json"
        trust.write_text("{}")
        p, result = self.fixture.result()
        self.assertNotEqual(p.returncode, 0)
        self.assertNotEqual(result["status"], "VERIFIED")
        self.assertEqual(result["nonce_state"], "NOT_CHECKED")

    def test_ledger_symlink_hardlink_and_directory_replacement_rejected(self):
        ledger = self.fixture.root / ".alpha_m1_native_gate/nonce-ledger/nonce-ledger.json"
        external = self.fixture.base / "ledger-copy"
        shutil.copyfile(ledger, external); ledger.unlink(); ledger.symlink_to(external)
        p, r = self.fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")

        temp = tempfile.TemporaryDirectory(prefix="alpha-m1-session61-hardlink-")
        try:
            fixture = Fixture(Path(temp.name), self.production)
            ledger = fixture.root / ".alpha_m1_native_gate/nonce-ledger/nonce-ledger.json"
            external = fixture.base / "ledger-copy"
            shutil.copyfile(ledger, external); ledger.unlink(); os.link(external, ledger)
            p, r = fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")
        finally: temp.cleanup()

        temp = tempfile.TemporaryDirectory(prefix="alpha-m1-session61-ledgerdir-")
        try:
            fixture = Fixture(Path(temp.name), self.production)
            gate = fixture.root / ".alpha_m1_native_gate"
            ledger_dir = gate / "nonce-ledger"
            backup = gate / "ledger-backup"
            ledger_dir.rename(backup); ledger_dir.symlink_to(backup, target_is_directory=True)
            p, r = fixture.result(); self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "VERIFIED")
        finally: temp.cleanup()

    def test_reservation_and_consumption_are_distinct_test_only_states(self):
        self.fixture.executable = self.testing
        first, reserved = self.fixture.reserve()
        self.assertEqual((first.returncode, reserved["status"], reserved["nonce_state"]), (0, "RESERVED", "RESERVED"))
        self.assertTrue(reserved["reservation_id"])
        second, replay = self.fixture.result()
        self.assertNotEqual(second.returncode, 0)
        self.assertEqual((replay["status"], replay["nonce_state"]), ("REPLAY", "REPLAY"))
        third, consumed = self.fixture.consume_reservation()
        self.assertEqual((third.returncode, consumed["status"], consumed["nonce_state"]), (0, "CONSUMED", "CONSUMED"))
        ledger_path = self.fixture.root / ".alpha_m1_native_gate/nonce-ledger/nonce-ledger.json"
        ledger = parse_reservation_ledger(ledger_path.read_bytes())
        row = ledger["records"][0]
        gate = self.fixture.root / ".alpha_m1_native_gate"
        messages = {name: (gate / name).read_bytes() for name in
                    ("candidate-approval.json", "readset-approval.json", "execution-approval.json", "target-attestation.json")}
        self.assertEqual(row["authorization_sha256"], ledger_authorization_digest(messages))
        self.assertEqual(row["candidate_sha256"], self.fixture.candidate_digest)
        self.assertEqual(row["target_id"], self.fixture.target)
        self.assertEqual(row["state"], "CONSUMED")
        corrupt = dict(ledger); corrupt["sha256"] = "0" * 64
        with self.assertRaises(AAPError):
            parse_reservation_ledger(canonical(corrupt))
        fourth, replayed = self.fixture.reserve()
        self.assertNotEqual(fourth.returncode, 0); self.assertEqual(replayed["status"], "REPLAY")

    def test_same_uid_ledger_rollback_restores_replayable_old_image(self):
        self.fixture.executable = self.testing
        path = self.fixture.root / ".alpha_m1_native_gate/nonce-ledger/nonce-ledger.json"
        prior = path.read_bytes()
        first, data = self.fixture.reserve()
        self.assertEqual((first.returncode, data["nonce_state"]), (0, "RESERVED"))
        path.write_bytes(prior)  # synthetic rollback, intentionally demonstrates the same-UID limit
        second, replay = self.fixture.reserve()
        self.assertEqual((second.returncode, replay["nonce_state"]), (0, "RESERVED"))

    def test_concurrent_verify_only_is_not_nonce_consumption(self):
        command = [str(self.production), "--verify-only", str(self.fixture.root)]
        children = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for _ in range(2)]
        results = [json.loads(child.communicate(timeout=10)[0]) for child in children]
        self.assertEqual([item["status"] for item in results], ["VERIFIED", "VERIFIED"])
        self.assertTrue(all(item["nonce_state"] == "AVAILABLE" and not item["nonce_consumed"] for item in results))

    def test_fault_injected_persistence_never_verifies(self):
        self.fixture.executable = self.testing
        for fault in ("enospc-write", "eio-file-fsync", "rename", "eio-dir-fsync", "stale-next"):
            with self.subTest(fault=fault):
                # Give each failure an independent nonce ledger fixture.
                temp = tempfile.TemporaryDirectory(prefix="alpha-m1-session60-fault-")
                try:
                    fixture = Fixture(Path(temp.name), self.testing)
                    env = dict(os.environ, M1_SESSION61_FAULT=fault)
                    p, r = fixture.reserve(env)
                    self.assertNotEqual(p.returncode, 0); self.assertNotEqual(r["status"], "RESERVED")
                    if fault == "eio-dir-fsync":
                        self.assertEqual(r["nonce_state"], "UNKNOWN_AFTER_FAILURE")
                finally: temp.cleanup()

    def test_short_write_and_eintr_are_handled(self):
        self.fixture.executable = self.testing
        for fault in ("short-write", "eintr-write"):
            with self.subTest(fault=fault):
                temp = tempfile.TemporaryDirectory(prefix="alpha-m1-session60-write-")
                try:
                    fixture = Fixture(Path(temp.name), self.testing)
                    p, r = fixture.reserve(dict(os.environ, M1_SESSION61_FAULT=fault))
                    self.assertEqual((p.returncode, r["status"]), (0, "RESERVED"))
                    self.assertEqual(r["nonce_state"], "RESERVED")
                finally: temp.cleanup()

    def test_concurrent_synthetic_consumption_has_single_winner(self):
        self.fixture.executable = self.testing
        command = [str(self.testing), "--test-reserve-nonce", str(self.fixture.root)]
        children = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for _ in range(2)]
        outputs = [child.communicate(timeout=10) + (child.returncode,) for child in children]
        statuses = [json.loads(row[0]).get("status") for row in outputs]
        self.assertEqual(statuses.count("RESERVED"), 1)
        self.assertEqual(statuses.count("REPLAY"), 1)

    def test_many_concurrent_reservations_have_one_winner(self):
        self.fixture.executable = self.testing
        command = [str(self.testing), "--test-reserve-nonce", str(self.fixture.root)]
        children = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for _ in range(8)]
        rows = [json.loads(child.communicate(timeout=20)[0]) for child in children]
        self.assertEqual(sum(row["status"] == "RESERVED" for row in rows), 1)
        self.assertTrue(all(row["status"] in {"RESERVED", "REPLAY", "INCONCLUSIVE"} for row in rows))
        self.assertEqual(sum(row["status"] in {"REPLAY", "INCONCLUSIVE"} for row in rows), 7)

    def test_crash_recovery_is_conservative_at_each_persistence_boundary(self):
        self.fixture.executable = self.testing
        for fault, expected in (("crash-before-stage-create", "AVAILABLE"),
                                ("crash-after-stage-create", "INCONCLUSIVE"),
                                ("crash-after-stage-write", "INCONCLUSIVE"),
                                ("crash-after-file-fsync", "INCONCLUSIVE"),
                                ("crash-after-rename", "REPLAY"),
                                ("crash-after-dir-fsync", "REPLAY")):
            with self.subTest(fault=fault):
                temp = tempfile.TemporaryDirectory(prefix="alpha-m1-session61-crash-")
                try:
                    fixture = Fixture(Path(temp.name), self.testing)
                    env = dict(os.environ, M1_SESSION61_FAULT=fault)
                    command = [str(self.testing), "--test-reserve-nonce", str(fixture.root)]
                    crashed = subprocess.run(command, capture_output=True, text=True, env=env)
                    self.assertEqual(crashed.returncode, 86)
                    check, result = fixture.result()
                    if expected == "AVAILABLE":
                        self.assertEqual((check.returncode, result["nonce_state"]), (0, "AVAILABLE"))
                    elif expected == "REPLAY":
                        self.assertNotEqual(check.returncode, 0); self.assertEqual(result["status"], "REPLAY")
                    else:
                        self.assertNotEqual(check.returncode, 0)
                        self.assertEqual((result["status"], result["nonce_state"]), ("INCONCLUSIVE", "INCONCLUSIVE"))
                finally: temp.cleanup()

    def test_arbitrary_execution_option_rejected(self):
        for option in ("--execute", "--test-handoff"):
            proc = subprocess.run([str(self.production), option, str(self.fixture.root)], capture_output=True, text=True)
            self.assertNotEqual(proc.returncode, 0)
            self.assertFalse(json.loads(proc.stdout)["python_started"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
