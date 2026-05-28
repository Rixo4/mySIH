from __future__ import annotations

import re
from typing import Any


_ABSENT_TOKENS = {"not observed", "n/a", "na", "none", "null", "undefined", "-", "—"}


def _safe_float(value: Any, default: float) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return default
    if number != number:
        return default
    return number


def _extract_dose_range(input_payload: dict[str, Any]) -> tuple[float, float, float]:
    dose_range = input_payload.get("dose_range") if isinstance(input_payload, dict) else None
    if not isinstance(dose_range, dict):
        return 0.0, 20.0, 2.0

    min_dose = _safe_float(dose_range.get("min"), 0.0)
    max_dose = _safe_float(dose_range.get("max"), 20.0)
    step = _safe_float(dose_range.get("step"), 2.0)

    if max_dose <= min_dose:
        max_dose = min_dose + 20.0
    if step <= 0:
        step = 2.0

    return min_dose, max_dose, step


def _extract_channel_inputs(input_payload: dict[str, Any]) -> dict[str, dict[str, float]]:
    channels = input_payload.get("channels") if isinstance(input_payload, dict) else None
    defaults = {
        "Na": {"ic50": 200.0, "hill": 3.2},
        "K": {"ic50": 8.0, "hill": 3.2},
        "Ca": {"ic50": 1000.0, "hill": 3.2},
    }

    if not isinstance(channels, dict):
        return defaults

    result: dict[str, dict[str, float]] = {}
    for key, fallback in defaults.items():
        channel = channels.get(key)
        if isinstance(channel, dict):
            result[key] = {
                "ic50": _safe_float(channel.get("ic50"), fallback["ic50"]),
                "hill": _safe_float(channel.get("hill"), fallback["hill"]),
            }
        else:
            result[key] = fallback
    return result


def _parse_number(value: Any, default: float) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    if not isinstance(value, str):
        return default

    for token in value.replace(",", " ").split():
        try:
            return float(token)
        except ValueError:
            continue
    return default


def _is_absent(value: Any) -> bool:
    if value is None:
        return True
    if isinstance(value, str):
        return value.strip().lower() in _ABSENT_TOKENS
    return False


def _parse_range_text(value: Any) -> tuple[float, float] | None:
    if _is_absent(value):
        return None
    if not isinstance(value, str):
        return None
    text = value.replace("to", "-").replace("–", "-")
    tokens = [token.strip() for token in text.split("-") if token.strip()]
    if len(tokens) < 2:
        return None
    try:
        left = float(tokens[0])
        right = float(tokens[1])
    except ValueError:
        return None
    return (left, right) if left <= right else (right, left)


def _coerce_range(value: Any) -> tuple[float, float] | None:
    if isinstance(value, (list, tuple)) and len(value) == 2:
        try:
            left = float(value[0])
            right = float(value[1])
        except (TypeError, ValueError):
            return None
        return (left, right) if left <= right else (right, left)
    return _parse_range_text(value)


def _read_zone_ranges(parsed_summary: dict[str, Any]) -> dict[str, tuple[float, float] | None]:
    zone_ranges = parsed_summary.get("zone_ranges") if isinstance(parsed_summary.get("zone_ranges"), dict) else {}
    return {
        "ineffective_zone": _coerce_range(zone_ranges.get("ineffective_zone") if isinstance(zone_ranges, dict) else None)
        or _parse_range_text(parsed_summary.get("ineffective_zone")),
        "therapeutic_zone": _coerce_range(zone_ranges.get("therapeutic_zone") if isinstance(zone_ranges, dict) else None)
        or _parse_range_text(parsed_summary.get("therapeutic_zone")),
        "over_suppression_zone": _coerce_range(zone_ranges.get("over_suppression_zone") if isinstance(zone_ranges, dict) else None)
        or _parse_range_text(parsed_summary.get("over_suppression_zone")),
        "severe_excitability_zone": _coerce_range(zone_ranges.get("severe_excitability_zone") if isinstance(zone_ranges, dict) else None)
        or _parse_range_text(parsed_summary.get("severe_excitability_zone")),
        "saturated_stabilization_zone": _coerce_range(zone_ranges.get("saturated_stabilization_zone") if isinstance(zone_ranges, dict) else None)
        or _parse_range_text(parsed_summary.get("saturated_stabilization_zone")),
    }


def _normalize_zone_name(value: str | None, response_mode: str) -> str:
    if not value:
        return "NONE"

    normalized = value.strip().upper()
    if normalized in {"THERAPEUTIC_ZONE", "THERAPEUTIC"}:
        return "THERAPEUTIC"
    if normalized in {"STABILIZATION_ZONE", "STABILIZING_ZONE", "STABILIZING_RESPONSE", "STABILIZING"}:
        return "STABILIZING"
    if normalized in {"OVER_SUPPRESSION_ZONE", "OVER_SUPPRESSION"}:
        return "OVER_SUPPRESSION"
    if normalized in {"EXCITATORY_ZONE", "SEVERE_EXCITABILITY_ZONE", "EXCITATORY"}:
        return "EXCITATORY"
    if normalized in {"TOXIC", "TOXIC_ZONE", "TOXICITY"}:
        return "TOXIC"
    if normalized in {"INEFFECTIVE_ZONE", "INEFFECTIVE"}:
        return "INEFFECTIVE"
    if normalized == "NO_VALID_WINDOW":
        return "NONE"
    if normalized == "NO_SIGNIFICANT_RESPONSE":
        return "NONE"
    if response_mode == "STABILIZING_RESPONSE":
        return "STABILIZING"
    return "NONE"


def _zone_object(interval: tuple[float, float] | None) -> dict[str, float] | None:
    if interval is None:
        return None
    start, end = interval
    if end <= start:
        return None
    return {"start": round(start, 2), "end": round(end, 2)}


def _number_from_text(value: Any) -> float | None:
    if isinstance(value, (int, float)):
        number = float(value)
        return number if number == number else None
    if isinstance(value, str):
        match = re.search(r"[-+]?\d*\.?\d+", value)
        if match:
            try:
                return float(match.group(0))
            except ValueError:
                return None
    return None


def _build_zone_contract(
    *,
    zones: dict[str, tuple[float, float] | None],
    response_mode: str,
    max_effect: float | None,
    response_strength: str | None,
    toxicity_observed: bool,
    saturation_trend: Any,
) -> dict[str, Any]:
    low_response = bool(max_effect is not None and max_effect < 20.0)
    response_strength_text = (response_strength or "").strip().upper()
    response_strength_none = response_strength_text in {"NONE", "NOT OBSERVED", "NO RESPONSE"}
    no_significant_response = low_response and response_strength_none

    ineffective = _zone_object(zones["ineffective_zone"]) or {"start": 0.0, "end": 0.0}
    therapeutic = None
    over_suppression = None
    excitatory = None
    toxic = None

    if not (low_response or response_strength_none):
        therapeutic = _zone_object(zones["therapeutic_zone"])
        over_suppression = _zone_object(zones["over_suppression_zone"])
        excitatory = _zone_object(zones["severe_excitability_zone"])
        toxic = _zone_object(zones["saturated_stabilization_zone"])

        if response_mode == "SUPPRESSIVE_RESPONSE" and over_suppression is None:
            over_suppression = None
        if response_mode == "EXCITATORY_RESPONSE" and excitatory is None:
            excitatory = None
        if response_mode == "STABILIZING_RESPONSE" and therapeutic is None:
            therapeutic = None

    if not toxicity_observed:
        toxic = None

    active_zone = _normalize_zone_name(
        None if low_response or response_strength_none else (
            "THERAPEUTIC_ZONE" if therapeutic is not None and response_mode == "SUPPRESSIVE_RESPONSE"
            else "STABILIZATION_ZONE" if therapeutic is not None and response_mode == "STABILIZING_RESPONSE"
            else "EXCITATORY_ZONE" if excitatory is not None and response_mode == "EXCITATORY_RESPONSE"
            else "OVER_SUPPRESSION_ZONE" if over_suppression is not None and response_mode == "SUPPRESSIVE_RESPONSE"
            else "TOXIC" if toxic is not None
            else None
        ),
        response_mode,
    )

    if low_response or response_strength_none:
        active_zone = "NONE"
    if no_significant_response:
        active_zone = "NONE"

    zones_payload = {
        "ineffective": ineffective,
        "therapeutic": therapeutic,
        "over_suppression": over_suppression,
        "excitatory": excitatory,
        "toxic": toxic,
    }

    onset = None
    if not low_response and therapeutic is not None:
        onset = therapeutic["start"]
    elif not low_response and response_mode == "SUPPRESSIVE_RESPONSE" and over_suppression is not None:
        onset = over_suppression["start"]
    elif not low_response and response_mode == "EXCITATORY_RESPONSE" and excitatory is not None:
        onset = excitatory["start"]

    saturation = None
    toxic_threshold = None
    if toxicity_observed:
        toxic_source = zones["over_suppression_zone"] or zones["severe_excitability_zone"] or zones["saturated_stabilization_zone"]
        toxic_threshold = toxic_source[0] if toxic_source is not None else None
    saturation = _number_from_text(saturation_trend)

    thresholds = {
        "onset": round(onset, 2) if onset is not None else None,
        "toxic": round(toxic_threshold, 2) if toxic_threshold is not None else None,
        "saturation": round(saturation, 2) if saturation is not None else None,
    }

    integrity_warnings: list[str] = []
    if not toxicity_observed and thresholds["toxic"] is not None:
        integrity_warnings.append("toxic_threshold_hidden_due_to_absent_toxicity")
        thresholds["toxic"] = None
    if low_response and any(zones_payload[key] is not None for key in ("therapeutic", "over_suppression", "excitatory", "toxic")):
        integrity_warnings.append("low_response_complex_zones_suppressed")
        zones_payload["therapeutic"] = None
        zones_payload["over_suppression"] = None
        zones_payload["excitatory"] = None
        zones_payload["toxic"] = None
        active_zone = "NONE"
        thresholds["onset"] = None
        thresholds["toxic"] = None
        thresholds["saturation"] = None

    return {
        "response_mode": response_mode,
        "active_zone": active_zone,
        "toxicity_observed": toxicity_observed,
        "zones": zones_payload,
        "thresholds": thresholds,
        "integrity_warnings": integrity_warnings,
    }


def _derive_active_zone(
    *,
    parsed_summary: dict[str, Any],
    response_mode: str,
    has_valid_therapeutic_window: bool,
    zones: dict[str, tuple[float, float] | None],
) -> str:
    summary_active_zone = parsed_summary.get("active_zone")
    if isinstance(summary_active_zone, str) and summary_active_zone.strip():
        active = summary_active_zone.strip().upper()
    else:
        active = ""

    recommendation = str(parsed_summary.get("recommendation") or "").upper()
    toxicity_dominant = "NOT RECOMMENDED" in recommendation

    if has_valid_therapeutic_window and not toxicity_dominant:
        if active in {"THERAPEUTIC_ZONE", "STABILIZATION_ZONE", "EXCITATORY_ZONE"}:
            return active
        if response_mode == "STABILIZING_RESPONSE":
            return "STABILIZATION_ZONE"
        if response_mode == "EXCITATORY_RESPONSE":
            return "EXCITATORY_ZONE"
        return "THERAPEUTIC_ZONE"

    if response_mode == "SUPPRESSIVE_RESPONSE" and zones["over_suppression_zone"] is not None:
        return "OVER_SUPPRESSION_ZONE"
    if response_mode == "EXCITATORY_RESPONSE" and zones["severe_excitability_zone"] is not None:
        return "SEVERE_EXCITABILITY_ZONE"
    if response_mode == "STABILIZING_RESPONSE" and zones["saturated_stabilization_zone"] is not None:
        return "SATURATED_STABILIZATION_ZONE"
    return "NO_VALID_WINDOW"


def _state_for_dose(dose: float, zones: dict[str, tuple[float, float] | None], response_mode: str) -> str:
    for key, state in (
        ("ineffective_zone", "INEFFECTIVE_ZONE"),
        ("therapeutic_zone", "STABILIZATION_ZONE" if response_mode == "STABILIZING_RESPONSE" else "THERAPEUTIC_ZONE"),
        ("over_suppression_zone", "OVER_SUPPRESSION_ZONE"),
        ("severe_excitability_zone", "SEVERE_EXCITABILITY_ZONE"),
        ("saturated_stabilization_zone", "SATURATED_STABILIZATION_ZONE"),
    ):
        interval = zones.get(key)
        if interval is None:
            continue
        left, right = interval
        if left <= dose <= right:
            return state
    if response_mode == "EXCITATORY_RESPONSE":
        return "LIMITED_EFFECT"
    if response_mode == "STABILIZING_RESPONSE":
        return "LIMITED_EFFECT"
    return "LIMITED_EFFECT"


def _build_timeline(zones: dict[str, tuple[float, float] | None], response_mode: str) -> list[dict[str, Any]]:
    segments: list[dict[str, Any]] = []

    def add(label: str, state: str, key: str, color: str) -> None:
        interval = zones.get(key)
        if interval is None:
            return
        left, right = interval
        if right <= left:
            return
        segments.append({"label": label, "state": state, "from": round(left, 2), "to": round(right, 2), "color": color})

    add("Ineffective Zone", "INEFFECTIVE_ZONE", "ineffective_zone", "#475569")
    if response_mode == "NO_SIGNIFICANT_RESPONSE":
        return segments
    if response_mode == "STABILIZING_RESPONSE":
        add("Stabilization Zone", "STABILIZATION_ZONE", "therapeutic_zone", "#06b6d4")
    elif response_mode == "EXCITATORY_RESPONSE":
        add("Excitatory Zone", "EXCITATORY_ZONE", "therapeutic_zone", "#f59e0b")
    else:
        add("Therapeutic Zone", "THERAPEUTIC_ZONE", "therapeutic_zone", "#10b981")
    add("Over-Suppression Zone", "OVER_SUPPRESSION_ZONE", "over_suppression_zone", "#6366f1")
    add("Severe Excitability Zone", "SEVERE_EXCITABILITY_ZONE", "severe_excitability_zone", "#ef4444")
    add("Saturated Stabilization Zone", "SATURATED_STABILIZATION_ZONE", "saturated_stabilization_zone", "#8b5cf6")
    return segments


def _mode_profile(response_mode: str, dose: float, midpoint: float, toxic_threshold: float) -> dict[str, float]:
    normalized = response_mode.upper()
    if normalized == "NO_SIGNIFICANT_RESPONSE":
        flat_effect = 7.0 + 0.25 * pow(2.718281828, -((dose - midpoint) ** 2) / (2 * max(1.0, midpoint * 0.65) ** 2))
        firing = max(16.0, 18.0 - dose * 0.08)
        sync = max(0.22, 0.28 - (dose * 0.0025))
        nii = max(0.08, 0.12 - (dose * 0.0018))
        seizure = max(2.8, 3.6 - dose * 0.03)
        variance = max(0.18, 0.28 - (dose / max(1.0, toxic_threshold)) * 0.04)
        return {
            "effect": round(max(0.0, min(20.0, flat_effect)), 1),
            "firing_rate": round(max(0.0, firing), 2),
            "sync": round(max(0.0, min(1.0, sync)), 3),
            "nii": round(max(0.0, min(1.0, nii)), 3),
            "seizure_score": round(max(0.0, seizure), 2),
            "toxicity_score": round(max(0.0, 2.0 + max(0.0, dose - toxic_threshold) * 0.05), 2),
            "variance": round(max(0.05, variance), 3),
        }
    if normalized == "EXCITATORY_RESPONSE":
        effect = 20 + 78 / (1 + pow(2.718281828, -0.55 * (dose - midpoint)))
        firing = 16 + (dose * 2.2)
        sync = max(0.15, 0.28 + (dose - midpoint) * 0.022)
        nii = max(0.05, 0.12 + (dose - midpoint) * 0.028)
        seizure = 10 + max(0.0, (dose - midpoint) * 5.4)
        variance = 1.4 + (dose / max(1.0, toxic_threshold)) * 1.8
    elif normalized == "STABILIZING_RESPONSE":
        effect = 58 + 18 * pow(2.718281828, -((dose - midpoint) ** 2) / (2 * max(1.0, midpoint * 0.45) ** 2))
        firing = max(6.0, 22 - dose * 0.55)
        sync = max(0.04, 0.34 - (dose * 0.016))
        nii = max(0.03, 0.24 - (dose * 0.014))
        seizure = max(1.5, 5.5 - dose * 0.22)
        variance = max(0.25, 1.0 - (dose / max(1.0, toxic_threshold * 1.2)) * 0.6)
    else:
        effect = 78 / (1 + pow(2.718281828, 0.5 * (dose - midpoint)))
        firing = max(4.0, 20 - dose * 1.1)
        sync = max(0.03, 0.26 - (dose * 0.012))
        nii = max(0.02, 0.14 - (dose * 0.01))
        seizure = max(0.8, 4.5 - dose * 0.18 - max(0.0, dose - toxic_threshold) * 0.12)
        variance = max(0.22, 0.95 - (dose / max(1.0, toxic_threshold)) * 0.45)

    if normalized == "EXCITATORY_RESPONSE" and dose >= toxic_threshold:
        seizure += (dose - toxic_threshold) * 3.5
        variance += 0.65

    return {
        "effect": round(max(0.0, min(100.0, effect)), 1),
        "firing_rate": round(max(0.0, firing), 2),
        "sync": round(max(0.0, min(1.0, sync)), 3),
        "nii": round(max(0.0, min(1.0, nii)), 3),
        "seizure_score": round(max(0.0, seizure), 2),
        "toxicity_score": round(max(0.0, 2.0 + max(0.0, dose - toxic_threshold) * 2.8), 2),
        "variance": round(max(0.05, variance), 3),
    }


def _make_voltage_trace(response_mode: str, dose: float, sample_count: int = 240) -> list[dict[str, float]]:
    normalized = response_mode.upper()
    trace: list[dict[str, float]] = []
    base = -67.0 if normalized == "NO_SIGNIFICANT_RESPONSE" else (-64.0 if normalized == "EXCITATORY_RESPONSE" else -68.0)
    amplitude = 42.0 if normalized == "SUPPRESSIVE_RESPONSE" else 56.0
    spike_period = 38 if normalized == "EXCITATORY_RESPONSE" else 54
    suppression = 0.9 if normalized == "SUPPRESSIVE_RESPONSE" else 0.65
    if normalized == "NO_SIGNIFICANT_RESPONSE":
        amplitude = 9.0
        spike_period = 64
        suppression = 0.98

    for index in range(sample_count):
        time = index * 0.75
        phase = index % spike_period
        spike = max(0.0, 1.0 - abs(phase - spike_period / 2) / (spike_period / 2))
        modulation = 1.0 + (dose * 0.012)
        if normalized == "SUPPRESSIVE_RESPONSE":
            modulation *= suppression
        if normalized == "STABILIZING_RESPONSE":
            modulation *= 0.72
        if normalized == "NO_SIGNIFICANT_RESPONSE":
            modulation *= suppression

        voltage = base + amplitude * spike * modulation
        if normalized == "STABILIZING_RESPONSE":
            voltage = min(-35.0, voltage + 4.0)
        if normalized == "NO_SIGNIFICANT_RESPONSE":
            voltage = min(-50.0, max(-74.0, voltage))
        trace.append({"time": round(time, 2), "voltage": round(voltage, 2)})
    return trace


def _make_raster_spikes(response_mode: str, dose: float, neuron_count: int = 18) -> list[dict[str, float]]:
    normalized = response_mode.upper()
    spikes: list[dict[str, float]] = []
    base_interval = 14.0 if normalized == "EXCITATORY_RESPONSE" else 24.0
    if normalized == "NO_SIGNIFICANT_RESPONSE":
        base_interval = 28.0
    interval = max(6.0, base_interval - dose * (0.6 if normalized == "SUPPRESSIVE_RESPONSE" else 0.35 if normalized == "EXCITATORY_RESPONSE" else 0.12))

    for neuron_id in range(1, neuron_count + 1):
        offset = (neuron_id * 1.7) % interval
        for spike_index in range(7):
            spike_time = round(offset + spike_index * interval, 2)
            spikes.append({"neuron_id": neuron_id, "spike_time": spike_time})
    return spikes


def build_visualization_payload(
    *,
    report_type: str,
    input_payload: dict[str, Any],
    parsed_summary: dict[str, Any],
) -> dict[str, Any] | None:
    if report_type != "dose-eval" or not isinstance(input_payload, dict):
        return None
    visualization_data = parsed_summary.get("visualization_data")
    if isinstance(visualization_data, dict):
        return visualization_data

    return None