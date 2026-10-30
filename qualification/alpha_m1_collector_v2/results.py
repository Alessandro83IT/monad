"""Internal adapter result model; free text is redacted at orchestration."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .contracts import ResultStatus


@dataclass(frozen=True)
class ModuleResult:
    status: ResultStatus
    records: int = 0
    errors: tuple[str, ...] = ()
    anomalies: tuple[str, ...] = ()
    summary: dict[str, Any] | None = None
    interrupted: bool = False
