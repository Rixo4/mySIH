from __future__ import annotations

import logging

from rq import Worker

from app.config import get_settings
from app.logging import configure_logging
from app.queue.redis_conn import redis_conn
from app.queue.queues import simulation_queue

configure_logging(get_settings().log_level)

worker = Worker(
    [simulation_queue.name],
    connection=redis_conn,
)



if __name__ == "__main__":
    worker.work()
