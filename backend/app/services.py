from __future__ import annotations

import json
import shutil
from pathlib import Path
from typing import Any

from sqlalchemy import select
from sqlalchemy.orm import Session

from .config import get_settings
from .engine_runner import run_engine
from .models import RunRecord
from .report_parser import parse_report
from .visualization import build_visualization_payload
from .utils import ensure_directory, generate_run_id, loads_or_default, sanitize_run_id, utc_now, write_json_file, write_text_file


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
    max_effect_value = summary.get("max_effect")
    low_effect = isinstance(max_effect_value, (int, float)) and max_effect_value < 20
    response_strength = _normalize_zone_name(summary.get("response_strength"))
    no_significant_response = low_effect and not has_valid_therapeutic_window and response_strength in {None, "NONE", "NOT OBSERVED", "NO RESPONSE"}

    if not has_valid_therapeutic_window:
        has_valid_therapeutic_window = has_therapeutic_zone and effective_range_observed

    forbidden_therapeutic_zones = {"THERAPEUTIC_ZONE", "STABILIZATION_ZONE", "EXCITATORY_ZONE"}
    if not has_valid_therapeutic_window and active_zone in forbidden_therapeutic_zones:
        active_zone = "NO_VALID_WINDOW"
    if "NOT RECOMMENDED" in recommendation and active_zone in forbidden_therapeutic_zones:
        active_zone = "NO_VALID_WINDOW"
    if no_significant_response:
        summary["response_mode"] = "NO_SIGNIFICANT_RESPONSE"
        summary["response_strength"] = "None"
        active_zone = "NO_VALID_WINDOW"

    summary["has_valid_therapeutic_window"] = has_valid_therapeutic_window
    if active_zone is not None:
        summary["active_zone"] = active_zone
    summary.setdefault("consistency_checks", {})
    if isinstance(summary["consistency_checks"], dict):
        summary["consistency_checks"] = {
            **summary["consistency_checks"],
            "therapeutic_window_valid": has_valid_therapeutic_window,
            "active_zone": summary.get("active_zone"),
            "recommendation": summary.get("recommendation"),
        }
    return summary


def _extract_decision_fields(parsed: dict[str, Any]) -> tuple[str | None, str | None, str | None]:
    recommendation = parsed.get("recommendation") if isinstance(parsed, dict) else None
    risk_level = parsed.get("risk_level") if isinstance(parsed, dict) else None
    confidence = parsed.get("confidence") if isinstance(parsed, dict) else None

    return (
        str(recommendation) if recommendation is not None else None,
        str(risk_level) if risk_level is not None else None,
        str(confidence) if confidence is not None else None,
    )


def _build_run_metadata(
    run_id: str,
    report_type: str,
    status: str,
    engine_input_mode: str,
    duration_seconds: float | None,
    error_message: str | None,
    stderr: str | None,
) -> dict[str, Any]:
    settings = get_settings()
    return {
        "run_id": run_id,
        "report_type": report_type,
        "status": status,
        "engine_input_mode": engine_input_mode,
        "created_at": utc_now().isoformat(),
        "duration_seconds": duration_seconds,
        "engine_path": str(settings.engine_path),
        "error": error_message,
        "stderr": stderr,
    }


def _save_run_artifacts(
    run_dir: Path,
    input_payload: dict[str, Any],
    raw_report: str,
    parsed_summary: dict[str, Any],
    metadata: dict[str, Any],
) -> None:
    ensure_directory(run_dir)
    write_json_file(run_dir / "input.json", input_payload)
    write_text_file(run_dir / "report.txt", raw_report)
    write_json_file(run_dir / "parsed.json", parsed_summary)
    write_json_file(run_dir / "metadata.json", metadata)


def _record_to_run_response(record: RunRecord) -> dict[str, Any]:
    parsed_summary = loads_or_default(record.parsed_json, {})
    visualization_data = parsed_summary.get("visualization_data") if isinstance(parsed_summary, dict) else None
    return {
        "run_id": record.run_id,
        "status": record.status,
        "report_type": record.report_type,
        "engine_input_mode": record.engine_input_mode,
        "parsed_summary": parsed_summary,
        "visualization_data": visualization_data,
        "raw_report": record.raw_report,
        "duration_seconds": record.duration_seconds,
        "created_at": record.created_at,
        "error": record.error_message,
        "stderr": None,
        "user_id": getattr(record, "user_id", None),
        "company_id": getattr(record, "company_id", None),
    }


def execute_engine_run(
    db: Session,
    report_type: str,
    input_payload: dict[str, Any] | None = None,
    drug_name: str | None = None,
    runs_override: int | None = None,
    engine_input_mode: str = "default_internal_engine_config",
    user_id: int | None = None,
    company_id: int | None = None,
) -> dict[str, Any]:
    settings = get_settings()

    run_id = sanitize_run_id(generate_run_id())
    created_at = utc_now()

    safe_input_payload = input_payload or {}
    run_dir = settings.runs_dir / run_id

    # Ensure run directory exists and save input.json so engine can consume it.
    ensure_directory(run_dir)
    write_json_file(run_dir / "input.json", safe_input_payload)

    env_overrides: dict[str, str] = {}
    if report_type == "dose-eval" and runs_override is not None:
        env_overrides["SPP_DOSE_EVAL_RUNS"] = str(runs_override)

    if report_type == "dose-eval" and isinstance(safe_input_payload, dict):
        channels = safe_input_payload.get("channels")
        dose_range = safe_input_payload.get("dose_range")

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

            # Current engine supports one global Hill value for dose-eval.
            if isinstance(na_cfg, dict) and isinstance(na_cfg.get("hill"), (int, float)):
                env_overrides["SPP_DOSE_EVAL_HILL"] = str(na_cfg["hill"])

        if isinstance(dose_range, dict):
            if isinstance(dose_range.get("min"), (int, float)):
                env_overrides["SPP_DOSE_EVAL_MIN"] = str(dose_range["min"])
            if isinstance(dose_range.get("max"), (int, float)):
                env_overrides["SPP_DOSE_EVAL_MAX"] = str(dose_range["max"])
            if isinstance(dose_range.get("step"), (int, float)):
                env_overrides["SPP_DOSE_EVAL_STEP"] = str(dose_range["step"])

    # If a payload was provided for dose-eval, treat input as user config.
    if report_type == "dose-eval" and isinstance(safe_input_payload, dict):
        engine_input_mode = "user_config"

    engine_result = run_engine(
        report_type,
        env_overrides=env_overrides or None,
        drug_config_path=str(run_dir / "input.json"),
        cwd=str(run_dir),
    )

    raw_report = engine_result.stdout or ""
    parsed_summary = _enforce_scientific_consistency(parse_report(report_type, raw_report))
    visualization_data = build_visualization_payload(report_type=report_type, input_payload=safe_input_payload, parsed_summary=parsed_summary)
    if visualization_data is not None:
        parsed_summary = {**parsed_summary, "visualization_data": visualization_data}
    recommendation, risk_level, confidence = _extract_decision_fields(parsed_summary)

    metadata = _build_run_metadata(
        run_id=run_id,
        report_type=report_type,
        status=engine_result.status,
        engine_input_mode=engine_input_mode,
        duration_seconds=engine_result.duration_seconds,
        error_message=engine_result.error,
        stderr=engine_result.stderr,
    )
    _save_run_artifacts(
        run_dir=run_dir,
        input_payload=safe_input_payload,
        raw_report=raw_report,
        parsed_summary=parsed_summary,
        metadata=metadata,
    )

    record = RunRecord(
        run_id=run_id,
        report_type=report_type,
        drug_name=drug_name,
        status=engine_result.status,
        engine_input_mode=engine_input_mode,
        # Link run to user/company for multi-tenant isolation
        user_id=user_id,
        company_id=company_id,
        recommendation=recommendation,
        risk_level=risk_level,
        confidence=confidence,
        raw_report=raw_report,
        parsed_json=json.dumps(parsed_summary),
        input_json=json.dumps(safe_input_payload),
        error_message=engine_result.error,
        created_at=created_at,
        duration_seconds=engine_result.duration_seconds,
    )

    db.add(record)
    db.commit()
    db.refresh(record)

    response = _record_to_run_response(record)

    if engine_result.status == "failed" and engine_result.error == "Engine execution failed":
        response["stderr"] = engine_result.stderr
    elif engine_result.status == "failed":
        response["stderr"] = engine_result.stderr or None

    return response


def list_runs(db: Session) -> list[dict[str, Any]]:
    stmt = select(RunRecord).order_by(RunRecord.created_at.desc())
    rows = db.execute(stmt).scalars().all()
    return [
        {
            "run_id": row.run_id,
            "report_type": row.report_type,
            "drug_name": row.drug_name,
            "status": row.status,
            "recommendation": row.recommendation,
            "risk_level": row.risk_level,
            "confidence": row.confidence,
            "created_at": row.created_at,
            "user_id": getattr(row, "user_id", None),
            "company_id": getattr(row, "company_id", None),
        }
        for row in rows
    ]


def get_run_or_none(db: Session, run_id: str) -> RunRecord | None:
    safe_run_id = sanitize_run_id(run_id)
    stmt = select(RunRecord).where(RunRecord.run_id == safe_run_id)
    return db.execute(stmt).scalar_one_or_none()


def delete_run(db: Session, run_id: str) -> bool:
    settings = get_settings()
    safe_run_id = sanitize_run_id(run_id)
    record = get_run_or_none(db, safe_run_id)
    if record is None:
        return False

    db.delete(record)
    db.commit()

    run_dir = settings.runs_dir / safe_run_id
    shutil.rmtree(run_dir, ignore_errors=True)
    return True


def get_run_detail_or_none(db: Session, run_id: str) -> dict[str, Any] | None:
    row = get_run_or_none(db, run_id)
    if row is None:
        return None

    parsed_summary = loads_or_default(row.parsed_json, {})
    visualization_data = parsed_summary.get("visualization_data") if isinstance(parsed_summary, dict) else None

    return {
        "run_id": row.run_id,
        "report_type": row.report_type,
        "drug_name": row.drug_name,
        "status": row.status,
        "engine_input_mode": row.engine_input_mode,
        "recommendation": row.recommendation,
        "risk_level": row.risk_level,
        "confidence": row.confidence,
        "raw_report": row.raw_report,
        "parsed_summary": parsed_summary,
        "visualization_data": visualization_data,
        "input_payload": loads_or_default(row.input_json, {}),
        "error_message": row.error_message,
        "duration_seconds": row.duration_seconds,
        "created_at": row.created_at,
        "user_id": getattr(row, "user_id", None),
        "company_id": getattr(row, "company_id", None),
    }
