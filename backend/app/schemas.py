from __future__ import annotations

from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator


class HealthResponse(BaseModel):
    status: Literal["ok"]
    service: str


class ChannelInput(BaseModel):
    ic50: float = Field(gt=0)
    hill: float = Field(gt=0)


class ChannelsInput(BaseModel):
    model_config = ConfigDict(extra="forbid")

    Na: ChannelInput
    K: ChannelInput
    Ca: ChannelInput


class DoseRangeInput(BaseModel):
    min_dose: float = Field(alias="min", ge=0)
    max_dose: float = Field(alias="max")
    step: float = Field(gt=0)

    @model_validator(mode="after")
    def validate_range(self) -> "DoseRangeInput":
        if self.max_dose <= self.min_dose:
            raise ValueError("dose.max must be greater than dose.min")
        return self


class DoseEvalRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    drug_name: str = Field(min_length=1)
    channels: ChannelsInput
    dose_range: DoseRangeInput
    runs: int = Field(ge=1, le=20)


class SingleDoseSimulationRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    drug_name: str = Field(min_length=1)
    channels: ChannelsInput
    dose: float = Field(ge=0)
    mode: Literal["fast", "accurate"] = "fast"


class RunResponse(BaseModel):
    run_id: str
    status: str
    report_type: str
    engine_input_mode: str
    parsed_summary: dict[str, Any] | None
    visualization_data: dict[str, Any] | None = None
    raw_report: str | None
    duration_seconds: float | None
    created_at: datetime
    error: str | None = None
    stderr: str | None = None


class RunListItem(BaseModel):
    run_id: str
    report_type: str
    drug_name: str | None
    status: str
    recommendation: str | None
    risk_level: str | None
    confidence: str | None
    created_at: datetime


class RunDetailResponse(BaseModel):
    run_id: str
    report_type: str
    drug_name: str | None
    status: str
    engine_input_mode: str
    recommendation: str | None
    risk_level: str | None
    confidence: str | None
    raw_report: str | None
    parsed_summary: dict[str, Any] | None
    visualization_data: dict[str, Any] | None = None
    input_payload: dict[str, Any] | None
    error_message: str | None
    duration_seconds: float | None
    created_at: datetime


class RunsListResponse(BaseModel):
    runs: list[RunListItem]


class DeleteRunResponse(BaseModel):
    run_id: str
    deleted: bool


class QueuedSimulationJobResponse(BaseModel):
    job_id: str
    status: str
    queue_name: str


class SimulationJobStatusResponse(BaseModel):
    job_id: str
    queue_name: str
    status: str
    is_finished: bool
    is_queued: bool
    is_started: bool
    result: dict[str, Any] | None = None
    error: str | None = None
    enqueued_at: datetime | None = None
    started_at: datetime | None = None
    ended_at: datetime | None = None
    meta: dict[str, Any] | None = None


class ScientificSimulationSubmitResponse(BaseModel):
    job_id: str
    status: Literal["QUEUED"]


class ScientificSimulationStatusResponse(BaseModel):
    job_id: str
    status: Literal["QUEUED", "RUNNING", "COMPLETED", "FAILED", "CANCELLED"]
    progress: int = Field(ge=0, le=100)
    created_at: datetime
    started_at: datetime | None = None
    completed_at: datetime | None = None
    result_run_id: str | None = None
    error_message: str | None = None
    runtime_seconds: float | None = None
    queue_latency_seconds: float | None = None

class QueueStatsResponse(BaseModel):
    queue_name: str
    queued_jobs: int
    running_jobs: int
    completed_jobs: int
    failed_jobs: int
    active_jobs: int
    max_concurrent_simulations: int
