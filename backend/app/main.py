from __future__ import annotations

import os
import uuid
import logging

from fastapi import Depends, FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import PlainTextResponse
from sqlalchemy.orm import Session

from .auth import require_roles, require_user, router as auth_router
from .logging import configure_logging
from .db_migrations import ensure_auth_schema
from .config import get_settings
from .database import Base, engine, get_db
from .database import SessionLocal
from .models import SimulationJob
from . import models as _models_registry  # noqa: F401
from .orchestration import enqueue_scientific_simulation, get_queue_stats, job_owner_matches, serialize_simulation_job
from .schemas import (
    DeleteRunResponse,
    DoseEvalRequest,
    HealthResponse,
    QueuedSimulationJobResponse,
    QueueStatsResponse,
    ScientificSimulationStatusResponse,
    ScientificSimulationSubmitResponse,
    RunDetailResponse,
    RunResponse,
    SingleDoseSimulationRequest,
    RunsListResponse,
    SimulationJobStatusResponse,
)
from .services import delete_run, get_run_detail_or_none, get_run_or_none, list_runs
from .utils import ensure_directory
from .queue.queues import simulation_queue
from .jobs.simulation_jobs import run_simulation_job

settings = get_settings()
logger = logging.getLogger(__name__)
configure_logging(settings.log_level)

app = FastAPI(title=settings.service_name, version="1.0.0")

# Security headers middleware
@app.middleware("http")
async def set_secure_headers(request, call_next):
    response = await call_next(request)
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    response.headers["Referrer-Policy"] = "no-referrer"
    response.headers["Content-Security-Policy"] = "default-src 'self'"
    if os.getenv("ENVIRONMENT", "development") == "production":
        response.headers["Strict-Transport-Security"] = "max-age=63072000; includeSubDomains; preload"
    return response

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Mount auth router
app.include_router(auth_router)


@app.on_event("startup")
def on_startup() -> None:
    ensure_directory(settings.runs_dir)
    ensure_directory(settings.reports_dir)
    Base.metadata.create_all(bind=engine)
    ensure_auth_schema(engine)
    # Production hardening checks
    env = os.getenv("ENVIRONMENT", "development")
    if env == "production":
        # SECRET_KEY must be set and not the dev fallback
        secret = os.getenv("SECRET_KEY") or os.getenv("SPP_SECRET_KEY")
        if not secret or secret == "silicon-patient-dev-secret-key":
            raise RuntimeError("SECRET_KEY must be set to a strong value in production")

        # Require secure cookie for auth refresh in production
        cookie_secure = os.getenv("AUTH_COOKIE_SECURE", "false").lower() == "true"
        if not cookie_secure:
            raise RuntimeError("AUTH_COOKIE_SECURE must be 'true' in production")

        samesite = os.getenv("AUTH_COOKIE_SAMESITE", "lax").lower()
        if samesite == "lax":
            raise RuntimeError("AUTH_COOKIE_SAMESITE must be 'strict' or 'none' in production")


@app.get("/health", response_model=HealthResponse)
def health() -> HealthResponse:
    return HealthResponse(status="ok", service=settings.service_name)

@app.get("/api/queue/stats", response_model=QueueStatsResponse)
def get_queue_stats_endpoint(db: Session = Depends(get_db), user=Depends(require_user)) -> QueueStatsResponse:
    stats = get_queue_stats(db)
    return QueueStatsResponse(**stats)


def _submit_scientific_job(
    *,
    payload: SingleDoseSimulationRequest | None,
    user,
) -> tuple[str, str]:
    input_payload = payload.model_dump() if payload is not None else {"mode": "simulate"}
    with SessionLocal() as orchestration_db:
        simulation_job = enqueue_scientific_simulation(
            orchestration_db,
            payload=input_payload,
            report_type="simulate",
            engine_input_mode="default_internal_engine_config",
            user_id=user.id if user is not None else None,
            company_id=user.company_id if user is not None else None,
            drug_name=payload.drug_name if payload is not None else None,
        )
    logger.info("simulation_job_enqueued", extra={"simulation_job_id": str(simulation_job.id)})
    return str(simulation_job.id), simulation_job.status.value


@app.post("/queue-test")
def queue_test():
    """Test endpoint for queue infrastructure validation."""
    payload = {
        "drug": "test_drug",
        "timestamp": "2026-05-26T00:00:00Z"
    }

    job = simulation_queue.enqueue(
        run_simulation_job,
        payload
    )

    return {
        "job_id": job.id,
        "status": "QUEUED"
    }


@app.post("/simulate")
def simulate_background_job(payload: SingleDoseSimulationRequest | None = None, user=Depends(require_roles("admin", "researcher"))) -> ScientificSimulationSubmitResponse:
    job_id, _ = _submit_scientific_job(payload=payload, user=user)
    return ScientificSimulationSubmitResponse(job_id=job_id, status="QUEUED")


@app.post("/api/simulate/async")
def simulate_async(payload: SingleDoseSimulationRequest | None = None, user=Depends(require_roles("admin", "researcher"))) -> QueuedSimulationJobResponse:
    simulation_job_id, job_status = _submit_scientific_job(payload=payload, user=user)
    return QueuedSimulationJobResponse(job_id=simulation_job_id, status=job_status, queue_name=simulation_queue.name)


@app.post("/api/simulate/sync", response_model=ScientificSimulationSubmitResponse)
def simulate_sync(payload: SingleDoseSimulationRequest | None = None, user=Depends(require_roles("admin", "researcher"))) -> ScientificSimulationSubmitResponse:
    job_id, _ = _submit_scientific_job(payload=payload, user=user)
    return ScientificSimulationSubmitResponse(job_id=job_id, status="QUEUED")


@app.get("/api/jobs/{job_id}", response_model=ScientificSimulationStatusResponse)
def get_job_status_rq(job_id: str, db: Session = Depends(get_db), user=Depends(require_user)) -> ScientificSimulationStatusResponse:
    return get_scientific_job_status(job_id=job_id, db=db, user=user)


@app.get("/jobs/{job_id}", response_model=ScientificSimulationStatusResponse)
def get_scientific_job_status(job_id: str, db: Session = Depends(get_db), user=Depends(require_user)) -> ScientificSimulationStatusResponse:
    try:
        parsed_id = uuid.UUID(job_id)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail="Invalid job id") from exc

    job = db.get(SimulationJob, parsed_id)
    if job is None:
        raise HTTPException(status_code=404, detail="Job not found")

    if not job_owner_matches(job, user_id=user.id, company_id=user.company_id, user_role=user.role):
        raise HTTPException(status_code=403, detail="Forbidden")

    payload = serialize_simulation_job(job)
    return ScientificSimulationStatusResponse(
        job_id=payload["job_id"],
        status=payload["status"],
        progress=payload["progress"],
        created_at=payload["created_at"],
        started_at=payload["started_at"],
        completed_at=payload["completed_at"],
        result_run_id=payload.get("result_run_id"),
        error_message=payload.get("error_message"),
        runtime_seconds=payload.get("runtime_seconds"),
        queue_latency_seconds=payload.get("queue_latency_seconds"),
    )


@app.post("/api/dose-eval", response_model=ScientificSimulationSubmitResponse)
def dose_eval(payload: DoseEvalRequest, db: Session = Depends(get_db), user=Depends(require_roles("admin", "researcher"))) -> ScientificSimulationSubmitResponse:
    simulation_job = enqueue_scientific_simulation(
        db,
        payload=payload.model_dump(by_alias=True),
        report_type="dose-eval",
        engine_input_mode="user_config",
        user_id=user.id,
        company_id=user.company_id,
        drug_name=payload.drug_name,
        runs_override=payload.runs,
    )
    return ScientificSimulationSubmitResponse(job_id=str(simulation_job.id), status="QUEUED")


# Internal developer-only endpoint: run the Internal Biological Benchmark Suite.
# This route is intentionally placed under /api/internal to avoid exposure as a
# public workflow. Use only in developer or research environments.
@app.post("/api/internal/validate", response_model=ScientificSimulationSubmitResponse)
def internal_validate(db: Session = Depends(get_db)) -> ScientificSimulationSubmitResponse:
    # Only allow this developer endpoint when SPP_DEVELOPER_MODE=1 is set
    if os.getenv("SPP_DEVELOPER_MODE", "0") != "1":
        raise HTTPException(status_code=404, detail="Not found")

    simulation_job = enqueue_scientific_simulation(
        db,
        payload={"mode": "internal-benchmark"},
        report_type="validate",
        engine_input_mode="default_internal_engine_config",
        user_id=None,
        company_id=None,
    )
    return ScientificSimulationSubmitResponse(job_id=str(simulation_job.id), status="QUEUED")


@app.get("/api/runs", response_model=RunsListResponse)
def get_runs(db: Session = Depends(get_db), user=Depends(require_user)) -> RunsListResponse:
    rows = list_runs(db)
    if user.role == "admin":
        filtered = rows
    else:
        filtered = [r for r in rows if r.get("user_id") == user.id]
    return RunsListResponse(runs=filtered)


@app.get("/api/runs/{run_id}", response_model=RunDetailResponse)
def get_run_detail(run_id: str, db: Session = Depends(get_db), user=Depends(require_user)) -> RunDetailResponse:
    try:
        detail = get_run_detail_or_none(db, run_id)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    if detail is None:
        raise HTTPException(status_code=404, detail="Run not found")
    if user.role != "admin":
        if detail.get("user_id") != user.id:
            raise HTTPException(status_code=403, detail="Forbidden")

    return RunDetailResponse(**detail)


@app.get("/api/runs/{run_id}/report", response_class=PlainTextResponse)
def get_run_report(run_id: str, db: Session = Depends(get_db), user=Depends(require_user)) -> PlainTextResponse:
    try:
        record = get_run_or_none(db, run_id)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    if record is None:
        raise HTTPException(status_code=404, detail="Run not found")

    if user.role != "admin":
        if record.user_id != user.id:
            raise HTTPException(status_code=403, detail="Forbidden")

    return PlainTextResponse(record.raw_report or "")


@app.delete("/api/runs/{run_id}", response_model=DeleteRunResponse)
def remove_run(run_id: str, db: Session = Depends(get_db), user=Depends(require_user)) -> DeleteRunResponse:
    try:
        record = get_run_or_none(db, run_id)
        if record is None:
            raise HTTPException(status_code=404, detail="Run not found")
        if user.role != "admin" and record.user_id != user.id:
            raise HTTPException(status_code=403, detail="Forbidden")

        deleted = delete_run(db, run_id)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    if not deleted:
        raise HTTPException(status_code=404, detail="Run not found")

    return DeleteRunResponse(run_id=run_id, deleted=True)
