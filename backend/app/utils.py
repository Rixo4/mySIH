from __future__ import annotations

import json
import re
from datetime import datetime, timezone
from pathlib import Path
from uuid import uuid4

RUN_ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]{8,80}$")


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def generate_run_id() -> str:
    ts = utc_now().strftime("%Y%m%dT%H%M%SZ")
    return f"run_{ts}_{uuid4().hex[:10]}"


def sanitize_run_id(run_id: str) -> str:
    if not RUN_ID_PATTERN.fullmatch(run_id):
        raise ValueError("Invalid run_id format")
    return run_id


def ensure_directory(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def write_json_file(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, ensure_ascii=True), encoding="utf-8")


def write_text_file(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def loads_or_default(raw: str | None, default: object) -> object:
    if not raw:
        return default
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return default
