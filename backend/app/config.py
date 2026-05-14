from __future__ import annotations

import os
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from dotenv import load_dotenv

BACKEND_ROOT = Path(__file__).resolve().parent.parent
PROJECT_ROOT = BACKEND_ROOT.parent

# Load variables from backend/.env when present.
load_dotenv(BACKEND_ROOT / ".env")


@dataclass(frozen=True)
class Settings:
    service_name: str
    engine_path: Path
    engine_timeout_seconds: int
    database_url: str
    runs_dir: Path
    reports_dir: Path
    backend_root: Path
    project_root: Path
    cors_origins: list[str]


def _resolve_from_backend_root(path_value: str) -> Path:
    candidate = Path(path_value)
    if candidate.is_absolute():
        return candidate.resolve()
    return (BACKEND_ROOT / candidate).resolve()


def _default_engine_path() -> Path:
    candidates = [
        PROJECT_ROOT / "build-linux" / "silicon_patient",
        PROJECT_ROOT / "build-cuda" / "silicon_patient.exe",
        PROJECT_ROOT / "build-cuda" / "silicon_patient",
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()

    return _resolve_from_backend_root("../build-cuda/silicon_patient.exe")


@lru_cache(maxsize=1)
def get_settings() -> Settings:
    timeout_raw = os.getenv("SPP_ENGINE_TIMEOUT_SECONDS", "3600")
    database_url = os.getenv("SPP_DATABASE_URL", "sqlite:///./silicon_patient.db")
    engine_path_raw = os.getenv("SPP_ENGINE_PATH")

    try:
        engine_timeout_seconds = int(timeout_raw)
    except ValueError:
        engine_timeout_seconds = 900

    runs_dir = BACKEND_ROOT / "runs"
    reports_dir = BACKEND_ROOT / "reports"

    return Settings(
        service_name="Silicon Patient Backend",
        engine_path=_resolve_from_backend_root(engine_path_raw) if engine_path_raw else _default_engine_path(),
        engine_timeout_seconds=max(1, engine_timeout_seconds),
        database_url=database_url,
        runs_dir=runs_dir,
        reports_dir=reports_dir,
        backend_root=BACKEND_ROOT,
        project_root=PROJECT_ROOT,
        cors_origins=[
            "http://localhost:5174",
            "http://127.0.0.1:5174",
            "http://localhost:5173",
            "http://127.0.0.1:5173",
            "http://localhost:4173",
            "http://127.0.0.1:4173",
            "http://localhost:3000",
            "http://127.0.0.1:3000",
        ],
    )
