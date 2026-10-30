#!/usr/bin/env python3
"""Offline, dry-run adapter from a GitHub issue snapshot to a supervisor plan."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

import supervisor


AUTH_FIELDS = {"schema", "repository", "issue_number", "body_sha256"}
SNAPSHOT_FIELDS = {"repository", "issue"}
ISSUE_FIELDS = {"number", "state", "body", "pull_request"}
REPOSITORY_FIELDS = {"full_name"}
REPO_RE = re.compile(r"[A-Za-z0-9_.-]{1,100}/[A-Za-z0-9_.-]{1,100}\Z")
ID_RE = re.compile(r"[a-z0-9][a-z0-9-]{0,63}\Z")
FORBIDDEN_PATHS = ("qualification/", "docs/qualification/", "scripts/", ".codex/runs/",
                   ".codex/autonomous-mandate.md", ".codex-supervisor/")
ALLOWED_REPOSITORY = "Alessandro83IT/monad"


def strict_json(path: Path, max_bytes: int = 300000) -> object:
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError("duplicate JSON object key")
            out[key] = value
        return out

    if path.stat().st_size > max_bytes:
        raise ValueError("JSON input exceeds size limit")
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError("invalid JSON constant")))


def validate_authorization(path: Path, repository: str, number: int, body: str) -> None:
    if path.is_symlink() or not path.is_file():
        raise ValueError("authorization must be a regular, non-symlink local file")
    st = path.stat()
    if st.st_uid != os.getuid() or stat.S_IMODE(st.st_mode) & 0o077:
        raise ValueError("authorization file must be owned by this user and mode 0600 or stricter")
    auth = strict_json(path)
    if (not isinstance(auth, dict) or set(auth) != AUTH_FIELDS or
            isinstance(auth.get("schema"), bool) or auth.get("schema") != 1):
        raise ValueError("authorization schema/fields invalid")
    if (auth.get("repository") != repository or auth.get("issue_number") != number or
            auth.get("body_sha256") != hashlib.sha256(body.encode("utf-8")).hexdigest()):
        raise ValueError("local authorization does not bind this issue snapshot")


def adapt(snapshot_path: Path, authorization_path: Path) -> dict:
    snapshot = strict_json(snapshot_path)
    if not isinstance(snapshot, dict) or set(snapshot) != SNAPSHOT_FIELDS:
        raise ValueError("snapshot schema/fields invalid")
    repo = snapshot["repository"]
    issue = snapshot["issue"]
    if not isinstance(repo, dict) or set(repo) != REPOSITORY_FIELDS:
        raise ValueError("repository object invalid")
    full_name = repo["full_name"]
    if not isinstance(full_name, str) or not REPO_RE.fullmatch(full_name):
        raise ValueError("repository name invalid")
    if full_name.casefold() != ALLOWED_REPOSITORY.casefold():
        raise ValueError("repository is not allowed")
    if not isinstance(issue, dict) or set(issue) != ISSUE_FIELDS:
        raise ValueError("issue object invalid or unexpected fields present")
    number = issue["number"]
    body = issue["body"]
    if isinstance(number, bool) or not isinstance(number, int) or number <= 0:
        raise ValueError("issue number invalid")
    if issue["state"] != "open" or issue["pull_request"] is not None:
        raise ValueError("only open issues (not pull requests) are accepted")
    if not isinstance(body, str) or not body or len(body.encode("utf-8")) > 262144:
        raise ValueError("issue body must be 1..262144 UTF-8 bytes")
    validate_authorization(authorization_path, full_name, number, body)
    try:
        plan = json.loads(body, object_pairs_hook=lambda pairs: _unique_pairs(pairs))
    except (json.JSONDecodeError, UnicodeError) as exc:
        raise ValueError("issue body must contain a JSON supervisor plan") from exc
    if not isinstance(plan, dict) or set(plan) != {"schema", "milestones"} or isinstance(plan.get("schema"), bool) or plan.get("schema") != 1:
        raise ValueError("issue body plan schema/fields invalid")
    # Run the supervisor's canonical strict plan validation without modifying
    # the repository plan or supervisor state.
    with tempfile.TemporaryDirectory(prefix="monad-github-plan-") as temporary:
        candidate = Path(temporary) / "plan.json"
        candidate.write_text(json.dumps(plan), encoding="utf-8")
        original = supervisor.PLAN
        try:
            supervisor.PLAN = candidate
            milestones = supervisor.load_plan()
        finally:
            supervisor.PLAN = original
    # This adapter intentionally supports only a single, bounded milestone per issue.
    if len(milestones) != 1:
        raise ValueError("exactly one milestone per issue is supported")
    milestone = milestones[0]
    if not ID_RE.fullmatch(milestone["id"]):
        raise ValueError("milestone id invalid")
    for path in milestone["allowed_paths"]:
        normalized = path.rstrip("/") + "/"
        if any(normalized == forbidden or normalized.startswith(forbidden) or forbidden.startswith(normalized)
               for forbidden in FORBIDDEN_PATHS):
            raise ValueError("allowed path intersects a protected path")
    return {"schema": 1, "milestones": milestones}


def _unique_pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("duplicate JSON object key")
        result[key] = value
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", required=True,
                        help="required; adapter has no non-dry-run mode")
    parser.add_argument("--snapshot", required=True, type=Path, help="offline GitHub issue snapshot JSON")
    parser.add_argument("--authorization", required=True, type=Path, help="locally prepared issue-bound authorization JSON")
    args = parser.parse_args(argv)
    try:
        plan = adapt(args.snapshot, args.authorization)
        json.dump(plan, sys.stdout, sort_keys=True, indent=2)
        sys.stdout.write("\n")
        return 0
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print(f"REJECTED: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
