from __future__ import annotations

import json
import re
from datetime import datetime, timedelta, timezone
from pathlib import Path
from uuid import uuid4

RUN_ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]{8,80}$")
IST = timezone(timedelta(hours=5, minutes=30))


def utc_now() -> datetime:
    return datetime.now(IST)


def generate_run_id() -> str:
    ts = utc_now().strftime("%Y%m%dT%H%M%SIST")
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


def safe_datetime_diff_seconds(dt1: Any, dt2: Any) -> float:
    if dt1 is None or dt2 is None:
        return 0.0
    try:
        if isinstance(dt1, str):
            dt1 = datetime.fromisoformat(dt1)
        if isinstance(dt2, str):
            dt2 = datetime.fromisoformat(dt2)
        t1 = dt1.timestamp() if hasattr(dt1, "timestamp") else 0.0
        t2 = dt2.timestamp() if hasattr(dt2, "timestamp") else 0.0
        return max(0.0, float(t1 - t2))
    except Exception:
        return 0.0
