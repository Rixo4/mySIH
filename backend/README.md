# Silicon Patient Backend

Production-ready FastAPI backend for the SILICON PATIENT PLATFORM C++/CUDA engine.

## 1. Setup

1. Open a terminal in `backend/`.
2. Create and activate a Python 3.11+ virtual environment.
3. Install dependencies.

## 2. Install Requirements

```bash
pip install -r requirements.txt
```

## 3. Configure Engine Path

1. Copy `.env.example` to `.env`.
2. Set `SPP_ENGINE_PATH`.

Default value:

```env
SPP_ENGINE_PATH=../build-cuda/silicon_patient.exe
```

Optional settings:

```env
SPP_DATABASE_URL=sqlite:///./silicon_patient.db
SPP_ENGINE_TIMEOUT_SECONDS=900
```

## 4. Start Backend

```bash
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

## 5. Example curl Requests

### Health

```bash
curl -X GET http://localhost:8000/health
```

### Simulate

```bash
curl -X POST http://localhost:8000/api/simulate
```

### Dose Eval

```bash
curl -X POST http://localhost:8000/api/dose-eval \
  -H "Content-Type: application/json" \
  -d '{
    "drug_name": "TestDrug-X",
    "channels": {
      "Na": { "ic50": 200.0, "hill": 3.2 },
      "K":  { "ic50": 8.0, "hill": 3.2 },
      "Ca": { "ic50": 1000.0, "hill": 3.2 }
    },
    "dose_range": {
      "min": 0.0,
      "max": 20.0,
      "step": 2.0
    },
    "runs": 3
  }'
```

### Validate

```bash
curl -X POST http://localhost:8000/api/validate
```

### List Runs

```bash
curl -X GET http://localhost:8000/api/runs
```

### Run Detail

```bash
curl -X GET http://localhost:8000/api/runs/<run_id>
```

### Raw Report

```bash
curl -X GET http://localhost:8000/api/runs/<run_id>/report
```

## 6. Expected Responses

### Success run response shape

```json
{
  "run_id": "run_20260427T120000Z_ab12cd34ef",
  "status": "completed",
  "report_type": "dose-eval",
  "engine_input_mode": "default_internal_engine_config",
  "parsed_summary": {
    "curve_type": "Sigmoidal",
    "recommendation": "Proceed"
  },
  "raw_report": "...",
  "duration_seconds": 12.4,
  "created_at": "2026-04-27T12:00:00+00:00",
  "error": null,
  "stderr": null
}
```

### Failure examples

Engine not found:

```json
{
  "run_id": "run_...",
  "status": "failed",
  "report_type": "simulate",
  "engine_input_mode": "default_internal_engine_config",
  "parsed_summary": {},
  "raw_report": "",
  "duration_seconds": 0.01,
  "created_at": "2026-04-27T12:00:00+00:00",
  "error": "Engine executable not found",
  "stderr": null
}
```

Timeout:

```json
{
  "status": "failed",
  "error": "Engine execution timed out"
}
```

Non-zero exit:

```json
{
  "status": "failed",
  "error": "Engine execution failed",
  "stderr": "..."
}
```

## Auditability Storage

Each run is persisted in SQLite and also written to filesystem:

```text
runs/<run_id>/
  input.json
  report.txt
  parsed.json
  metadata.json
```

This backend accepts and validates dose-eval JSON input now, stores it for traceability, and runs the current engine with internal defaults (`engine_input_mode=default_internal_engine_config`) until direct JSON engine integration is available.
