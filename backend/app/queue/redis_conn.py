from __future__ import annotations

from redis import Redis

from ..config import get_settings


def _redis_url() -> str:
    return get_settings().redis_url


redis_conn = Redis.from_url(
    _redis_url(),
    decode_responses=False,
)
