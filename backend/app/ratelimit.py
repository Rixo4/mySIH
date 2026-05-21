from __future__ import annotations

import os
import time
from typing import Optional

try:
    import redis
except Exception:  # pragma: no cover - redis may be absent in dev
    redis = None


class InMemoryStore:
    def __init__(self) -> None:
        # key -> (count, expires_at)
        self._data: dict[str, tuple[int, float]] = {}

    def incr(self, key: str, window: int) -> int:
        now = time.time()
        count, exp = self._data.get(key, (0, now + window))
        if now > exp:
            count = 0
            exp = now + window
        count += 1
        self._data[key] = (count, exp)
        return count

    def get(self, key: str) -> int:
        now = time.time()
        count, exp = self._data.get(key, (0, 0))
        if now > exp:
            return 0
        return count

    def expire(self, key: str, _seconds: int) -> None:
        now = time.time()
        count, _ = self._data.get(key, (0, now + _seconds))
        self._data[key] = (count, now + _seconds)

    def delete(self, key: str) -> None:
        self._data.pop(key, None)

    def exists(self, key: str) -> bool:
        now = time.time()
        count, exp = self._data.get(key, (0, 0))
        return now <= exp


class RateLimiter:
    def __init__(self, redis_url: Optional[str] = None) -> None:
        self._client = None
        self._store: Optional[InMemoryStore] = None
        if redis and (redis_url or os.getenv("REDIS_URL")):
            url = redis_url or os.getenv("REDIS_URL")
            try:
                self._client = redis.from_url(url)
                # test connection
                self._client.ping()
            except Exception:
                self._client = None
        if self._client is None:
            self._store = InMemoryStore()

    def incr_with_expire(self, key: str, window: int = 60) -> int:
        if self._client is not None:
            val = self._client.incr(key)
            if val == 1:
                try:
                    self._client.expire(key, window)
                except Exception:
                    pass
            return int(val)

        assert self._store is not None
        return self._store.incr(key, window)

    def get(self, key: str) -> int:
        if self._client is not None:
            val = self._client.get(key)
            return int(val) if val is not None else 0
        assert self._store is not None
        return self._store.get(key)

    def delete(self, key: str) -> None:
        if self._client is not None:
            try:
                self._client.delete(key)
            except Exception:
                pass
            return
        assert self._store is not None
        self._store.delete(key)

    def exists(self, key: str) -> bool:
        if self._client is not None:
            try:
                return self._client.exists(key) == 1
            except Exception:
                return False
        assert self._store is not None
        return self._store.exists(key)

    def set_lock(self, key: str, seconds: int) -> None:
        lock_key = f"lock:{key}"
        if self._client is not None:
            try:
                self._client.set(lock_key, 1, ex=seconds)
            except Exception:
                pass
            return
        # in-memory: set a key with expire
        assert self._store is not None
        self._store.incr(lock_key, seconds)

    def is_locked(self, key: str) -> bool:
        lock_key = f"lock:{key}"
        return self.exists(lock_key)


# Singleton instance
_limiter: RateLimiter | None = None


def get_rate_limiter() -> RateLimiter:
    global _limiter
    if _limiter is None:
        _limiter = RateLimiter()
    return _limiter
