# Queue Infrastructure Setup Guide

This is the shortest path to run the Redis + RQ queue flow.

## What you need
- Redis running locally on port `6379`
- Python virtual environment at `.venv`
- `redis` and `rq` installed in that environment

## Start order

### 1. Start Redis
```bash
redis-server
```

Check it:
```bash
redis-cli ping
```

Expected:
```text
PONG
```

### 2. Start FastAPI
```bash
cd /home/ranjith/projectubantu/Neuro_drug_testing/backend
source ../.venv/bin/activate
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

### 3. Start the worker
Open a second terminal and run:
```bash
cd /home/ranjith/projectubantu/Neuro_drug_testing/backend
source ../.venv/bin/activate
python -m app.workers.simulation_worker
```

### 4. Test the queue endpoint
```bash
curl -s -X POST http://127.0.0.1:8000/queue-test | python -m json.tool
```

Expected:
```json
{
  "job_id": "...",
  "status": "QUEUED"
}
```

## What should happen
- FastAPI returns immediately
- Redis stores the job
- Worker prints the payload, waits 10 seconds, then prints completion

## Stop
- Press `Ctrl+C` in the FastAPI terminal
- Press `Ctrl+C` in the worker terminal
- Optionally stop Redis with `redis-cli shutdown`

## If it does not work
1. Confirm `redis-cli ping` returns `PONG`
2. Confirm the worker terminal says `*** Listening on simulation_queue...`
3. Confirm FastAPI is still running on port `8000`

The temporary test job sleeps for 10 seconds to validate infrastructure. Replace with neuroscience engine logic when ready.
