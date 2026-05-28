from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .config import get_settings
from .utils import ensure_directory


@dataclass(frozen=True)
class ArtifactRef:
    run_id: str
    name: str
    path: Path


class ArtifactStore:
    def save_json(self, run_id: str, name: str, payload: dict[str, Any]) -> ArtifactRef:
        raise NotImplementedError

    def save_text(self, run_id: str, name: str, payload: str) -> ArtifactRef:
        raise NotImplementedError

    def get_path(self, run_id: str, name: str) -> Path:
        raise NotImplementedError


class FilesystemArtifactStore(ArtifactStore):
    def __init__(self, root: Path | None = None) -> None:
        settings = get_settings()
        self._root = root or settings.artifacts_dir
        ensure_directory(self._root)

    def _run_dir(self, run_id: str) -> Path:
        run_dir = self._root / run_id
        ensure_directory(run_dir)
        return run_dir

    def get_path(self, run_id: str, name: str) -> Path:
        return self._run_dir(run_id) / name

    def save_json(self, run_id: str, name: str, payload: dict[str, Any]) -> ArtifactRef:
        path = self.get_path(run_id, name)
        path.write_text(json.dumps(payload, indent=2, sort_keys=True, default=str), encoding="utf-8")
        return ArtifactRef(run_id=run_id, name=name, path=path)

    def save_text(self, run_id: str, name: str, payload: str) -> ArtifactRef:
        path = self.get_path(run_id, name)
        path.write_text(payload, encoding="utf-8")
        return ArtifactRef(run_id=run_id, name=name, path=path)


_default_artifact_store: FilesystemArtifactStore | None = None


def get_artifact_store() -> FilesystemArtifactStore:
    global _default_artifact_store
    if _default_artifact_store is None:
        _default_artifact_store = FilesystemArtifactStore()
    return _default_artifact_store
