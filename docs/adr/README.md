# Architecture decision records

This directory contains Athena PM's repository architecture decision records
(ADRs). ADR-001 is the first actual ADR in this repository. No earlier ADR
documents or implied ADR history are established by this convention.

## Naming and numbering

Assign sequential identifiers to actual documents. Filenames use a four-digit
number and a descriptive lowercase kebab-case suffix:

```text
0001-build-input-artifact-identity-content-addressed-store.md
0002-<decision-title>.md
0003-<decision-title>.md
```

Human-readable identifiers use at least three digits: ADR-001, ADR-002,
ADR-003. The filename and identifier refer to the same number. The examples
above do not establish decisions or reserve placeholder documents.

Each ADR MUST contain its identifier and title, status, context, decision,
consequences, and rejected alternatives. It MAY contain implementation
prerequisites, deferred work, and other supporting sections. Normative MUST,
MUST NOT, SHOULD, and MAY express requirements, prohibitions, recommendations,
and permitted choices respectively.

## Status and historical integrity

Allowed statuses are:

- **Proposed**: recorded for consideration, not yet accepted.
- **Accepted**: the project's adopted architectural decision.
- **Superseded**: replaced by an identified subsequent ADR.
- **Deprecated**: no longer applicable, without a replacement decision.

Accepted ADRs are immutable historical decisions. A material change MUST be
recorded in a new ADR that explicitly supersedes the old decision, rather than
silently rewriting its decision text. Lifecycle status and links to the
superseding ADR MAY be updated on the historical document and in this index;
such updates MUST preserve the original decision. Proposed records MAY be
revised before acceptance.

Add each actual ADR to the index. Do not create placeholder ADRs for decisions
that do not exist.

## Index

| Identifier | Title | Status |
| --- | --- | --- |
| [ADR-001](0001-build-input-artifact-identity-content-addressed-store.md) | Build Input Identity, Artifact Identity and Content-Addressed Store | Accepted |
