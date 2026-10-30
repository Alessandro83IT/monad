#!/usr/bin/env python3
"""One-shot, read-only GitHub issue acquisition and local approval workflow.

The network boundary fetches one public issue from the fixed GitHub API host.
It never sends credentials and never performs remote writes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import sys
import urllib.error
import urllib.request

import github_issue_adapter as adapter

API = "https://api.github.com/repos/Alessandro83IT/monad/issues/"
MAX_RESPONSE = 300_000
TIMEOUT_SECONDS = 10
ISSUE_FIELDS = {"number", "state", "body", "pull_request"}


class FetchError(ValueError):
    pass


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise FetchError("GitHub response contains duplicate JSON keys")
        result[key] = value
    return result


def fetch_issue(number: int, *, opener=None, timeout: int = TIMEOUT_SECONDS) -> dict:
    if isinstance(number, bool) or not isinstance(number, int) or not 1 <= number <= 2**31 - 1:
        raise FetchError("issue number must be a positive bounded integer")
    url = API + str(number)
    request = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "Monad-GitHub-Issue-Orchestrator/1",
    }, method="GET")
    open_url = opener or urllib.request.urlopen
    try:
        response = open_url(request, timeout=timeout)
        with response:
            status = getattr(response, "status", response.getcode())
            if status != 200:
                if status == 403 and response.headers.get("X-RateLimit-Remaining") == "0":
                    raise FetchError("GitHub API rate limit reached")
                raise FetchError(f"GitHub API returned HTTP {status}")
            data = response.read(MAX_RESPONSE + 1)
            if len(data) > MAX_RESPONSE:
                raise FetchError("GitHub API response exceeds size limit")
            content_type = response.headers.get("Content-Type", "").split(";", 1)[0].strip().lower()
            if content_type != "application/json":
                raise FetchError("GitHub API returned an unexpected content type")
    except FetchError:
        raise
    except urllib.error.HTTPError as exc:
        if exc.code == 403 and exc.headers.get("X-RateLimit-Remaining") == "0":
            raise FetchError("GitHub API rate limit reached") from None
        raise FetchError(f"GitHub API returned HTTP {exc.code}") from None
    except (urllib.error.URLError, TimeoutError, socket.timeout, OSError) as exc:
        # Deliberately omit exception text; URL/client errors can contain sensitive data.
        label = "timeout" if isinstance(exc, (TimeoutError, socket.timeout)) else "network error"
        raise FetchError(f"GitHub API {label}") from None
    try:
        remote = json.loads(data.decode("utf-8"), object_pairs_hook=_unique,
                            parse_constant=lambda _: (_ for _ in ()).throw(FetchError("invalid JSON constant")))
    except (UnicodeError, json.JSONDecodeError):
        raise FetchError("GitHub API returned invalid JSON") from None
    if not isinstance(remote, dict):
        raise FetchError("GitHub API response must be an object")
    if remote.get("repository_url") != "https://api.github.com/repos/Alessandro83IT/monad":
        raise FetchError("GitHub response repository identity mismatch")
    if remote.get("number") != number or isinstance(remote.get("number"), bool):
        raise FetchError("GitHub response issue number mismatch")
    if not isinstance(remote.get("state"), str) or remote["state"] not in {"open", "closed"}:
        raise FetchError("GitHub response issue state is invalid")
    body = remote.get("body")
    if not isinstance(body, str) or not body or len(body.encode("utf-8")) > 262144:
        raise FetchError("GitHub issue body missing or exceeds size limit")
    if remote.get("pull_request") is not None and not isinstance(remote["pull_request"], dict):
        raise FetchError("GitHub pull request marker is invalid")
    issue = {"number": number, "state": remote["state"], "body": body,
             "pull_request": remote.get("pull_request")}
    return {"repository": {"full_name": adapter.ALLOWED_REPOSITORY}, "issue": issue}


def atomic_snapshot(path: Path, snapshot: dict) -> None:
    # Refuse replacement: retained snapshots are review/evidence inputs.
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as stream:
        json.dump(snapshot, stream, sort_keys=True, ensure_ascii=False, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def authorize(snapshot_path: Path, authorization_path: Path) -> None:
    snapshot = adapter.strict_json(snapshot_path)
    if not isinstance(snapshot, dict) or set(snapshot) != adapter.SNAPSHOT_FIELDS:
        raise ValueError("snapshot schema/fields invalid")
    repo, issue = snapshot["repository"], snapshot["issue"]
    if (not isinstance(repo, dict) or set(repo) != adapter.REPOSITORY_FIELDS or
            not isinstance(repo.get("full_name"), str) or
            repo["full_name"].casefold() != adapter.ALLOWED_REPOSITORY.casefold()):
        raise ValueError("repository is not allowed")
    if not isinstance(issue, dict) or set(issue) != ISSUE_FIELDS or issue.get("state") != "open" or issue.get("pull_request") is not None:
        raise ValueError("only a valid open issue can receive local authorization")
    body, number = issue.get("body"), issue.get("number")
    if (not isinstance(body, str) or not body or len(body.encode("utf-8")) > 262144 or
            isinstance(number, bool) or not isinstance(number, int) or number <= 0):
        raise ValueError("issue snapshot is invalid")
    value = {"schema": 1, "repository": adapter.ALLOWED_REPOSITORY, "issue_number": number,
             "body_sha256": hashlib.sha256(body.encode("utf-8")).hexdigest()}
    atomic_snapshot(authorization_path, value)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    fetch = sub.add_parser("fetch", help="fetch and retain one public issue snapshot (GET only)")
    fetch.add_argument("--issue", type=int, required=True)
    fetch.add_argument("--snapshot", type=Path, required=True)
    verify = sub.add_parser("verify", help="validate snapshot and local issue authorization; print plan")
    verify.add_argument("--snapshot", type=Path, required=True)
    verify.add_argument("--authorization", type=Path, required=True)
    prepare = sub.add_parser("prepare", help="write a verified proposed plan without authorizing its execution")
    prepare.add_argument("--snapshot", type=Path, required=True)
    prepare.add_argument("--authorization", type=Path, required=True)
    prepare.add_argument("--plan-output", type=Path, required=True)
    approve = sub.add_parser("authorize-issue", help="explicitly bind local approval to a reviewed snapshot")
    approve.add_argument("--snapshot", type=Path, required=True)
    approve.add_argument("--authorization-output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.action == "fetch":
            atomic_snapshot(args.snapshot, fetch_issue(args.issue))
            print(f"SNAPSHOT_WRITTEN issue={args.issue} repository={adapter.ALLOWED_REPOSITORY}")
        elif args.action == "authorize-issue":
            authorize(args.snapshot, args.authorization_output)
            print("LOCAL_ISSUE_AUTHORIZATION_WRITTEN; inspect and retain the approval file")
        else:
            plan = adapter.adapt(args.snapshot, args.authorization)
            encoded = json.dumps(plan, sort_keys=True, indent=2) + "\n"
            if args.action == "verify":
                sys.stdout.write(encoded)
            else:
                flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
                fd = os.open(args.plan_output, flags, 0o600)
                with os.fdopen(fd, "w", encoding="utf-8") as stream:
                    stream.write(encoded); stream.flush(); os.fsync(stream.fileno())
                print("PROPOSED_PLAN_WRITTEN; review it, then separately use supervisor authorize-plan")
        return 0
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print(f"REJECTED: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
