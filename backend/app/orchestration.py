from __future__ import annotations

import json
import logging
import socket
import shutil
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any

from sqlalchemy import func, select
from sqlalchemy.orm import Session
from rq.job import Job

from .config import get_settings
from .artifact_store import get_artifact_store
from .database import SessionLocal
from .engine_runner import run_engine
from .models import DoseResult, RunRecord, SimulationJob, SimulationJobStatus
from .queue.queues import simulation_queue
from .queue.redis_conn import redis_conn
from .report_parser import parse_report
from .utils import ensure_directory, generate_run_id, sanitize_run_id, safe_datetime_diff_seconds, utc_now, write_json_file
from .visualization import build_visualization_payload

logger = logging.getLogger(__name__)


def _normalize_zone_name(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    normalized = value.strip().upper()
    return normalized or None


def _enforce_scientific_consistency(parsed: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(parsed, dict):
        return parsed

    summary = dict(parsed)
    active_zone = _normalize_zone_name(summary.get("active_zone"))
    recommendation = _normalize_zone_name(summary.get("recommendation")) or ""
    has_valid_therapeutic_window = bool(summary.get("has_valid_therapeutic_window"))
    zone_ranges = summary.get("zone_ranges") if isinstance(summary.get("zone_ranges"), dict) else {}
    therapeutic_zone = zone_ranges.get("therapeutic_zone") if isinstance(zone_ranges, dict) else None
    has_therapeutic_zone = isinstance(therapeutic_zone, list) and len(therapeutic_zone) == 2
    effective_range_text = str(summary.get("effective_range") or "").strip().lower()
    effective_range_observed = bool(effective_range_text) and effective_range_text != "not observed"
    if not has_valid_therapeutic_window:
        has_valid_therapeutic_window = has_therapeutic_zone and effective_range_observed

    forbidden_therapeutic_zones = {"THERAPEUTIC_ZONE", "STABILIZATION_ZONE", "EXCITATORY_ZONE"}
    if not has_valid_therapeutic_window and active_zone in forbidden_therapeutic_zones:
        active_zone = "NO_VALID_WINDOW"
    if "NOT RECOMMENDED" in recommendation and active_zone in forbidden_therapeutic_zones:
        active_zone = "NO_VALID_WINDOW"

    summary["has_valid_therapeutic_window"] = has_valid_therapeutic_window
    if active_zone is not None:
        summary["active_zone"] = active_zone
    return summary


def _settings():
    return get_settings()


def max_concurrent_simulations() -> int:
    return max(1, _settings().max_concurrent_simulations)


def simulation_timeout_seconds() -> int:
    return max(1, _settings().simulation_timeout_seconds)


def create_simulation_job(
    db: Session,
    *,
    input_payload: dict[str, Any],
    user_id: int | None,
    company_id: int | None,
) -> SimulationJob:
    payload = dict(input_payload)
    payload.setdefault("submitted_at", utc_now().isoformat())
    if user_id is not None:
        payload.setdefault("user_id", user_id)
    if company_id is not None:
        payload.setdefault("company_id", company_id)

    job = SimulationJob(
        status=SimulationJobStatus.QUEUED,
        progress=0.0,
        created_at=utc_now(),
        input_json=payload,
        error_message=None,
        runtime_seconds=None,
        queue_latency_seconds=None,
        worker_hostname=None,
    )
    db.add(job)
    db.commit()
    db.refresh(job)

    logger.info(
        "simulation_job_created",
        extra={
            "job_id": str(job.id),
            "user_id": user_id,
            "company_id": company_id,
        },
    )
    return job


def attach_rq_job_id(db: Session, *, simulation_job_id: uuid.UUID, rq_job_id: str) -> None:
    job = db.get(SimulationJob, simulation_job_id)
    if job is None:
        return
    job.rq_job_id = rq_job_id
    db.commit()


def cancel_scientific_simulation_job(db: Session, *, simulation_job_id: uuid.UUID) -> SimulationJob:
    job = db.get(SimulationJob, simulation_job_id)
    if job is None:
        raise ValueError(f"SimulationJob {simulation_job_id} not found")

    if job.status in {SimulationJobStatus.COMPLETED, SimulationJobStatus.FAILED, SimulationJobStatus.CANCELLED}:
        return job

    if job.rq_job_id:
        try:
            rq_job = Job.fetch(job.rq_job_id, connection=redis_conn)
            rq_job.cancel()
        except Exception:
            logger.info(
                "simulation_job_cancel_rq_cleanup_failed",
                extra={"job_id": str(job.id), "rq_job_id": job.rq_job_id},
            )

    job.status = SimulationJobStatus.CANCELLED
    job.error_message = "Cancelled by user"
    job.completed_at = utc_now()
    if job.started_at is not None:
        job.runtime_seconds = safe_datetime_diff_seconds(job.completed_at, job.started_at)
    db.commit()

    logger.info(
        "simulation_job_cancelled",
        extra={
            "job_id": str(job.id),
            "rq_job_id": job.rq_job_id,
            "worker_hostname": job.worker_hostname,
        },
    )
    return job


def _count_running_jobs(db: Session) -> int:
    stmt = select(func.count()).select_from(SimulationJob).where(SimulationJob.status == SimulationJobStatus.RUNNING)
    return int(db.scalar(stmt) or 0)

def get_queue_stats(db: Session) -> dict[str, int | str]:
    try:
        queued_jobs = int(getattr(simulation_queue, "count", 0) or 0)
    except Exception:
        queued_jobs = 0
    running_jobs = _count_running_jobs(db)
    completed_jobs = int(
        db.scalar(select(func.count()).select_from(SimulationJob).where(SimulationJob.status == SimulationJobStatus.COMPLETED))
        or 0
    )
    failed_jobs = int(
        db.scalar(select(func.count()).select_from(SimulationJob).where(SimulationJob.status == SimulationJobStatus.FAILED))
        or 0
    )

    return {
        "queue_name": simulation_queue.name,
        "queued_jobs": queued_jobs,
        "running_jobs": running_jobs,
        "completed_jobs": completed_jobs,
        "failed_jobs": failed_jobs,
        "active_jobs": queued_jobs + running_jobs,
        "max_concurrent_simulations": max_concurrent_simulations(),
    }


def _safe_float(value: Any) -> float | None:
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        try:
            return float(value)
        except ValueError:
            return None
    return None


def _safe_str(value: Any) -> str | None:
    if value is None:
        return None
    return str(value)


def _contract_summary_from_legacy(report_type: str, raw_report: str, input_payload: dict[str, Any]) -> dict[str, Any]:
    parsed_summary = _enforce_scientific_consistency(parse_report(report_type, raw_report))
    visualization_data = build_visualization_payload(
        report_type=report_type,
        input_payload=input_payload,
        parsed_summary=parsed_summary,
    )
    if visualization_data is not None:
        parsed_summary = {**parsed_summary, "visualization_data": visualization_data}
    return parsed_summary


def _normalize_engine_contract(
    *,
    report_type: str,
    stdout: str,
    input_payload: dict[str, Any],
) -> dict[str, Any]:
    stripped = stdout.strip()
    if stripped:
        try:
            contract = json.loads(stripped)
        except json.JSONDecodeError:
            contract = None
        else:
            if isinstance(contract, dict):
                contract.setdefault("metadata", {})
                contract.setdefault("metrics", {})
                contract.setdefault("dose_results", [])
                contract.setdefault("summary", {})
                return contract

    legacy_summary = _contract_summary_from_legacy(report_type, stdout, input_payload)
    return {
        "summary": legacy_summary,
        "dose_results": legacy_summary.get("visualization_data", {}).get("dose_results", []) if isinstance(legacy_summary.get("visualization_data"), dict) else [],
        "metrics": {},
        "metadata": {
            "output_format": "legacy-text",
        },
    }


def _artifact_run_dir(run_id: str) -> Path:
    settings = _settings()
    return settings.runs_dir / run_id


def _save_run_artifacts(
    run_dir: Path,
    input_payload: dict[str, Any],
    raw_report: str,
    parsed_summary: dict[str, Any],
    metadata: dict[str, Any],
    *,
    stdout: str,
    stderr: str,
    engine_contract: dict[str, Any],
) -> None:
    artifact_store = get_artifact_store()
    ensure_directory(run_dir)
    write_json_file(run_dir / "input.json", input_payload)
    artifact_store.save_text(run_dir.name, "report.txt", raw_report)
    artifact_store.save_json(run_dir.name, "parsed.json", parsed_summary)
    artifact_store.save_json(run_dir.name, "metadata.json", metadata)
    artifact_store.save_json(run_dir.name, "engine_output.json", engine_contract)
    artifact_store.save_text(run_dir.name, "stdout.txt", stdout)
    artifact_store.save_text(run_dir.name, "stderr.txt", stderr)


def _extract_dose_rows(engine_contract: dict[str, Any]) -> list[dict[str, Any]]:
    raw_rows = engine_contract.get("dose_results")
    if not isinstance(raw_rows, list):
        return []
    rows: list[dict[str, Any]] = []
    for row in raw_rows:
        if isinstance(row, dict):
            rows.append(row)
    return rows


def _row_to_dose_result(row: dict[str, Any], *, run_id: str, created_at: datetime) -> DoseResult:
    response_mode = _safe_str(row.get("response_mode"))
    normalized_mode = response_mode.upper() if isinstance(response_mode, str) else None
    effect_value = _safe_float(row.get("effect"))
    suppression_effect = _safe_float(row.get("suppression_effect"))
    excitation_effect = _safe_float(row.get("excitation_effect"))
    stabilization_effect = _safe_float(row.get("stabilization_effect"))

    if suppression_effect is None and normalized_mode == "SUPPRESSIVE_RESPONSE":
        suppression_effect = effect_value
    if excitation_effect is None and normalized_mode == "EXCITATORY_RESPONSE":
        excitation_effect = effect_value
    if stabilization_effect is None and normalized_mode == "STABILIZING_RESPONSE":
        stabilization_effect = effect_value

    return DoseResult(
        run_id=run_id,
        dose=_safe_float(row.get("dose")) or 0.0,
        firing_rate=_safe_float(row.get("firing_rate")),
        seizure_score=_safe_float(row.get("seizure_score")),
        sync_index=_safe_float(row.get("sync_index") or row.get("sync")),
        nii=_safe_float(row.get("nii")),
        toxicity_score=_safe_float(row.get("toxicity_score")),
        response_mode=response_mode,
        biological_state=_safe_str(row.get("biological_state")),
        suppression_effect=suppression_effect,
        excitation_effect=excitation_effect,
        stabilization_effect=stabilization_effect,
        created_at=created_at,
    )


def _persist_run_record(
    db: Session,
    *,
    run_id: str,
    report_type: str,
    engine_input_mode: str,
    drug_name: str | None,
    status: str,
    parsed_summary: dict[str, Any],
    raw_report: str,
    input_payload: dict[str, Any],
    error_message: str | None,
    duration_seconds: float | None,
    user_id: int | None,
    company_id: int | None,
) -> RunRecord:
    recommendation = parsed_summary.get("recommendation") if isinstance(parsed_summary, dict) else None
    risk_level = parsed_summary.get("risk_level") if isinstance(parsed_summary, dict) else None
    confidence = parsed_summary.get("confidence") if isinstance(parsed_summary, dict) else None

    record = RunRecord(
        run_id=run_id,
        report_type=report_type,
        drug_name=drug_name,
        status=status,
        engine_input_mode=engine_input_mode,
        user_id=user_id,
        company_id=company_id,
        recommendation=_safe_str(recommendation),
        risk_level=_safe_str(risk_level),
        confidence=_safe_str(confidence),
        raw_report=raw_report,
        parsed_json=json.dumps(parsed_summary),
        input_json=json.dumps(input_payload),
        error_message=error_message,
        created_at=utc_now(),
        duration_seconds=duration_seconds,
    )
    db.add(record)
    db.flush()
    return record


def _mark_job_running(db: Session, *, job: SimulationJob) -> None:
    started = utc_now()
    job.status = SimulationJobStatus.RUNNING
    job.started_at = started
    job.completed_at = None
    job.error_message = None
    job.worker_hostname = socket.gethostname()
    job.queue_latency_seconds = safe_datetime_diff_seconds(started, job.created_at)
    job.progress = 0.0
    db.commit()

    logger.info(
        "simulation_job_running",
        extra={
            "job_id": str(job.id),
            "queue_latency_seconds": job.queue_latency_seconds,
            "worker_hostname": job.worker_hostname,
        },
    )


def _mark_job_failed(db: Session, *, job: SimulationJob, error_message: str) -> None:
    job.status = SimulationJobStatus.FAILED
    job.error_message = error_message
    job.completed_at = utc_now()
    if job.started_at is not None:
        job.runtime_seconds = safe_datetime_diff_seconds(job.completed_at, job.started_at)
    db.commit()

    logger.error(
        "simulation_job_failed",
        extra={
            "job_id": str(job.id),
            "error_message": error_message,
        },
    )


def _mark_job_completed(db: Session, *, job: SimulationJob) -> None:
    job.status = SimulationJobStatus.COMPLETED
    job.error_message = None
    job.progress = 1.0
    job.completed_at = utc_now()
    if job.started_at is not None:
        job.runtime_seconds = safe_datetime_diff_seconds(job.completed_at, job.started_at)
    db.commit()

    logger.info(
        "simulation_job_completed",
        extra={
            "job_id": str(job.id),
            "runtime_seconds": job.runtime_seconds,
            "result_run_id": job.result_run_id,
        },
    )


def _update_progress(db: Session, *, job: SimulationJob, completed_doses: int, total_doses: int) -> None:
    if total_doses <= 0:
        job.progress = 0.0
    else:
        job.progress = min(1.0, max(0.0, completed_doses / total_doses))
    db.commit()
    logger.info(
        "simulation_job_progress",
        extra={
            "job_id": str(job.id),
            "completed_doses": completed_doses,
            "total_doses": total_doses,
            "progress": round(job.progress, 4),
        },
    )


def enqueue_scientific_simulation(
    db: Session,
    *,
    payload: dict[str, Any],
    report_type: str,
    engine_input_mode: str,
    user_id: int | None,
    company_id: int | None,
    drug_name: str | None = None,
    runs_override: int | None = None,
) -> SimulationJob:
    import threading as _threading
    job = create_simulation_job(
        db,
        input_payload={
            **payload,
            "report_type": report_type,
            "engine_input_mode": engine_input_mode,
            "drug_name": drug_name,
            "runs_override": runs_override,
        },
        user_id=user_id,
        company_id=company_id,
    )

    rq_payload = {
        "simulation_job_id": str(job.id),
        "report_type": report_type,
        "input_payload": payload,
        "drug_name": drug_name,
        "engine_input_mode": engine_input_mode,
        "user_id": user_id,
        "company_id": company_id,
        "runs_override": runs_override,
    }

    # When using fakeredis (is_async=False), enqueue() would block the HTTP request.
    # Instead run the simulation in a background thread so the API returns QUEUED immediately.
    if not simulation_queue.is_async:
        job_id_snapshot = str(job.id)

        def _run_in_thread():
            _db = SessionLocal()
            try:
                run_scientific_simulation_job(rq_payload)
            except Exception as exc:
                logger.error("background_simulation_thread_error", extra={"job_id": job_id_snapshot, "error": str(exc)})
            finally:
                _db.close()

        t = _threading.Thread(target=_run_in_thread, daemon=True)
        t.start()
        logger.info(
            "simulation_job_enqueued_thread",
            extra={
                "job_id": str(job.id),
                "queue_mode": "synchronous_thread",
            },
        )
        return job

    queued_job = simulation_queue.enqueue(
        run_scientific_simulation_job,
        rq_payload,
        job_timeout=simulation_timeout_seconds() + 300,
        result_ttl=86400,
        failure_ttl=86400,
    )
    attach_rq_job_id(db, simulation_job_id=job.id, rq_job_id=queued_job.id)
    logger.info(
        "simulation_job_enqueued",
        extra={
            "job_id": str(job.id),
            "rq_job_id": queued_job.id,
            "queue_name": simulation_queue.name,
        },
    )
    return job


def run_scientific_simulation_job(job_payload: dict[str, Any]) -> dict[str, Any]:
    db = SessionLocal()
    try:
        simulation_job_id_raw = job_payload.get("simulation_job_id")
        if simulation_job_id_raw is None:
            raise ValueError("simulation_job_id is required")
        simulation_job_id = uuid.UUID(str(simulation_job_id_raw))

        job = db.get(SimulationJob, simulation_job_id)
        if job is None:
            raise ValueError(f"SimulationJob {simulation_job_id} not found")

        db.refresh(job)
        if job.status == SimulationJobStatus.CANCELLED:
            return {
                "job_id": str(job.id),
                "status": job.status.value,
                "result_run_id": job.result_run_id,
                "error": job.error_message or "Cancelled by user",
            }

        if _count_running_jobs(db) >= max_concurrent_simulations():
            raise RuntimeError("Maximum concurrent simulations reached")

        db.refresh(job)
        if job.status == SimulationJobStatus.CANCELLED:
            return {
                "job_id": str(job.id),
                "status": job.status.value,
                "result_run_id": job.result_run_id,
                "error": job.error_message or "Cancelled by user",
            }

        report_type = str(job_payload.get("report_type", "simulate"))
        input_payload = job_payload.get("input_payload")
        if not isinstance(input_payload, dict):
            input_payload = {}

        drug_name = job_payload.get("drug_name")
        engine_input_mode = str(job_payload.get("engine_input_mode", "default_internal_engine_config"))
        user_id = job_payload.get("user_id")
        company_id = job_payload.get("company_id")
        runs_override = job_payload.get("runs_override")

        _mark_job_running(db, job=job)

        run_id = sanitize_run_id(generate_run_id())
        run_dir = _artifact_run_dir(run_id)
        ensure_directory(run_dir)
        write_json_file(run_dir / "input.json", input_payload)

        env_overrides: dict[str, str] = {}
        if report_type == "dose-eval" and isinstance(runs_override, int):
            env_overrides["SPP_DOSE_EVAL_RUNS"] = str(runs_override)

        if report_type == "dose-eval" and isinstance(input_payload, dict):
            channels = input_payload.get("channels")
            dose_range = input_payload.get("dose_range")

            if isinstance(channels, dict):
                na_cfg = channels.get("Na")
                k_cfg = channels.get("K")
                ca_cfg = channels.get("Ca")

                if isinstance(na_cfg, dict) and isinstance(na_cfg.get("ic50"), (int, float)):
                    env_overrides["SPP_DOSE_EVAL_IC50_NA"] = str(na_cfg["ic50"])
                if isinstance(k_cfg, dict) and isinstance(k_cfg.get("ic50"), (int, float)):
                    env_overrides["SPP_DOSE_EVAL_IC50_K"] = str(k_cfg["ic50"])
                if isinstance(ca_cfg, dict) and isinstance(ca_cfg.get("ic50"), (int, float)):
                    env_overrides["SPP_DOSE_EVAL_IC50_CA"] = str(ca_cfg["ic50"])
                if isinstance(na_cfg, dict) and isinstance(na_cfg.get("hill"), (int, float)):
                    env_overrides["SPP_DOSE_EVAL_HILL"] = str(na_cfg["hill"])

            if isinstance(dose_range, dict):
                if isinstance(dose_range.get("min"), (int, float)):
                    env_overrides["SPP_DOSE_EVAL_MIN"] = str(dose_range["min"])
                if isinstance(dose_range.get("max"), (int, float)):
                    env_overrides["SPP_DOSE_EVAL_MAX"] = str(dose_range["max"])
                if isinstance(dose_range.get("step"), (int, float)):
                    env_overrides["SPP_DOSE_EVAL_STEP"] = str(dose_range["step"])

        if report_type == "dose-eval" and isinstance(input_payload, dict):
            engine_input_mode = "user_config"

        engine_mode = report_type if report_type in {"simulate", "dose-eval", "validate"} else "validate"

        engine_result = run_engine(
            engine_mode,
            env_overrides=env_overrides or None,
            drug_config_path=str(run_dir / "input.json"),
            cwd=str(run_dir),
        )

        db.refresh(job)
        if job.status == SimulationJobStatus.CANCELLED:
            shutil.rmtree(run_dir, ignore_errors=True)
            return {
                "job_id": str(job.id),
                "status": job.status.value,
                "result_run_id": job.result_run_id,
                "error": job.error_message or "Cancelled by user",
            }

        raw_report = engine_result.stdout or ""
        pharma_dir = run_dir / "output_pharma_decision"
        if pharma_dir.exists() and pharma_dir.is_dir():
            report_file = pharma_dir / "liability_screening_report.txt"
            if report_file.exists() and not raw_report.strip():
                raw_report = report_file.read_text(encoding="utf-8")

        engine_contract = _normalize_engine_contract(
            report_type=engine_mode,
            stdout=raw_report,
            input_payload=input_payload,
        )

        if pharma_dir.exists() and pharma_dir.is_dir():
            csv_file = pharma_dir / "dose_response.csv"
            if csv_file.exists() and not engine_contract.get("dose_results"):
                import csv
                dose_rows = []
                try:
                    with open(csv_file, "r", encoding="utf-8") as f:
                        reader = csv.DictReader(f)
                        for r in reader:
                            dose_rows.append({
                                "dose": float(r.get("dose") or 0),
                                "firing_rate": float(r.get("mean_firing_rate_hz") or 0),
                                "sync_index": float(r.get("synchronization") or 0),
                                "sync": float(r.get("synchronization") or 0),
                                "seizure_score": float(r.get("seizure_probability_pct") or 0),
                                "nii": float(r.get("nii") or 0),
                                "response_mode": r.get("classification"),
                                "biological_state": r.get("classification"),
                                "effect": float(r.get("suppression_pct") or 0),
                            })
                    engine_contract["dose_results"] = dose_rows
                except Exception as e:
                    logger.warning("Failed to parse dose_response.csv: %s", e)

        parsed_summary = engine_contract.get("summary") if isinstance(engine_contract.get("summary"), dict) else {}
        visualization_data = build_visualization_payload(
            report_type=engine_mode,
            input_payload=input_payload,
            parsed_summary=parsed_summary,
        )
        if visualization_data is not None:
            if not visualization_data.get("dose_results") and engine_contract.get("dose_results"):
                visualization_data["dose_results"] = engine_contract["dose_results"]
            parsed_summary = {**parsed_summary, "visualization_data": visualization_data}
            engine_contract["summary"] = parsed_summary

        metadata = {
            "run_id": run_id,
            "report_type": report_type,
            "engine_input_mode": engine_input_mode,
            "status": engine_result.status,
            "engine_path": str(_settings().engine_path),
            "duration_seconds": engine_result.duration_seconds,
            "queue_latency_seconds": job.queue_latency_seconds,
            "worker_hostname": job.worker_hostname,
            "command": engine_result.command,
            "return_code": engine_result.return_code,
        }
        _save_run_artifacts(
            run_dir,
            input_payload,
            raw_report,
            parsed_summary,
            metadata,
            stdout=engine_result.stdout or "",
            stderr=engine_result.stderr or "",
            engine_contract=engine_contract,
        )

        if engine_result.status != "completed":
            error_message = engine_result.error or "Simulation failed"
            record = _persist_run_record(
                db,
                run_id=run_id,
                report_type=report_type,
                engine_input_mode=engine_input_mode,
                drug_name=_safe_str(drug_name),
                status="failed",
                parsed_summary=parsed_summary,
                raw_report=raw_report,
                input_payload=input_payload,
                error_message=error_message,
                duration_seconds=engine_result.duration_seconds,
                user_id=int(user_id) if isinstance(user_id, int) else None,
                company_id=int(company_id) if isinstance(company_id, int) else None,
            )
            job.result_run_id = record.run_id
            _mark_job_failed(db, job=job, error_message=error_message)
            return {
                "job_id": str(job.id),
                "status": job.status.value,
                "result_run_id": record.run_id,
                "error": error_message,
            }

        record = _persist_run_record(
            db,
            run_id=run_id,
            report_type=report_type,
            engine_input_mode=engine_input_mode,
            drug_name=_safe_str(drug_name),
            status="completed",
            parsed_summary=parsed_summary,
            raw_report=raw_report,
            input_payload=input_payload,
            error_message=None,
            duration_seconds=engine_result.duration_seconds,
            user_id=int(user_id) if isinstance(user_id, int) else None,
            company_id=int(company_id) if isinstance(company_id, int) else None,
        )
        job.result_run_id = record.run_id

        dose_rows = _extract_dose_rows(engine_contract)
        total_doses = len(dose_rows)
        created_at = utc_now()
        for index, row in enumerate(dose_rows, start=1):
            db.refresh(job)
            if job.status == SimulationJobStatus.CANCELLED:
                shutil.rmtree(run_dir, ignore_errors=True)
                return {
                    "job_id": str(job.id),
                    "status": job.status.value,
                    "result_run_id": job.result_run_id,
                    "error": job.error_message or "Cancelled by user",
                }
            db.add(_row_to_dose_result(row, run_id=record.run_id, created_at=created_at))
            _update_progress(db, job=job, completed_doses=index, total_doses=total_doses)

        if total_doses == 0:
            job.progress = 1.0
            db.commit()

        _mark_job_completed(db, job=job)
        return {
            "job_id": str(job.id),
            "status": job.status.value,
            "result_run_id": record.run_id,
            "run_response": {
                "run_id": record.run_id,
                "status": record.status,
            },
        }
    except Exception as exc:
        db.rollback()
        try:
            job_id_raw = job_payload.get("simulation_job_id")
            if job_id_raw is not None:
                job = db.get(SimulationJob, uuid.UUID(str(job_id_raw)))
                if job is not None:
                    _mark_job_failed(db, job=job, error_message=str(exc))
        except Exception:
            db.rollback()
            logger.exception("simulation_job_failure_recovery_failed")
        logger.exception("simulation_job_exception")
        return {
            "job_id": str(job_payload.get("simulation_job_id") or ""),
            "status": "FAILED",
            "error": str(exc),
        }
    finally:
        db.close()


def serialize_simulation_job(job: SimulationJob) -> dict[str, Any]:
    return {
        "job_id": str(job.id),
        "status": job.status.value,
        "progress": int(round(max(0.0, min(1.0, job.progress)) * 100)),
        "created_at": job.created_at,
        "started_at": job.started_at,
        "completed_at": job.completed_at,
        "runtime_seconds": job.runtime_seconds,
        "queue_latency_seconds": job.queue_latency_seconds,
        "result_run_id": job.result_run_id,
        "error_message": job.error_message,
        "rq_job_id": job.rq_job_id,
    }


def job_owner_matches(job: SimulationJob, *, user_id: int, company_id: int | None, user_role: str) -> bool:
    if user_role == "admin":
        return True
    payload = job.input_json or {}
    if isinstance(payload, dict):
        job_user_id = payload.get("user_id")
        job_company_id = payload.get("company_id")
        if job_user_id == user_id:
            return True
        if company_id is not None and job_company_id == company_id:
            return True
    return False