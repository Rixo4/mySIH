from __future__ import annotations

import logging

from ..config import get_settings

logger = logging.getLogger(__name__)


def _redis_url() -> str:
    return get_settings().redis_url


def _make_redis_conn():
    """
    Try to connect to a real Redis instance.
    Fall back to fakeredis (in-process, no external server required)
    when Redis is unavailable -- typical for local dev on Windows without Redis.
    """
    url = _redis_url()
    try:
        from redis import Redis
        conn = Redis.from_url(url, decode_responses=False, socket_connect_timeout=1)
        conn.ping()
        logger.info("redis_conn: connected to real Redis at %s", url)
        return conn
    except Exception as exc:
        logger.warning(
            "redis_conn: Redis unavailable (%s). Falling back to fakeredis (in-process).",
            exc,
        )
        try:
            import fakeredis
            conn = fakeredis.FakeRedis(decode_responses=False)
            logger.info("redis_conn: using fakeredis (in-process, no external server needed)")
            return conn
        except ImportError:
            raise RuntimeError(
                "Redis is not running and fakeredis is not installed. "
                "Run: pip install fakeredis  OR  start a Redis server."
            ) from exc


redis_conn = _make_redis_conn()
