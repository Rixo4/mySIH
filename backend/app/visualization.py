from __future__ import annotations

from typing import Any


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


def _mode_profile(response_mode: str, dose: float, midpoint: float, toxic_threshold: float) -> dict[str, float]:
    normalized = response_mode.upper()
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
        seizure = max(1.0, 4.5 - dose * 0.18)
        variance = max(0.22, 0.95 - (dose / max(1.0, toxic_threshold)) * 0.45)

    if dose >= toxic_threshold:
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


def _biological_state(dose: float, therapeutic_min: float, therapeutic_max: float, toxic_threshold: float, response_mode: str) -> str:
    normalized = response_mode.upper()
    if dose < therapeutic_min:
        return "INEFFECTIVE_ZONE"
    if therapeutic_min <= dose <= therapeutic_max:
        return "STABILIZATION_ZONE" if normalized == "STABILIZING_RESPONSE" else "THERAPEUTIC_ZONE"
    if dose < toxic_threshold:
        return "EXCITATORY_ZONE" if normalized == "EXCITATORY_RESPONSE" else "LIMITED_EFFECT"
    if dose < toxic_threshold * 1.35:
        return "OVER_SUPPRESSION_ZONE" if normalized == "SUPPRESSIVE_RESPONSE" else "SEVERE_EXCITABILITY_ZONE"
    return "SEVERE_EXCITABILITY_ZONE"


def _make_voltage_trace(response_mode: str, dose: float, sample_count: int = 240) -> list[dict[str, float]]:
    normalized = response_mode.upper()
    trace: list[dict[str, float]] = []
    base = -68.0 if normalized != "EXCITATORY_RESPONSE" else -64.0
    amplitude = 42.0 if normalized == "SUPPRESSIVE_RESPONSE" else 56.0
    spike_period = 38 if normalized == "EXCITATORY_RESPONSE" else 54
    suppression = 0.9 if normalized == "SUPPRESSIVE_RESPONSE" else 0.65

    for index in range(sample_count):
        time = index * 0.75
        phase = index % spike_period
        spike = max(0.0, 1.0 - abs(phase - spike_period / 2) / (spike_period / 2))
        modulation = 1.0 + (dose * 0.012)
        if normalized == "SUPPRESSIVE_RESPONSE":
            modulation *= suppression
        if normalized == "STABILIZING_RESPONSE":
            modulation *= 0.72

        voltage = base + amplitude * spike * modulation
        if normalized == "STABILIZING_RESPONSE":
            voltage = min(-35.0, voltage + 4.0)
        trace.append({"time": round(time, 2), "voltage": round(voltage, 2)})
    return trace


def _make_raster_spikes(response_mode: str, dose: float, neuron_count: int = 18) -> list[dict[str, float]]:
    normalized = response_mode.upper()
    spikes: list[dict[str, float]] = []
    base_interval = 14.0 if normalized == "EXCITATORY_RESPONSE" else 24.0
    interval = max(6.0, base_interval - dose * (0.6 if normalized == "SUPPRESSIVE_RESPONSE" else 0.35))

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

    min_dose, max_dose, step = _extract_dose_range(input_payload)
    channels = _extract_channel_inputs(input_payload)
    response_mode = str(parsed_summary.get("response_mode") or "SUPPRESSIVE_RESPONSE").upper()
    therapeutic_range_text = parsed_summary.get("effective_range") or parsed_summary.get("stabilization_range")
    toxic_threshold_text = parsed_summary.get("toxic_threshold")

    therapeutic_min = min_dose + (max_dose - min_dose) * 0.25
    therapeutic_max = min_dose + (max_dose - min_dose) * 0.6
    if isinstance(therapeutic_range_text, str) and "-" in therapeutic_range_text:
        try:
            left, right = therapeutic_range_text.split("-", 1)
            therapeutic_min = float(left.strip())
            therapeutic_max = float(right.strip())
        except ValueError:
            pass

    toxic_threshold = _parse_number(toxic_threshold_text, max_dose * 0.85)
    midpoint = (min_dose + max_dose) / 2
    count = max(1, int(round((max_dose - min_dose) / step)))

    dose_results: list[dict[str, Any]] = []
    for index in range(count + 1):
        dose = round(min_dose + index * step, 2)
        profile = _mode_profile(response_mode, dose, midpoint, toxic_threshold)
        dose_results.append(
            {
                "dose": dose,
                **profile,
                "response_mode": response_mode,
                "biological_state": _biological_state(dose, therapeutic_min, therapeutic_max, toxic_threshold, response_mode),
                "ic50_na": round(channels["Na"]["ic50"], 2),
                "ic50_k": round(channels["K"]["ic50"], 2),
                "ic50_ca": round(channels["Ca"]["ic50"], 2),
            }
        )

    return {
        "dose_results": dose_results,
        "voltage_trace": _make_voltage_trace(response_mode, midpoint),
        "raster_spikes": _make_raster_spikes(response_mode, midpoint),
        "classification_timeline": [
            {"label": "Ineffective Zone", "state": "INEFFECTIVE_ZONE", "from": min_dose, "to": therapeutic_min, "color": "#475569"},
            {"label": "Therapeutic Zone", "state": "THERAPEUTIC_ZONE", "from": therapeutic_min, "to": therapeutic_max, "color": "#10b981"},
            {"label": "Excitatory Zone", "state": "EXCITATORY_ZONE", "from": therapeutic_max, "to": toxic_threshold, "color": "#f59e0b"},
            {"label": "Stabilization Zone", "state": "STABILIZATION_ZONE", "from": therapeutic_min, "to": therapeutic_max, "color": "#06b6d4"},
            {"label": "Over-Suppression Zone", "state": "OVER_SUPPRESSION_ZONE", "from": toxic_threshold, "to": toxic_threshold * 1.15, "color": "#6366f1"},
            {"label": "Severe Excitability Zone", "state": "SEVERE_EXCITABILITY_ZONE", "from": toxic_threshold * 1.15, "to": max_dose, "color": "#ef4444"},
        ],
        "reference_points": {
            "ic50": round(channels["Na"]["ic50"], 2),
            "toxic_threshold": round(toxic_threshold, 2),
            "therapeutic_min": round(therapeutic_min, 2),
            "therapeutic_max": round(therapeutic_max, 2),
        },
    }