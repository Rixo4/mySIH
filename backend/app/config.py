from __future__ import annotations

import os
import subprocess
from functools import lru_cache
from pathlib import Path

from dotenv import load_dotenv
from pydantic import AliasChoices, Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

BACKEND_ROOT = Path(__file__).resolve().parent.parent
PROJECT_ROOT = BACKEND_ROOT.parent

# Load variables from backend/.env when present.
load_dotenv(BACKEND_ROOT / ".env")


def _resolve_from_backend_root(path_value: str | Path) -> Path:
    candidate = Path(path_value)
    if candidate.is_absolute():
        return candidate.resolve()
    return (BACKEND_ROOT / candidate).resolve()


def _default_engine_path() -> Path:
    candidates = [
        PROJECT_ROOT / "build-validate" / "silicon_patient",
        PROJECT_ROOT / "build-linux" / "silicon_patient",
        PROJECT_ROOT / "build-cuda" / "silicon_patient.exe",
        PROJECT_ROOT / "build-cuda" / "silicon_patient",
    ]

    for candidate in candidates:
        if not candidate.exists():
            continue

        if not os.access(candidate, os.X_OK):
            continue

        if os.name != "nt":
            try:
                ldd_result = subprocess.run(["ldd", str(candidate)], capture_output=True, text=True, check=False)
            except OSError:
                return candidate.resolve()

            combined_output = f"{ldd_result.stdout}\n{ldd_result.stderr}"
            if "not found" in combined_output:
                continue

        return candidate.resolve()

    return _resolve_from_backend_root("../build-cuda/silicon_patient.exe")


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=str(BACKEND_ROOT / ".env"), extra="ignore")

    service_name: str = "Silicon Patient Backend"
    database_url: str = Field(
        default="sqlite:///./silicon_patient.db",
        validation_alias=AliasChoices("DATABASE_URL", "SPP_DATABASE_URL"),
    )
    redis_url: str = Field(
        default="redis://localhost:6379/0",
        validation_alias=AliasChoices("REDIS_URL", "SPP_REDIS_URL"),
    )
    engine_path: Path = Field(
        default_factory=_default_engine_path,
        validation_alias=AliasChoices("ENGINE_PATH", "SPP_ENGINE_PATH"),
    )
    log_level: str = Field(
        default="INFO",
        validation_alias=AliasChoices("LOG_LEVEL", "SPP_LOG_LEVEL"),
    )
    max_concurrent_simulations: int = Field(
        default=2,
        validation_alias=AliasChoices("MAX_CONCURRENT_SIMULATIONS", "SPP_MAX_CONCURRENT_SIMULATIONS"),
    )
    simulation_timeout_seconds: int = Field(
        default=3600,
        validation_alias=AliasChoices("SIMULATION_TIMEOUT_SECONDS", "SPP_ENGINE_TIMEOUT_SECONDS"),
    )
    runs_dir: Path = Field(default=BACKEND_ROOT / "runs")
    reports_dir: Path = Field(default=BACKEND_ROOT / "reports")
    artifacts_dir: Path = Field(default=BACKEND_ROOT / "artifacts")
    cors_origins: list[str] = Field(default_factory=list)

    @field_validator("engine_path", mode="before")
    @classmethod
    def _validate_engine_path(cls, value: Path | str | None) -> Path:
        if value is None:
            return _default_engine_path()
        return _resolve_from_backend_root(value)

    @field_validator("runs_dir", "reports_dir", "artifacts_dir", mode="before")
    @classmethod
    def _validate_dirs(cls, value: Path | str) -> Path:
        return _resolve_from_backend_root(value)

    @field_validator("cors_origins", mode="before")
    @classmethod
    def _validate_cors_origins(cls, value: list[str] | str | None) -> list[str]:
        if value is None or value == []:
            value = os.getenv("ALLOWED_ORIGINS", "http://localhost:5173")
        if isinstance(value, str):
            return [origin.strip() for origin in value.split(",") if origin.strip()]
        return [str(origin).strip() for origin in value if str(origin).strip()]

    @field_validator("database_url", mode="before")
    @classmethod
    def _validate_database_url(cls, value: str) -> str:
        if not isinstance(value, str):
            return value
        normalized = value.replace("postgresql+psycopg2://", "postgresql+psycopg://")
        if normalized.startswith("postgresql://"):
            normalized = normalized.replace("postgresql://", "postgresql+psycopg://", 1)
        return normalized

    @property
    def engine_timeout_seconds(self) -> int:
        return self.simulation_timeout_seconds


@lru_cache(maxsize=1)
def get_settings() -> Settings:
    return Settings()
