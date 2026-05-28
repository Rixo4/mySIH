# Queue Infrastructure - Quick Command Reference

## Start Redis
```bash
redis-server
redis-cli ping
```

Expected output:
```text
PONG
```

## Start FastAPI
```bash
cd /home/ranjith/projectubantu/Neuro_drug_testing/backend
source ../.venv/bin/activate
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

## Start the worker
```bash
cd /home/ranjith/projectubantu/Neuro_drug_testing/backend
source ../.venv/bin/activate
python -m app.workers.simulation_worker
```

## Test the endpoint
```bash
curl -s -X POST http://127.0.0.1:8000/queue-test | python -m json.tool
```

Expected response:
```json
{
  "job_id": "...",
  "status": "QUEUED"
}
```

## Stop services
- Press `Ctrl+C` in the FastAPI terminal
- Press `Ctrl+C` in the worker terminal
- Optionally stop Redis with `redis-cli shutdown`

## If jobs are not running
1. Check `redis-cli ping` returns `PONG`
2. Confirm the worker terminal shows `*** Listening on simulation_queue...`
3. Confirm FastAPI is still running on port `8000`

