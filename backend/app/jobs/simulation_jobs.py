from __future__ import annotations

import time
import uuid
from typing import Any

from ..database import SessionLocal
from ..orchestration import run_scientific_simulation_job


def run_simulation_job(payload):
    print("===================================")
    print("STARTING SIMULATION JOB")
    print("Payload:", payload)
    print("===================================")

    # temporary infrastructure validation
    time.sleep(10)

    print("===================================")
    print("SIMULATION COMPLETED")
    print("===================================")

    return {
        "status": "COMPLETED"
    }


def run_engine_simulation_job(job_payload: dict[str, Any]) -> dict[str, Any]:
    db = SessionLocal()
    try:
        simulation_job_id_raw = job_payload.get("simulation_job_id")
        if simulation_job_id_raw is None:
            raise ValueError("simulation_job_id is required")
        simulation_job_id = uuid.UUID(str(simulation_job_id_raw))

        input_payload = job_payload.get("input_payload")
        if not isinstance(input_payload, dict):
            input_payload = {}

        report_type = str(job_payload.get("report_type", "simulate"))
        drug_name = job_payload.get("drug_name")
        engine_input_mode = str(job_payload.get("engine_input_mode", "default_internal_engine_config"))
        user_id = job_payload.get("user_id")
        company_id = job_payload.get("company_id")
        runs_override = job_payload.get("runs_override")

        return run_scientific_simulation_job(
            db=db,
            simulation_job_id=simulation_job_id,
            report_type=report_type,
            input_payload=input_payload,
            drug_name=str(drug_name) if drug_name is not None else None,
            runs_override=int(runs_override) if isinstance(runs_override, int) else None,
            engine_input_mode=engine_input_mode,
            user_id=int(user_id) if isinstance(user_id, int) else None,
            company_id=int(company_id) if isinstance(company_id, int) else None,
        )
    finally:
        db.close()
