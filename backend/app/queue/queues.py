from rq import Queue
from app.queue.redis_conn import redis_conn

simulation_queue = Queue(
    "simulation_queue",
    connection=redis_conn
)
