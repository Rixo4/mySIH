from __future__ import annotations

import re
from typing import Any


_ABSENT_TOKENS = {"not observed", "n/a", "na", "none", "null", "undefined", "-", "—"}


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


def _extract_first_present_label(raw_text: str, labels: list[str]) -> str | None:
    for label in labels:
        value = _extract_labeled_value(raw_text, label)
        if value is not None:
            return value
    return None


def _is_absent(value: str | None) -> bool:
    if value is None:
        return True
    return value.strip().lower() in _ABSENT_TOKENS


def _extract_range(value: str | None) -> tuple[float, float] | None:
    if _is_absent(value):
        return None
    if value is None:
        return None
    match = re.search(r"([-+]?\d*\.?\d+)\s*(?:-|to|–)\s*([-+]?\d*\.?\d+)", value, re.IGNORECASE)
    if not match:
        return None
    try:
        left = float(match.group(1))
        right = float(match.group(2))
    except ValueError:
        return None
    return (left, right) if left <= right else (right, left)


def _derive_active_zone(
    *,
    response_mode: str | None,
    recommendation: str | None,
    effective_range: str | None,
    therapeutic_zone: str | None,
    over_suppression_zone: str | None,
    severe_excitability_zone: str | None,
    saturated_stabilization_zone: str | None,
) -> tuple[str, bool]:
    normalized_mode = (response_mode or "NO_SIGNIFICANT_RESPONSE").strip().upper()
    normalized_recommendation = (recommendation or "").strip().upper()

    has_effective_range = not _is_absent(effective_range)
    has_therapeutic_zone = not _is_absent(therapeutic_zone)
    has_valid_therapeutic_window = has_effective_range and has_therapeutic_zone

    toxicity_dominant = "NOT RECOMMENDED" in normalized_recommendation

    if not has_valid_therapeutic_window or toxicity_dominant:
        if normalized_mode == "SUPPRESSIVE_RESPONSE" and not _is_absent(over_suppression_zone):
            return "OVER_SUPPRESSION_ZONE", has_valid_therapeutic_window
        if normalized_mode == "EXCITATORY_RESPONSE" and not _is_absent(severe_excitability_zone):
            return "SEVERE_EXCITABILITY_ZONE", has_valid_therapeutic_window
        if normalized_mode == "STABILIZING_RESPONSE" and not _is_absent(saturated_stabilization_zone):
            return "SATURATED_STABILIZATION_ZONE", has_valid_therapeutic_window
        return "NO_VALID_WINDOW", has_valid_therapeutic_window

    if normalized_mode == "STABILIZING_RESPONSE":
        return "STABILIZATION_ZONE", has_valid_therapeutic_window
    if normalized_mode == "EXCITATORY_RESPONSE":
        return "EXCITATORY_ZONE", has_valid_therapeutic_window
    if normalized_mode == "NO_SIGNIFICANT_RESPONSE":
        return "NO_VALID_WINDOW", has_valid_therapeutic_window
    return "THERAPEUTIC_ZONE", has_valid_therapeutic_window


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
    recommendation = _extract_labeled_value(raw_text, "Recommendation")

    ineffective_zone = _extract_labeled_value(raw_text, "Ineffective Zone")
    therapeutic_zone = _extract_first_present_label(raw_text, ["Therapeutic Zone", "Stabilization Zone", "Excitatory Zone"])
    over_suppression_zone = _extract_labeled_value(raw_text, "Over-Suppression")
    severe_excitability_zone = _extract_labeled_value(raw_text, "Severe Excitability Zone")
    saturated_stabilization_zone = _extract_labeled_value(raw_text, "Saturated Stabilization Zone")

    active_zone, has_valid_therapeutic_window = _derive_active_zone(
        response_mode=response_mode,
        recommendation=recommendation,
        effective_range=effective_range or stabilization_range,
        therapeutic_zone=therapeutic_zone,
        over_suppression_zone=over_suppression_zone,
        severe_excitability_zone=severe_excitability_zone,
        saturated_stabilization_zone=saturated_stabilization_zone,
    )

    therapeutic_range = _extract_range(therapeutic_zone)
    ineffective_range = _extract_range(ineffective_zone)
    over_suppression_range = _extract_range(over_suppression_zone)
    severe_excitability_range = _extract_range(severe_excitability_zone)
    saturated_stabilization_range = _extract_range(saturated_stabilization_zone)

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
        "recommendation": recommendation,
        "risk_level": _extract_labeled_value(raw_text, "Risk Level"),
        "confidence": _extract_labeled_value(raw_text, "Confidence"),
        "reason": _extract_labeled_value(raw_text, "Reason"),
        "onset_dose": _extract_labeled_value(raw_text, "Onset Dose"),
        "peak_biological_effect_dose": _extract_first_present_label(raw_text, ["Peak Biological Effect", "Peak Pharmacodynamic Effect", "Peak Efficiency"]),
        "saturation_trend": _extract_labeled_value(raw_text, "Saturation Trend"),
        "ineffective_zone": ineffective_zone,
        "therapeutic_zone": therapeutic_zone,
        "over_suppression_zone": over_suppression_zone,
        "severe_excitability_zone": severe_excitability_zone,
        "saturated_stabilization_zone": saturated_stabilization_zone,
        "has_valid_therapeutic_window": has_valid_therapeutic_window,
        "active_zone": active_zone,
        "zone_ranges": {
            "ineffective_zone": list(ineffective_range) if ineffective_range is not None else None,
            "therapeutic_zone": list(therapeutic_range) if therapeutic_range is not None else None,
            "over_suppression_zone": list(over_suppression_range) if over_suppression_range is not None else None,
            "severe_excitability_zone": list(severe_excitability_range) if severe_excitability_range is not None else None,
            "saturated_stabilization_zone": list(saturated_stabilization_range) if saturated_stabilization_range is not None else None,
        },
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
