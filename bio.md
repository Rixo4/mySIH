# Biofix Summary

## What Changed
- `backend/app/visualization.py` no longer synthesizes biological meaning in Python. It now only passes through backend-authored `visualization_data` when the C++ engine provides it.
- `backend/tests/test_scientific_validation.py` now checks contract preservation for four backend scientific profiles: Neutral, Strong Na, Strong K, and Strong Ca.
- `frontend/src/components/graphs/chartUtils.tsx` now treats backend `response_mode` and backend-provided zones as authoritative instead of applying local biological inference.
- `frontend/src/components/graphs/PrimaryResponseChart.tsx`, `FiringRateChart.tsx`, `NiiChart.tsx`, `SeizureRiskChart.tsx`, and `SynchronizationChart.tsx` now style charts from backend response mode only.

## Architectural Direction
- The neuropharmacology core remains in C++.
- Python is restricted to infrastructure and contract handling.
- The frontend is visualization-only and no longer invents biological state.

## Validation
- Backend syntax check passed with `python3 -m py_compile backend/app/visualization.py backend/tests/test_scientific_validation.py`.
- Frontend production build passed with `npm --prefix frontend run build`.
- Scientific regression tests passed with:

```bash
PYTHONPATH=backend .venv/bin/python -m pytest backend/tests/test_scientific_validation.py -q
```

## Notes
- The backend will not fabricate visualization payloads when `visualization_data` is missing.
- If the C++ engine does not emit a contract, the safe behavior is now to return no synthetic biology rather than guess.
