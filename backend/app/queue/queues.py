from __future__ import annotations

import logging
import threading

from rq import Queue

from app.queue.redis_conn import redis_conn

logger = logging.getLogger(__name__)

# ── Detect if we are in fakeredis fallback mode ──────────────────────────────
_using_fakeredis = type(redis_conn).__module__.startswith("fakeredis")

simulation_queue = Queue(
    "simulation_queue",
    connection=redis_conn,
    # In fakeredis mode, synchronous execution lets jobs run inline without a worker process.
    is_async=not _using_fakeredis,
)

if _using_fakeredis:
    logger.info(
        "queue: running in SYNCHRONOUS mode (fakeredis). "
        "Jobs execute inline — no separate worker process required."
    )
else:
    logger.info("queue: running in ASYNC mode. Ensure the RQ worker is running.")
