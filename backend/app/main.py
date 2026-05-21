from __future__ import annotations

from fastapi import Depends, FastAPI, HTTPException
import os
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import PlainTextResponse
from sqlalchemy.orm import Session

from .config import get_settings
from .database import Base, engine, get_db
from .schemas import (
    DeleteRunResponse,
    DoseEvalRequest,
    HealthResponse,
    RunDetailResponse,
    RunResponse,
    SingleDoseSimulationRequest,
    RunsListResponse,
)
from .services import delete_run, execute_engine_run, get_run_detail_or_none, get_run_or_none, list_runs
from .utils import ensure_directory

settings = get_settings()

app = FastAPI(title=settings.service_name, version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.on_event("startup")
def on_startup() -> None:
    ensure_directory(settings.runs_dir)
    ensure_directory(settings.reports_dir)
    Base.metadata.create_all(bind=engine)


@app.get("/health", response_model=HealthResponse)
def health() -> HealthResponse:
    return HealthResponse(status="ok", service=settings.service_name)


@app.post("/api/simulate", response_model=RunResponse)
def simulate(payload: SingleDoseSimulationRequest | None = None, db: Session = Depends(get_db)) -> RunResponse:
    input_payload = payload.model_dump() if payload is not None else {"mode": "simulate"}
    result = execute_engine_run(
        db=db,
        report_type="simulate",
        input_payload=input_payload,
        drug_name=payload.drug_name if payload is not None else None,
        engine_input_mode="default_internal_engine_config",
    )
    return RunResponse(**result)


@app.post("/api/dose-eval", response_model=RunResponse)
def dose_eval(payload: DoseEvalRequest, db: Session = Depends(get_db)) -> RunResponse:
    result = execute_engine_run(
        db=db,
        report_type="dose-eval",
        input_payload=payload.model_dump(by_alias=True),
        drug_name=payload.drug_name,
        runs_override=payload.runs,
        engine_input_mode="user_config",
    )
    return RunResponse(**result)


# Internal developer-only endpoint: run the Internal Biological Benchmark Suite.
# This route is intentionally placed under /api/internal to avoid exposure as a
# public workflow. Use only in developer or research environments.
@app.post("/api/internal/validate", response_model=RunResponse)
def internal_validate(db: Session = Depends(get_db)) -> RunResponse:
    # Only allow this developer endpoint when SPP_DEVELOPER_MODE=1 is set
    if os.getenv("SPP_DEVELOPER_MODE", "0") != "1":
        raise HTTPException(status_code=404, detail="Not found")

    result = execute_engine_run(
        db=db,
        report_type="internal-benchmark",
        input_payload={"mode": "internal-benchmark"},
        drug_name=None,
        engine_input_mode="default_internal_engine_config",
    )
    return RunResponse(**result)


@app.get("/api/runs", response_model=RunsListResponse)
def get_runs(db: Session = Depends(get_db)) -> RunsListResponse:
    return RunsListResponse(runs=list_runs(db))


@app.get("/api/runs/{run_id}", response_model=RunDetailResponse)
def get_run_detail(run_id: str, db: Session = Depends(get_db)) -> RunDetailResponse:
    try:
        detail = get_run_detail_or_none(db, run_id)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    if detail is None:
        raise HTTPException(status_code=404, detail="Run not found")

    return RunDetailResponse(**detail)


@app.get("/api/runs/{run_id}/report", response_class=PlainTextResponse)
def get_run_report(run_id: str, db: Session = Depends(get_db)) -> PlainTextResponse:
    try:
        record = get_run_or_none(db, run_id)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    if record is None:
        raise HTTPException(status_code=404, detail="Run not found")

    return PlainTextResponse(record.raw_report or "")


@app.delete("/api/runs/{run_id}", response_model=DeleteRunResponse)
def remove_run(run_id: str, db: Session = Depends(get_db)) -> DeleteRunResponse:
    try:
        deleted = delete_run(db, run_id)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    if not deleted:
        raise HTTPException(status_code=404, detail="Run not found")

    return DeleteRunResponse(run_id=run_id, deleted=True)
