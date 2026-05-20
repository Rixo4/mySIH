from __future__ import annotations

import re
from typing import Any


def _extract_labeled_value(text: str, label: str) -> str | None:
    pattern = re.compile(rf"^\s*{re.escape(label)}\s*:\s*(.+?)\s*$", re.MULTILINE)
    match = pattern.search(text)
    if not match:
        return None
    value = match.group(1).strip()
    return value or None


def _to_float(value: str | None) -> float | None:
    if value is None:
        return None
    number_match = re.search(r"[-+]?\d*\.?\d+", value)
    if not number_match:
        return None
    try:
        return float(number_match.group(0))
    except ValueError:
        return None


def _simulate_summary(raw_text: str) -> dict[str, Any]:
    return {
        "firing_rate": _to_float(_extract_labeled_value(raw_text, "Firing Rate")),
        "synchronization": _to_float(_extract_labeled_value(raw_text, "Synchronization")),
        "isi_variability": _extract_labeled_value(raw_text, "ISI Variability"),
        "seizure_score": _to_float(_extract_labeled_value(raw_text, "Seizure Score")),
        "seizure_risk": _extract_labeled_value(raw_text, "Seizure Risk"),
        "toxicity_score": _to_float(_extract_labeled_value(raw_text, "Toxicity Score")),
        "toxicity_risk": _extract_labeled_value(raw_text, "Toxicity Risk"),
        "recommendation": _extract_labeled_value(raw_text, "Recommendation"),
        "risk_level": _extract_labeled_value(raw_text, "Risk Level"),
        "confidence": _extract_labeled_value(raw_text, "Confidence"),
    }


def _dose_eval_summary(raw_text: str) -> dict[str, Any]:
    model_fit = _extract_labeled_value(raw_text, "Model Fit (R^2)")
    max_effect = _extract_labeled_value(raw_text, "Max Effect")
    effective_range = _extract_labeled_value(raw_text, "Effective Range")
    stabilization_range = _extract_labeled_value(raw_text, "Stabilization Range")
    response_mode = _extract_labeled_value(raw_text, "Response Mode")

    return {
        # Drug input section
        "drug_name": _extract_labeled_value(raw_text, "Drug Name"),
        "engine_input_mode": _extract_labeled_value(raw_text, "Engine Input Mode"),
        "na_ic50": _to_float(_extract_labeled_value(raw_text, "Na IC50")),
        "k_ic50": _to_float(_extract_labeled_value(raw_text, "K IC50")),
        "ca_ic50": _to_float(_extract_labeled_value(raw_text, "Ca IC50")),
        "hill": _to_float(_extract_labeled_value(raw_text, "Hill")),
        "runs": _to_float(_extract_labeled_value(raw_text, "Runs")),
        "curve_type": _extract_labeled_value(raw_text, "Curve Type"),
        "response_mode": response_mode,
        "model_fit_r2": _to_float(model_fit),
        "max_effect": _to_float(max_effect),
        "response_strength": _extract_labeled_value(raw_text, "Response Strength"),
        "toxic_threshold": _extract_labeled_value(raw_text, "Toxic Threshold"),
        "effective_range": effective_range or stabilization_range,
        "stabilization_range": stabilization_range,
        "stability_score": _extract_labeled_value(raw_text, "Stability Score"),
        "recommendation": _extract_labeled_value(raw_text, "Recommendation"),
        "risk_level": _extract_labeled_value(raw_text, "Risk Level"),
        "confidence": _extract_labeled_value(raw_text, "Confidence"),
        "reason": _extract_labeled_value(raw_text, "Reason"),
        "sync_reduction_pct": _to_float(_extract_labeled_value(raw_text, "Sync Reduction")),
        "nii_reduction_pct": _to_float(_extract_labeled_value(raw_text, "NII Reduction")),
        "nii_increase_pct": _to_float(_extract_labeled_value(raw_text, "NII Increase")),
        "seizure_reduction_pct": _to_float(_extract_labeled_value(raw_text, "Seizure Reduction")),
        "burst_reduction_pct": _to_float(_extract_labeled_value(raw_text, "Burst Reduction")),
        "calcium_effect_magnitude": _to_float(_extract_labeled_value(raw_text, "Calcium Effect")),
    }


def _validate_summary(raw_text: str) -> dict[str, Any]:
    return {
        "tests_passed": _extract_labeled_value(raw_text, "Tests Passed"),
        "system_status": _extract_labeled_value(raw_text, "System Status"),
        "performance": _extract_labeled_value(raw_text, "Performance"),
    }


def parse_report(report_type: str, raw_text: str) -> dict[str, Any]:
    if not raw_text:
        return {}

    parser_map = {
        "simulate": _simulate_summary,
        "dose-eval": _dose_eval_summary,
        "validate": _validate_summary,
    }

    parser = parser_map.get(report_type)
    if parser is None:
        return {}

    try:
        return parser(raw_text)
    except Exception:
        # Parsing must never break API execution paths.
        return {}
