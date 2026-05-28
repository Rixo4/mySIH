from __future__ import annotations

from app.visualization import build_visualization_payload


def _contract(
    response_mode: str,
    *,
    active_zone: str,
    dose_results: list[dict[str, object]],
    classification_timeline: list[dict[str, object]],
    reference_points: dict[str, object],
    zone_contract: dict[str, object],
) -> dict[str, object]:
    return {
        "response_mode": response_mode,
        "active_zone": active_zone,
        "toxicity_observed": active_zone not in {"NONE", "NO_VALID_WINDOW"},
        "dose_results": dose_results,
        "classification_timeline": classification_timeline,
        "reference_points": reference_points,
        "zone_contract": zone_contract,
    }


def _payload(contract: dict[str, object], *, max_dose: float = 20.0) -> dict:
    input_payload = {
        "drug_name": "test-drug",
        "channels": {
            "Na": {"ic50": 200.0, "hill": 3.2},
            "K": {"ic50": 8.0, "hill": 3.2},
            "Ca": {"ic50": 1000.0, "hill": 3.2},
        },
        "dose_range": {"min": 0.0, "max": max_dose, "step": 5.0},
    }
    parsed_summary = {
        "visualization_data": contract,
        "response_mode": contract["response_mode"],
        "active_zone": contract["active_zone"],
        "has_valid_therapeutic_window": contract["reference_points"]["has_valid_therapeutic_window"],
    }
    return build_visualization_payload(report_type="dose-eval", input_payload=input_payload, parsed_summary=parsed_summary)


def _base_dose_results(response_mode: str) -> list[dict[str, object]]:
    return [
        {
            "dose": 0.0,
            "effect": 0.0,
            "firing_rate": 12.0,
            "seizure_score": 22.0,
            "sync": 0.32,
            "nii": 0.21,
            "toxicity_score": 4.0,
            "variance": 0.08,
            "response_mode": response_mode,
            "biological_state": "LIMITED_EFFECT",
            "ic50_na": 200.0,
            "ic50_k": 8.0,
            "ic50_ca": 1000.0,
            "active_zone": "NONE",
        },
        {
            "dose": 10.0,
            "effect": 50.0,
            "firing_rate": 8.0,
            "seizure_score": 16.0,
            "sync": 0.25,
            "nii": 0.16,
            "toxicity_score": 3.0,
            "variance": 0.07,
            "response_mode": response_mode,
            "biological_state": "LIMITED_EFFECT",
            "ic50_na": 200.0,
            "ic50_k": 8.0,
            "ic50_ca": 1000.0,
            "active_zone": "THERAPEUTIC",
        },
        {
            "dose": 20.0,
            "effect": 90.0,
            "firing_rate": 6.0,
            "seizure_score": 10.0,
            "sync": 0.19,
            "nii": 0.11,
            "toxicity_score": 2.0,
            "variance": 0.05,
            "response_mode": response_mode,
            "biological_state": "LIMITED_EFFECT",
            "ic50_na": 200.0,
            "ic50_k": 8.0,
            "ic50_ca": 1000.0,
            "active_zone": "THERAPEUTIC",
        },
    ]


def _timeline(label: str, state: str, start: float, end: float, color: str) -> list[dict[str, object]]:
    return [{"label": label, "state": state, "from": start, "to": end, "color": color}]


def test_neutral_contract_is_passed_through() -> None:
    visualization_data = _contract(
        "NO_SIGNIFICANT_RESPONSE",
        active_zone="NONE",
        dose_results=_base_dose_results("NO_SIGNIFICANT_RESPONSE"),
        classification_timeline=_timeline("Ineffective Zone", "INEFFECTIVE_ZONE", 0.0, 20.0, "#475569"),
        reference_points={
            "ic50": 200.0,
            "toxic_threshold": None,
            "therapeutic_min": None,
            "therapeutic_max": None,
            "active_zone": "NONE",
            "has_valid_therapeutic_window": False,
        },
        zone_contract={
            "ineffective_zone": [0.0, 20.0],
            "therapeutic_zone": None,
            "over_suppression_zone": None,
            "severe_excitability_zone": None,
            "saturated_stabilization_zone": None,
            "active_zone": "NONE",
            "has_valid_therapeutic_window": False,
            "integrity_warnings": [],
        },
    )

    payload = _payload(visualization_data)

    assert payload == visualization_data
    assert payload["response_mode"] == "NO_SIGNIFICANT_RESPONSE"
    assert payload["active_zone"] == "NONE"


def test_strong_na_contract_is_passed_through() -> None:
    visualization_data = _contract(
        "SUPPRESSIVE_RESPONSE",
        active_zone="THERAPEUTIC",
        dose_results=[
            {
                "dose": 0.0,
                "effect": 0.0,
                "firing_rate": 16.0,
                "seizure_score": 26.0,
                "sync": 0.35,
                "nii": 0.23,
                "toxicity_score": 5.0,
                "variance": 0.09,
                "response_mode": "SUPPRESSIVE_RESPONSE",
                "biological_state": "CONTROLLED_SUPPRESSION",
                "ic50_na": 70.0,
                "ic50_k": 8.0,
                "ic50_ca": 1000.0,
                "active_zone": "THERAPEUTIC",
            },
            {
                "dose": 10.0,
                "effect": 68.0,
                "firing_rate": 8.0,
                "seizure_score": 12.0,
                "sync": 0.22,
                "nii": 0.14,
                "toxicity_score": 3.0,
                "variance": 0.06,
                "response_mode": "SUPPRESSIVE_RESPONSE",
                "biological_state": "CONTROLLED_SUPPRESSION",
                "ic50_na": 70.0,
                "ic50_k": 8.0,
                "ic50_ca": 1000.0,
                "active_zone": "THERAPEUTIC",
            },
            {
                "dose": 20.0,
                "effect": 92.0,
                "firing_rate": 5.0,
                "seizure_score": 8.0,
                "sync": 0.17,
                "nii": 0.10,
                "toxicity_score": 2.0,
                "variance": 0.05,
                "response_mode": "SUPPRESSIVE_RESPONSE",
                "biological_state": "NEURAL_SILENCING",
                "ic50_na": 70.0,
                "ic50_k": 8.0,
                "ic50_ca": 1000.0,
                "active_zone": "OVER_SUPPRESSION",
            },
        ],
        classification_timeline=_timeline("Therapeutic Zone", "THERAPEUTIC_ZONE", 4.0, 11.0, "#10b981"),
        reference_points={
            "ic50": 70.0,
            "toxic_threshold": 14.0,
            "therapeutic_min": 4.0,
            "therapeutic_max": 11.0,
            "active_zone": "THERAPEUTIC",
            "has_valid_therapeutic_window": True,
        },
        zone_contract={
            "ineffective_zone": [0.0, 4.0],
            "therapeutic_zone": [4.0, 11.0],
            "over_suppression_zone": [14.0, 18.0],
            "severe_excitability_zone": None,
            "saturated_stabilization_zone": None,
            "active_zone": "THERAPEUTIC",
            "has_valid_therapeutic_window": True,
            "integrity_warnings": [],
        },
    )

    payload = _payload(visualization_data)

    assert payload == visualization_data
    assert payload["response_mode"] == "SUPPRESSIVE_RESPONSE"
    assert payload["reference_points"]["toxic_threshold"] == 14.0


def test_strong_k_contract_is_passed_through() -> None:
    visualization_data = _contract(
        "EXCITATORY_RESPONSE",
        active_zone="EXCITATORY",
        dose_results=[
            {
                "dose": 0.0,
                "effect": 0.0,
                "firing_rate": 9.0,
                "seizure_score": 14.0,
                "sync": 0.24,
                "nii": 0.15,
                "toxicity_score": 2.0,
                "variance": 0.06,
                "response_mode": "EXCITATORY_RESPONSE",
                "biological_state": "HYPEREXCITABILITY",
                "ic50_na": 200.0,
                "ic50_k": 12.0,
                "ic50_ca": 1000.0,
                "active_zone": "EXCITATORY",
            },
            {
                "dose": 10.0,
                "effect": 62.0,
                "firing_rate": 15.0,
                "seizure_score": 24.0,
                "sync": 0.31,
                "nii": 0.21,
                "toxicity_score": 5.0,
                "variance": 0.09,
                "response_mode": "EXCITATORY_RESPONSE",
                "biological_state": "HYPEREXCITABILITY",
                "ic50_na": 200.0,
                "ic50_k": 12.0,
                "ic50_ca": 1000.0,
                "active_zone": "EXCITATORY",
            },
            {
                "dose": 20.0,
                "effect": 94.0,
                "firing_rate": 22.0,
                "seizure_score": 35.0,
                "sync": 0.39,
                "nii": 0.28,
                "toxicity_score": 8.0,
                "variance": 0.12,
                "response_mode": "EXCITATORY_RESPONSE",
                "biological_state": "TOXIC_INSTABILITY",
                "ic50_na": 200.0,
                "ic50_k": 12.0,
                "ic50_ca": 1000.0,
                "active_zone": "TOXIC",
            },
        ],
        classification_timeline=_timeline("Excitatory Zone", "EXCITATORY_ZONE", 14.0, 18.0, "#f59e0b"),
        reference_points={
            "ic50": 12.0,
            "toxic_threshold": 18.0,
            "therapeutic_min": 14.0,
            "therapeutic_max": 18.0,
            "active_zone": "EXCITATORY",
            "has_valid_therapeutic_window": True,
        },
        zone_contract={
            "ineffective_zone": [0.0, 4.0],
            "therapeutic_zone": None,
            "over_suppression_zone": None,
            "severe_excitability_zone": [14.0, 18.0],
            "saturated_stabilization_zone": None,
            "active_zone": "EXCITATORY",
            "has_valid_therapeutic_window": True,
            "integrity_warnings": [],
        },
    )

    payload = _payload(visualization_data)

    assert payload == visualization_data
    assert payload["response_mode"] == "EXCITATORY_RESPONSE"
    assert payload["reference_points"]["toxic_threshold"] == 18.0


def test_strong_ca_contract_is_passed_through() -> None:
    visualization_data = _contract(
        "STABILIZING_RESPONSE",
        active_zone="STABILIZING",
        dose_results=[
            {
                "dose": 0.0,
                "effect": 0.0,
                "firing_rate": 11.0,
                "seizure_score": 20.0,
                "sync": 0.41,
                "nii": 0.25,
                "toxicity_score": 4.0,
                "variance": 0.08,
                "response_mode": "STABILIZING_RESPONSE",
                "biological_state": "NETWORK_STABILIZATION",
                "ic50_na": 200.0,
                "ic50_k": 8.0,
                "ic50_ca": 150.0,
                "active_zone": "THERAPEUTIC",
            },
            {
                "dose": 10.0,
                "effect": 64.0,
                "firing_rate": 9.0,
                "seizure_score": 13.0,
                "sync": 0.21,
                "nii": 0.13,
                "toxicity_score": 3.0,
                "variance": 0.05,
                "response_mode": "STABILIZING_RESPONSE",
                "biological_state": "NETWORK_STABILIZATION",
                "ic50_na": 200.0,
                "ic50_k": 8.0,
                "ic50_ca": 150.0,
                "active_zone": "STABILIZING",
            },
            {
                "dose": 20.0,
                "effect": 88.0,
                "firing_rate": 7.0,
                "seizure_score": 9.0,
                "sync": 0.12,
                "nii": 0.08,
                "toxicity_score": 2.0,
                "variance": 0.04,
                "response_mode": "STABILIZING_RESPONSE",
                "biological_state": "NETWORK_STABILIZATION",
                "ic50_na": 200.0,
                "ic50_k": 8.0,
                "ic50_ca": 150.0,
                "active_zone": "STABILIZING",
            },
        ],
        classification_timeline=_timeline("Stabilization Zone", "STABILIZATION_ZONE", 5.0, 12.0, "#06b6d4"),
        reference_points={
            "ic50": 150.0,
            "toxic_threshold": 18.0,
            "therapeutic_min": 5.0,
            "therapeutic_max": 12.0,
            "active_zone": "STABILIZING",
            "has_valid_therapeutic_window": True,
        },
        zone_contract={
            "ineffective_zone": [0.0, 5.0],
            "therapeutic_zone": [5.0, 12.0],
            "over_suppression_zone": None,
            "severe_excitability_zone": None,
            "saturated_stabilization_zone": [14.0, 18.0],
            "active_zone": "STABILIZING",
            "has_valid_therapeutic_window": True,
            "integrity_warnings": [],
        },
    )

    payload = _payload(visualization_data)

    assert payload == visualization_data
    assert payload["response_mode"] == "STABILIZING_RESPONSE"
    assert payload["reference_points"]["therapeutic_min"] == 5.0


def test_missing_backend_visualization_data_is_not_synthesized() -> None:
    input_payload = {
        "drug_name": "test-drug",
        "channels": {
            "Na": {"ic50": 200.0, "hill": 3.2},
            "K": {"ic50": 8.0, "hill": 3.2},
            "Ca": {"ic50": 1000.0, "hill": 3.2},
        },
        "dose_range": {"min": 0.0, "max": 20.0, "step": 5.0},
    }

    assert build_visualization_payload(report_type="dose-eval", input_payload=input_payload, parsed_summary={"response_mode": "SUPPRESSIVE_RESPONSE"}) is None
