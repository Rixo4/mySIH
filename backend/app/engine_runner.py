from __future__ import annotations

import os
import subprocess
import time
from dataclasses import dataclass

from .config import get_settings

MODE_TO_FLAG = {
    "simulate": "--simulate",
    "dose-eval": "--dose-eval",
    "validate": "--validate",
}


@dataclass
class EngineResult:
    status: str
    mode: str
    stdout: str
    stderr: str
    error: str | None
    return_code: int | None
    duration_seconds: float
    command: list[str]


def run_engine(mode: str, env_overrides: dict[str, str] | None = None, drug_config_path: str | None = None, cwd: str | None = None) -> EngineResult:
    settings = get_settings()

    if mode not in MODE_TO_FLAG:
        raise ValueError(f"Unsupported mode: {mode}")

    command = [str(settings.engine_path), MODE_TO_FLAG[mode]]
    if drug_config_path:
        command.extend(["--drug-config", str(drug_config_path)])
    start = time.monotonic()

    if not settings.engine_path.exists():
        return EngineResult(
            status="failed",
            mode=mode,
            stdout="",
            stderr="",
            error="Engine executable not found",
            return_code=None,
            duration_seconds=round(time.monotonic() - start, 3),
            command=command,
        )

    env = os.environ.copy()
    if env_overrides:
        env.update({k: str(v) for k, v in env_overrides.items()})

    try:
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=settings.engine_timeout_seconds,
            env=env,
            cwd=(str(cwd) if cwd is not None else str(settings.project_root)),
            shell=False,
            check=False,
        )
    except FileNotFoundError:
        return EngineResult(
            status="failed",
            mode=mode,
            stdout="",
            stderr="",
            error="Engine executable not found",
            return_code=None,
            duration_seconds=round(time.monotonic() - start, 3),
            command=command,
        )
    except subprocess.TimeoutExpired as exc:
        return EngineResult(
            status="failed",
            mode=mode,
            stdout=exc.stdout or "",
            stderr=exc.stderr or "",
            error="Engine execution timed out",
            return_code=None,
            duration_seconds=round(time.monotonic() - start, 3),
            command=command,
        )
    except OSError as exc:
        return EngineResult(
            status="failed",
            mode=mode,
            stdout="",
            stderr="",
            error=f"Engine execution failed: {exc}",
            return_code=None,
            duration_seconds=round(time.monotonic() - start, 3),
            command=command,
        )

    duration = round(time.monotonic() - start, 3)

    if completed.returncode != 0:
        return EngineResult(
            status="failed",
            mode=mode,
            stdout=completed.stdout or "",
            stderr=completed.stderr or "",
            error="Engine execution failed",
            return_code=completed.returncode,
            duration_seconds=duration,
            command=command,
        )

    return EngineResult(
        status="completed",
        mode=mode,
        stdout=completed.stdout or "",
        stderr=completed.stderr or "",
        error=None,
        return_code=completed.returncode,
        duration_seconds=duration,
        command=command,
    )
