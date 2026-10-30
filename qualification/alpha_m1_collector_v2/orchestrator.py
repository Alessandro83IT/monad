"""Initial, non-collecting orchestrator for the Alpha M1 v2 core candidate."""
from __future__ import annotations

from dataclasses import asdict
import hashlib
import json
import re
import threading
import time
import uuid
from itertools import islice
from typing import Any, Iterable

from .adapters import ADAPTERS, DiagnosticSource
from .authorization import AuthorizationBundle, AuthorizationVerifier
from .budget import AdapterBudget, Budget
from .contracts import (
    BudgetExhausted, BudgetLimits, DeadlineExceeded, Interrupted, InvalidRequest,
    MODULE_IDS, MINIMUM_PROFILE, MODULE_SCOPES, OPTIONAL_MODULES, ParseFailure,
    ResultStatus, validate_limits,
)
from .safe_fs import PartialWriteError, SafeDirectory, WriteResult
from .results import ModuleResult


SAFE_ERROR_CODES = frozenset({
    "COLLECTION_NOT_IMPLEMENTED", "INTERRUPTED", "INTERRUPTED_BEFORE_START",
    "PARTIAL_OUTPUT_PRESERVED", "OUTPUT_NOT_COMPLETE", "TRUNCATED",
    "ACCESS_DENIED", "ABSENT", "PARSE_ERROR", "TIMEOUT", "INCONCLUSIVE",
    "ERROR_LIST_TRUNCATED", "UNCLASSIFIED_ERROR_REDACTED",
})
SAFE_ANOMALY_CODES = frozenset({
    "NO_REAL_READS_PERFORMED", "INTERRUPTED", "PARTIAL_OUTPUT_PRESERVED",
    "OUTPUT_NOT_COMPLETE", "UNCLASSIFIED_ANOMALY_REDACTED",
})


def _safe_codes(values: Iterable[str], allowlist: frozenset[str]) -> list[str]:
    """Bound and redact adapter text; arbitrary exception/data strings never persist."""
    items = list(islice(iter(values), 17))
    result: list[str] = []
    redacted = "UNCLASSIFIED_ERROR_REDACTED" if "ERROR_LIST_TRUNCATED" in allowlist else "UNCLASSIFIED_ANOMALY_REDACTED"
    for value in items[:16]:
        result.append(value if isinstance(value, str) and value in allowlist else redacted)
    if len(items) > 16 and "ERROR_LIST_TRUNCATED" in allowlist:
        result.append("ERROR_LIST_TRUNCATED")
    return result


def _safe_summary(module_id: str, summary: dict[str, Any] | None) -> dict[str, Any] | None:
    """Whitelist each adapter's structured fields before serialization."""
    if summary is None:
        return {}
    if (isinstance(summary, dict) and summary.get("adapter") == "synthetic-placeholder"
            and summary.get("read") is False and set(summary) == {"adapter", "read"}):
        return {"adapter": "synthetic-placeholder", "read": False}
    if not isinstance(summary, dict):
        return None
    try:
        from .adapters import safe_adapter_summary
        return safe_adapter_summary(module_id, summary)
    except Exception:
        return None


def _placeholder_adapter(module_id: str, budget: AdapterBudget) -> ModuleResult:
    """No module in this phase reads the system, guest, or evidence corpus."""
    budget.check_deadline()
    return ModuleResult(
        ResultStatus.INCONCLUSIVE,
        errors=("COLLECTION_NOT_IMPLEMENTED",),
        anomalies=("NO_REAL_READS_PERFORMED",),
        summary={"adapter": "synthetic-placeholder", "read": False},
    )


def _status_for_exception(exc: BaseException) -> ResultStatus:
    if isinstance(exc, BudgetExhausted):
        return ResultStatus.TRUNCATED
    if isinstance(exc, DeadlineExceeded):
        return ResultStatus.TIMEOUT
    if isinstance(exc, PermissionError) or getattr(exc, "errno", None) in (13, 1):
        return ResultStatus.ACCESS_DENIED
    if isinstance(exc, FileNotFoundError) or getattr(exc, "errno", None) == 2:
        return ResultStatus.ABSENT
    if isinstance(exc, ParseFailure):
        return ResultStatus.PARSE_ERROR
    if isinstance(exc, TimeoutError):
        return ResultStatus.TIMEOUT
    if isinstance(exc, Interrupted):
        return ResultStatus.INCONCLUSIVE
    return ResultStatus.INCONCLUSIVE


def _json_bytes(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")


def _serialize_manifest(value: Any, maximum: int) -> bytes:
    try:
        raw = _json_bytes(value)
    except (TypeError, ValueError, OverflowError, RecursionError) as exc:
        raise ParseFailure("manifest serialization failed") from exc
    if len(raw) > maximum:
        raise BudgetExhausted("manifest exceeds its fixed maximum size")
    return raw


def verify_finalization(manifest_bytes: bytes, seal_bytes: bytes) -> ResultStatus:
    """Only a matching bounded seal makes the manifest's proposed status final."""
    try:
        manifest = json.loads(manifest_bytes)
        seal = json.loads(seal_bytes)
        expected_keys = {"schema_version", "acquisition_id", "path", "bytes", "sha256",
                         "final_status", "finalization_metrics"}
        if not isinstance(manifest, dict) or not isinstance(seal, dict) or set(seal) != expected_keys:
            return ResultStatus.INCONCLUSIVE
        if (manifest.get("completion") != ResultStatus.INCONCLUSIVE.value or
                manifest.get("finalization_state") != "REQUIRES_VALID_SEAL" or
                seal.get("schema_version") != 1 or seal.get("path") != "manifest.final.json" or
                seal.get("acquisition_id") != manifest.get("acquisition_id") or
                seal.get("bytes") != len(manifest_bytes) or
                seal.get("sha256") != hashlib.sha256(manifest_bytes).hexdigest() or
                seal.get("final_status") != manifest.get("proposed_completion")):
            return ResultStatus.INCONCLUSIVE
        status = ResultStatus(seal["final_status"])
        if status == ResultStatus.NOT_REQUESTED:
            return ResultStatus.INCONCLUSIVE
        return status
    except (TypeError, ValueError, KeyError, json.JSONDecodeError):
        return ResultStatus.INCONCLUSIVE


def _fallback_manifest(acquisition_id: str, candidate_digest: str, error_code: str) -> bytes:
    # Keep this serializer independent of adapter data and the main serializer.
    value = {
        "schema_version": 1,
        "candidate_id": "alpha-m1-collector-v2-core",
        "candidate_digest": candidate_digest,
        "acquisition_id": acquisition_id,
        "completion": ResultStatus.INCONCLUSIVE.value,
        "proposed_completion": ResultStatus.INCONCLUSIVE.value,
        "finalization_state": "REQUIRES_VALID_SEAL",
        "errors": [error_code],
        "anomalies": ["FINALIZATION_FALLBACK_MANIFEST"],
    }
    return (json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")


def _artifact_record(result: WriteResult, module_id: str) -> dict[str, Any]:
    return {
        "path": result.name,
        "module_id": module_id,
        "bytes": result.bytes_written,
        "sha256": result.sha256,
        "status": ResultStatus.COMPLETE.value,
    }


def _aggregate_status(results: dict[str, dict[str, Any]], selected: tuple[str, ...]) -> ResultStatus:
    """COMPLETE is possible only when every selected adapter completed fully."""
    for module_id in selected:
        value = results[module_id]["status"]
        if value != ResultStatus.COMPLETE.value:
            return ResultStatus.INCONCLUSIVE if value == ResultStatus.NOT_REQUESTED.value else ResultStatus(value)
    if not selected:
        return ResultStatus.INCONCLUSIVE
    return ResultStatus.COMPLETE


class CollectorOrchestrator:
    """Runs only built-in adapter code over the abstract source interface.

    No real system backend or external plugin loader is present. Both signed
    approvals and the exact target/candidate identity are required fail-closed.
    """

    def __init__(self, *, limits: BudgetLimits = BudgetLimits(),
                 authorization_verifier: AuthorizationVerifier | None = None,
                 candidate_digest: str = "", target_id: str = ""):
        self.limits = validate_limits(limits)
        self.authorization_verifier = authorization_verifier
        self.candidate_digest = candidate_digest
        self.target_id = target_id

    @staticmethod
    def _validate_selection(authorized_modules: Iterable[str], requested_modules: Iterable[str] | None
                            ) -> tuple[tuple[str, ...], tuple[str, ...]]:
        try:
            authorized = tuple(islice(iter(authorized_modules), len(MODULE_IDS) + 1))
        except TypeError as exc:
            raise InvalidRequest("authorized modules must be an iterable of fixed module IDs") from exc
        if len(authorized) > len(MODULE_IDS):
            raise InvalidRequest("authorization module list exceeds the fixed allowlist size")
        if (any(not isinstance(item, str) for item in authorized) or
                len(set(authorized)) != len(authorized) or any(item not in MODULE_IDS for item in authorized)):
            raise InvalidRequest("authorization contains duplicate or unknown module IDs")
        if requested_modules is None:
            if not set(MINIMUM_PROFILE).issubset(authorized):
                raise InvalidRequest("default profile requires every minimum module to be authorized")
            requested = MINIMUM_PROFILE
        else:
            try:
                requested = tuple(islice(iter(requested_modules), len(MODULE_IDS) + 1))
            except TypeError as exc:
                raise InvalidRequest("requested modules must be an iterable of fixed module IDs") from exc
            if len(requested) > len(MODULE_IDS):
                raise InvalidRequest("requested module list exceeds the fixed allowlist size")
        if (not requested or any(not isinstance(item, str) for item in requested) or
                len(set(requested)) != len(requested)):
            raise InvalidRequest("requested module list must be non-empty and unique")
        if any(item not in MODULE_IDS for item in requested):
            raise InvalidRequest("requested module is outside the fixed allowlist")
        unauthorized = set(requested) - set(authorized)
        if unauthorized:
            raise InvalidRequest("requested read set exceeds explicit authorized module set")
        return authorized, requested

    def run(self, output_parent: SafeDirectory, *, authorization: AuthorizationBundle | None,
            requested_modules: Iterable[str] | None = None, source: DiagnosticSource | None = None,
            acquisition_id: str | None = None,
            interrupt_event: threading.Event | None = None) -> tuple[SafeDirectory, dict[str, Any], ResultStatus]:
        if not isinstance(output_parent, SafeDirectory):
            raise InvalidRequest("output parent must be an already-open SafeDirectory")
        if self.authorization_verifier is None:
            raise InvalidRequest("authorization verifier is not configured; acquisition is fail-closed")
        if source is None:
            raise InvalidRequest("an abstract diagnostic source is required")
        signed_read_set = authorization.read_set.requested_read_set if isinstance(authorization, AuthorizationBundle) else ()
        authorized, selected = self._validate_selection(
            authorization.read_set.authorized_modules if isinstance(authorization, AuthorizationBundle) else (),
            signed_read_set if requested_modules is None else requested_modules,
        )
        finalization_reserve = self.limits.manifest_bytes + self.limits.seal_bytes
        minimum_running_output = 32 * 1024
        if (self.limits.bytes_persisted < finalization_reserve + minimum_running_output or
                self.limits.output_file_bytes < 1024 or
                self.limits.files < len(selected) + 3):
            raise InvalidRequest("output budget is below finalization reserve plus running-output floor")
        authorization_ids = self.authorization_verifier.verify_bundle(
            authorization, candidate_id="alpha-m1-collector-v2-core",
            candidate_digest=self.candidate_digest, target_id=self.target_id,
            requested_modules=selected, limits=self.limits,
        )
        acquisition_id = str(uuid.uuid4()) if acquisition_id is None else acquisition_id
        start_ns = time.monotonic_ns()
        # new_manifest validates that the identifier is safe before it becomes
        # an output name; no caller-provided pathname is accepted.
        from .contracts import new_manifest
        manifest = new_manifest(acquisition_id, selected, self.limits, start_ns)
        manifest["candidate_digest"] = self.candidate_digest
        manifest["scope_authorization"].update({
            "authorization_ids": authorization_ids,
            "authorized_modules": list(authorized),
            "target_id": self.target_id,
            "candidate_digest": self.candidate_digest,
            "decision": "APPROVE_READ_SET_AND_EXECUTION",
        })
        output = output_parent.create_child_exclusive(f"alpha-m1-v2-{acquisition_id}")
        budget = Budget(self.limits)
        manifest["started_monotonic_ns"] = budget.started_ns
        manifest["completion"] = ResultStatus.INCONCLUSIVE.value
        manifest["anomalies"].append("RUN_IN_PROGRESS")

        running_bytes = _json_bytes(manifest)
        try:
            running_result = output.write_new_file("manifest.running.json", running_bytes, budget=budget)
        except BaseException as exc:
            manifest["completion"] = ResultStatus.INCONCLUSIVE.value
            manifest["finalization_state"] = "INITIAL_OUTPUT_INCOMPLETE"
            manifest["errors"] = [{"module_id": "orchestrator", "message": "RUNNING_MANIFEST_WRITE_FAILED"}]
            manifest["anomalies"].append("INITIAL_OUTPUT_INCOMPLETE")
            manifest["artifacts"] = [{
                "path": getattr(exc, "name", "manifest.running.json"),
                "module_id": "orchestrator",
                "bytes": getattr(exc, "bytes_written", 0),
                "sha256": getattr(exc, "sha256", hashlib.sha256(b"").hexdigest()),
                "status": ResultStatus.INCONCLUSIVE.value,
            }]
            manifest["budget_usage_after_failure"] = budget.snapshot()
            manifest["byte_counts_after_failure"] = {
                "read": budget.bytes_read, "persisted": budget.bytes_persisted,
            }
            return output, manifest, ResultStatus.INCONCLUSIVE
        artifacts = [_artifact_record(running_result, "orchestrator")]
        selected_set = set(selected)
        module_results: dict[str, dict[str, Any]] = {}
        stopped = False

        for module_id in MODULE_IDS:
            if module_id not in selected_set:
                module_results[module_id] = {
                    "module_id": module_id,
                    "scope": MODULE_SCOPES[module_id],
                    "status": ResultStatus.NOT_REQUESTED.value,
                    "records": 0,
                    "errors": [],
                    "anomalies": [],
                    "artifact": None,
                }
                continue

            if stopped or (interrupt_event is not None and interrupt_event.is_set()):
                stopped = True
                result = ModuleResult(ResultStatus.INCONCLUSIVE,
                                      errors=("INTERRUPTED_BEFORE_START",),
                                      anomalies=("INTERRUPTED",))
            else:
                try:
                    budget.check_deadline()
                    # Registry is code-fixed. Unselected adapters are never
                    # imported or called; no configuration-driven plugin path exists.
                    adapter = ADAPTERS.get(module_id)
                    if adapter is None:
                        result = _placeholder_adapter(module_id, AdapterBudget(budget, module_id))
                    else:
                        result = adapter.collect(source, AdapterBudget(budget, module_id))
                    if result.interrupted:
                        stopped = True
                        result = ModuleResult(
                            ResultStatus.INCONCLUSIVE, records=result.records,
                            errors=(*result.errors, "INTERRUPTED"),
                            anomalies=(*result.anomalies, "INTERRUPTED"),
                            summary=result.summary, interrupted=True,
                        )
                except (KeyboardInterrupt, InterruptedError) as exc:
                    stopped = True
                    result = ModuleResult(ResultStatus.INCONCLUSIVE,
                                          errors=("INTERRUPTED",),
                                          anomalies=("INTERRUPTED",))
                except Exception as exc:
                    result = ModuleResult(_status_for_exception(exc),
                                          errors=(_status_for_exception(exc).value,))

            try:
                if (isinstance(result.records, bool) or not isinstance(result.records, int)
                        or result.records < 0):
                    raise ValueError("invalid module record count")
                if result.status == ResultStatus.COMPLETE:
                    if module_id not in ADAPTERS:
                        raise InvalidRequest("unimplemented module cannot return COMPLETE")
                safe_summary = _safe_summary(module_id, result.summary)
                if safe_summary is None:
                    result = ModuleResult(ResultStatus.PARSE_ERROR, records=0,
                                          errors=("PARSE_ERROR",), anomalies=result.anomalies)
                    safe_summary = {}
                body = {
                    "schema_version": 1,
                    "acquisition_id": acquisition_id,
                    "module_id": module_id,
                    "scope": MODULE_SCOPES[module_id],
                    "status": result.status.value,
                    "records": result.records,
                    "errors": _safe_codes(result.errors, SAFE_ERROR_CODES),
                    "anomalies": _safe_codes(result.anomalies, SAFE_ANOMALY_CODES),
                    "summary": safe_summary,
                }
                file_name = f"module-{module_id}.json"
                write_result = output.write_new_file(file_name, _json_bytes(body), budget=budget)
                artifact = _artifact_record(write_result, module_id)
                artifacts.append(artifact)
            except PartialWriteError as exc:
                if isinstance(exc.cause, KeyboardInterrupt):
                    stopped = True
                result = ModuleResult(_status_for_exception(exc.cause),
                                      records=result.records,
                                      errors=(*result.errors, "PARTIAL_OUTPUT_PRESERVED",
                                              _status_for_exception(exc.cause).value),
                                      anomalies=(*result.anomalies, "PARTIAL_OUTPUT_PRESERVED"))
                artifact = {"path": exc.name, "module_id": module_id,
                            "bytes": exc.bytes_written, "sha256": exc.sha256,
                            "status": result.status.value}
                artifacts.append(artifact)
            except Exception as exc:
                result = ModuleResult(_status_for_exception(exc), records=0,
                                      errors=(*result.errors, "OUTPUT_NOT_COMPLETE",
                                              _status_for_exception(exc).value),
                                      anomalies=(*result.anomalies, "OUTPUT_NOT_COMPLETE"))
                artifact = None

            module_results[module_id] = {
                "module_id": module_id,
                "scope": MODULE_SCOPES[module_id],
                "status": result.status.value,
                "records": result.records,
                "errors": _safe_codes(result.errors, SAFE_ERROR_CODES),
                "anomalies": _safe_codes(result.anomalies, SAFE_ANOMALY_CODES),
                "artifact": artifact,
            }

        status = _aggregate_status(module_results, selected)
        if stopped:
            status = ResultStatus.INCONCLUSIVE
        manifest["module_results"] = module_results
        proposed_status = status
        manifest["proposed_completion"] = proposed_status.value
        manifest["completion"] = ResultStatus.INCONCLUSIVE.value
        manifest["finalization_state"] = "REQUIRES_VALID_SEAL"
        manifest["artifacts"] = artifacts
        if stopped:
            manifest["anomalies"].append("INTERRUPTED; selected modules after the interruption were not started")
        manifest["errors"] = [
            {"module_id": key, "message": message}
            for key, value in module_results.items() for message in value["errors"]
        ]
        manifest["record_count"] = sum(value["records"] for value in module_results.values())
        manifest["budget_usage"] = budget.snapshot()
        manifest["budget_usage_scope"] = (
            "access/operation counters through adapter and running/module output; "
            "finalization file counts and persisted bytes are projected; finalization syscalls are excluded"
        )
        manifest["byte_counts"] = {"read": budget.bytes_read, "persisted": budget.bytes_persisted}
        manifest["duration_ns"] = max(0, time.monotonic_ns() - budget.started_ns)
        manifest["duration_scope"] = "through adapter completion; finalization fsync time is excluded"

        fallback_code: str | None = None
        try:
            final_bytes = _serialize_manifest(manifest, self.limits.manifest_bytes)
        except ParseFailure:
            fallback_code = "MANIFEST_SERIALIZATION_FAILED"
        except BudgetExhausted:
            fallback_code = "MANIFEST_SIZE_LIMIT_REACHED"
        if fallback_code is not None:
            final_bytes = _fallback_manifest(acquisition_id, self.candidate_digest, fallback_code)
            if len(final_bytes) > self.limits.manifest_bytes:
                return self._finalization_failure(output, manifest, budget, ResultStatus.TRUNCATED,
                                                  "MANIFEST_FALLBACK_TOO_LARGE")
            manifest = json.loads(final_bytes)
            proposed_status = ResultStatus.INCONCLUSIVE
            manifest.update({
                "budget_usage": budget.snapshot(),
                "budget_usage_scope": (
                    "pre-finalization access counters; finalization file counts and persisted bytes are projected; "
                    "finalization syscalls are excluded"
                ),
                "byte_counts": {"read": budget.bytes_read, "persisted": budget.bytes_persisted},
                "duration_ns": max(0, time.monotonic_ns() - budget.started_ns),
                "duration_scope": "through adapter completion; finalization fsync time is excluded",
            })

        try:
            # The final output channel spends only the reserve and has separate
            # hard maxima for the manifest and seal.
            seal_bytes = b""
            snapshot = budget.snapshot()
            manifest["budget_usage"]["files_attempted"] = snapshot["files_attempted"] + 2
            manifest["budget_usage"]["files_opened"] = snapshot["files_opened"] + 2
            converged = False
            for _ in range(12):
                final_bytes = _serialize_manifest(manifest, self.limits.manifest_bytes)
                seal_template = {
                    "schema_version": 1,
                    "acquisition_id": acquisition_id,
                    "path": "manifest.final.json",
                    "bytes": len(final_bytes),
                    "sha256": hashlib.sha256(final_bytes).hexdigest(),
                    "final_status": proposed_status.value,
                    "finalization_metrics": {
                        "manifest_bytes": len(final_bytes),
                        "seal_bytes": 0,
                        "reserved_bytes": self.limits.manifest_bytes + self.limits.seal_bytes,
                    },
                }
                for _seal_try in range(4):
                    seal_template["finalization_metrics"]["seal_bytes"] = len(_json_bytes(seal_template))
                    seal_bytes = _json_bytes(seal_template)
                    if seal_template["finalization_metrics"]["seal_bytes"] == len(seal_bytes):
                        break
                if len(seal_bytes) > self.limits.seal_bytes:
                    raise BudgetExhausted("seal exceeds its fixed size limit")
                projected_bytes = budget.bytes_persisted + len(final_bytes) + len(seal_bytes)
                manifest["byte_counts"]["persisted"] = projected_bytes
                manifest["budget_usage"]["bytes_persisted"] = projected_bytes
                manifest["budget_usage"]["finalization_bytes"] = len(final_bytes) + len(seal_bytes)
                updated = _serialize_manifest(manifest, self.limits.manifest_bytes)
                if updated == final_bytes:
                    converged = True
                    break
            if not converged:
                raise ParseFailure("manifest and seal projection did not stabilize")
            if (budget.bytes_persisted + len(final_bytes) + len(seal_bytes) > self.limits.bytes_persisted or
                    len(final_bytes) > self.limits.manifest_bytes or len(seal_bytes) > self.limits.seal_bytes):
                raise BudgetExhausted("finalization reserve is insufficient")

            final_result = output.write_finalization_file(
                "manifest.final.json", final_bytes, budget=budget, file_kind="manifest"
            )
            seal_template["bytes"] = final_result.bytes_written
            seal_template["sha256"] = final_result.sha256
            seal_template["finalization_metrics"]["manifest_bytes"] = final_result.bytes_written
            seal_bytes = _json_bytes(seal_template)
            output.write_finalization_file(
                "manifest.final.sha256.json", seal_bytes, budget=budget, file_kind="seal"
            )
            verified_status = verify_finalization(final_bytes, seal_bytes)
            if verified_status != proposed_status:
                raise ParseFailure("final manifest seal verification failed")
            if budget.bytes_persisted != projected_bytes:
                raise RuntimeError("persisted-byte projection differs from actual writes")
            return output, manifest, verified_status
        except KeyboardInterrupt:
            budget.note_interruption()
            return self._finalization_failure(output, manifest, budget, ResultStatus.INCONCLUSIVE,
                                              "FINALIZATION_INTERRUPTED")
        except BaseException as exc:
            failed_status = _status_for_exception(exc)
            code = "FINALIZATION_LIMIT_REACHED" if failed_status == ResultStatus.TRUNCATED else "FINALIZATION_FAILED"
            return self._finalization_failure(output, manifest, budget, failed_status, code)

    @staticmethod
    def _finalization_failure(output: SafeDirectory, manifest: dict[str, Any], budget: Budget,
                              status: ResultStatus, error_code: str
                              ) -> tuple[SafeDirectory, dict[str, Any], ResultStatus]:
        manifest["completion"] = ResultStatus.INCONCLUSIVE.value
        manifest["finalization_state"] = "INCOMPLETE"
        manifest["errors"] = [*manifest.get("errors", []), {"module_id": "finalization", "message": error_code}]
        manifest["anomalies"] = [*manifest.get("anomalies", []), "FINALIZATION_INCOMPLETE"]
        manifest["budget_usage_after_failure"] = budget.snapshot()
        manifest["byte_counts_after_failure"] = {
            "read": budget.bytes_read, "persisted": budget.bytes_persisted,
        }
        return output, manifest, ResultStatus.INCONCLUSIVE


def module_allowlist() -> tuple[str, ...]:
    return MODULE_IDS


def optional_modules() -> frozenset[str]:
    return OPTIONAL_MODULES
