from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .config import get_settings
from .utils import ensure_directory

import boto3
from botocore.exceptions import BotoCoreError, ClientError


@dataclass(frozen=True)
class ArtifactRef:
    run_id: str
    name: str
    path: str


class ArtifactStore:
    def save_json(self, run_id: str, name: str, payload: dict[str, Any]) -> ArtifactRef:
        raise NotImplementedError

    def save_text(self, run_id: str, name: str, payload: str) -> ArtifactRef:
        raise NotImplementedError

    def get_path(self, run_id: str, name: str) -> str:
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

    def get_path(self, run_id: str, name: str) -> str:
        return str(self._run_dir(run_id) / name)

    def save_json(self, run_id: str, name: str, payload: dict[str, Any]) -> ArtifactRef:
        path = Path(self.get_path(run_id, name))
        path.write_text(json.dumps(payload, indent=2, sort_keys=True, default=str), encoding="utf-8")
        return ArtifactRef(run_id=run_id, name=name, path=str(path))

    def save_text(self, run_id: str, name: str, payload: str) -> ArtifactRef:
        path = Path(self.get_path(run_id, name))
        path.write_text(payload, encoding="utf-8")
        return ArtifactRef(run_id=run_id, name=name, path=str(path))


class S3ArtifactStore(ArtifactStore):
    """Simple S3-backed artifact store.

    Requires `S3_ARTIFACTS_BUCKET` env var or explicit `bucket` parameter.
    Uses the default boto3 credentials chain (env vars, instance profile, etc.).
    """

    def __init__(self, bucket: str | None = None) -> None:
        settings = get_settings()
        self._bucket = bucket or os.getenv("S3_ARTIFACTS_BUCKET")
        if not self._bucket:
            raise RuntimeError("S3_ARTIFACTS_BUCKET must be set to use S3ArtifactStore")
        self._s3 = boto3.client("s3")

    def _key(self, run_id: str, name: str) -> str:
        return f"{run_id}/{name}"

    def get_path(self, run_id: str, name: str) -> str:
        return f"s3://{self._bucket}/{self._key(run_id, name)}"

    def save_json(self, run_id: str, name: str, payload: dict[str, Any]) -> ArtifactRef:
        body = json.dumps(payload, indent=2, sort_keys=True, default=str).encode("utf-8")
        key = self._key(run_id, name)
        try:
            self._s3.put_object(Bucket=self._bucket, Key=key, Body=body)
        except (ClientError, BotoCoreError) as exc:
            raise RuntimeError("Failed to upload artifact to S3") from exc
        return ArtifactRef(run_id=run_id, name=name, path=self.get_path(run_id, name))

    def save_text(self, run_id: str, name: str, payload: str) -> ArtifactRef:
        body = payload.encode("utf-8")
        key = self._key(run_id, name)
        try:
            self._s3.put_object(Bucket=self._bucket, Key=key, Body=body)
        except (ClientError, BotoCoreError) as exc:
            raise RuntimeError("Failed to upload artifact to S3") from exc
        return ArtifactRef(run_id=run_id, name=name, path=self.get_path(run_id, name))


_default_artifact_store: ArtifactStore | None = None


def get_artifact_store() -> ArtifactStore:
    global _default_artifact_store
    if _default_artifact_store is None:
        # Prefer S3 if configured; otherwise fall back to filesystem.
        bucket = os.getenv("S3_ARTIFACTS_BUCKET")
        if bucket:
            _default_artifact_store = S3ArtifactStore(bucket=bucket)
        else:
            _default_artifact_store = FilesystemArtifactStore()
    return _default_artifact_store
