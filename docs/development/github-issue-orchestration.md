# GitHub Issues to Monad Supervisor: version 2

## Scope and trust model

This one-shot workflow fetches one public GitHub issue from the fixed repository
`Alessandro83IT/monad`, stores a bounded snapshot, and passes it to the offline
adapter only after local issue authorization. It uses HTTPS GET only; it sends
no credentials and cannot write to GitHub. It does not launch Codex, install
software, or start a persistent service. GitHub content is untrusted even when
the repository and issue identity are correct.

```text
GitHub GET -> bounded snapshot -> explicit local issue authorization
                                  -> strict adapter -> proposed plan
                                                    -> review
                                                    -> supervisor authorize-plan
                                                    -> manual supervisor run
```

Issue authorization and plan authorization are separate local approvals. The
issue hash binds approval to exact body bytes, repository, and issue number.
The Supervisor's `authorize-plan` binds a selected ordered set of milestones
to the complete plan file digest. Neither approval is a signature or defense
against another process running as the same user. An unchanged issue approval
does not expire and can be reused; operators should fetch and review current
issue content before approval.

## Acquisition and formats

The fetcher calls only
`https://api.github.com/repos/Alessandro83IT/monad/issues/{number}` with GET,
using Python's standard library. It has a 10-second timeout, a 300,000-byte
response cap, and a 262,144-byte UTF-8 body cap. It checks content type, JSON
syntax and duplicate keys, repository URL, issue number, state, body, and pull
request marker. It handles HTTP errors, rate limits, timeouts, and network
failures without printing client exception details. It does not retry. The
unauthenticated public API can be rate limited. Snapshots and outputs are
created exclusively with mode 0600; existing paths are never overwritten.

The snapshot has the offline adapter's strict format:

```json
{"repository":{"full_name":"Alessandro83IT/monad"},"issue":{
  "number":17,"state":"open","body":"{...}","pull_request":null}}
```

The issue body must be JSON in the existing Supervisor plan format
`{"schema":1,"milestones":[...]}` and exactly one milestone is supported.
The canonical Supervisor schema validator enforces milestone fields, Python
unittest argv shape, limits, dependencies, success/stop criteria, and paths.
The adapter additionally excludes protected administrative/evidence paths.
Closed issues and pull requests are rejected.

The local issue authorization file contains exactly:

```json
{"schema":1,"repository":"Alessandro83IT/monad","issue_number":17,
 "body_sha256":"<64 lowercase hex characters>"}
```

It must be a regular non-symlink file owned by the current user with mode 0600
or stricter. `authorize-issue` creates it exclusively after the operator has
reviewed the snapshot. Editing the body invalidates it. The local file is a
trust input, not a cryptographic signature.

## Commands

```sh
python3 tools/codex-supervisor/github_orchestrator.py fetch \
  --issue 17 --snapshot /tmp/monad-issue-17.json
# Review repository, issue number, state, and body before local approval.
python3 tools/codex-supervisor/github_orchestrator.py authorize-issue \
  --snapshot /tmp/monad-issue-17.json \
  --authorization-output /tmp/monad-issue-17.authorization.json
python3 tools/codex-supervisor/github_orchestrator.py verify \
  --snapshot /tmp/monad-issue-17.json \
  --authorization /tmp/monad-issue-17.authorization.json
python3 tools/codex-supervisor/github_orchestrator.py prepare \
  --snapshot /tmp/monad-issue-17.json \
  --authorization /tmp/monad-issue-17.authorization.json \
  --plan-output /tmp/monad-issue-17.plan.json
```

`verify` prints a plan only after validation. `prepare` writes an exclusive
0600 proposed plan and does not authorize execution. Review that plan, then
select it with the Supervisor's `--plan-file` and separately run `plan` and
`authorize-plan`. A later manual `run` is outside this orchestrator and is
never started automatically. The offline adapter remains available directly
with its required `--dry-run` option.

## Limits and risks

The issue content enters only the milestone prompt after validation. Review for
attempts to access credentials, expand scope, disable controls, invoke nested
Codex, alter administrative paths, or run unapproved tests. Schema validation
constrains command shape but cannot make instructions trustworthy. HTTPS and
repository identity checks do not guarantee issue authorship or content safety.

Synthetic tests cover body tampering, repository/number mismatch, closed issues,
pull requests, unexpected fields, duplicate JSON keys, boolean issue numbers,
insecure authorization, arbitrary commands, protected paths, multiple
milestones, timeout/network errors, rate limiting, unexpected content type,
oversized responses, malformed responses, and issue identity mismatch.

The Supervisor is a development control, not a boundary against a malicious
same-user process or hostile kernel. Git snapshots can miss ignored/transient
changes. Issue authorization is unsigned, can be reused for unchanged content,
and has no expiration. No webhook, retries, milestone chaining, remote write,
persistent service, or automatic execution is supported. Alpha M1 remains
BLOCKED. Future autonomous execution needs separate review of freshness,
approval custody, plan review, machine/account, containment, limits, recovery,
and stop procedures.
