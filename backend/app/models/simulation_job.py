from __future__ import annotations

import uuid
from datetime import datetime
from enum import Enum
from typing import Any

from sqlalchemy import DateTime, Enum as SAEnum, Float, ForeignKey, Index, JSON, String, Text, Uuid
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.orm import Mapped, mapped_column, relationship

from ..database import Base


class SimulationJobStatus(str, Enum):
    QUEUED = "QUEUED"
    RUNNING = "RUNNING"
    COMPLETED = "COMPLETED"
    FAILED = "FAILED"
    CANCELLED = "CANCELLED"


class SimulationJob(Base):
    __tablename__ = "simulation_jobs"

    id: Mapped[uuid.UUID] = mapped_column(Uuid(as_uuid=True), primary_key=True, default=uuid.uuid4)
    rq_job_id: Mapped[str | None] = mapped_column(String(128), unique=True, nullable=True, index=True)
    status: Mapped[SimulationJobStatus] = mapped_column(
        SAEnum(SimulationJobStatus, name="simulation_job_status"),
        nullable=False,
        default=SimulationJobStatus.QUEUED,
        index=True,
    )
    progress: Mapped[float] = mapped_column(Float, nullable=False, default=0.0)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, index=True)
    started_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    completed_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    input_json: Mapped[dict[str, Any] | None] = mapped_column(
        JSON().with_variant(JSONB(astext_type=Text()), "postgresql"),
        nullable=True,
    )
    result_run_id: Mapped[str | None] = mapped_column(String(128), ForeignKey("run_records.run_id", ondelete="SET NULL"), nullable=True, index=True)
    error_message: Mapped[str | None] = mapped_column(Text, nullable=True)
    worker_hostname: Mapped[str | None] = mapped_column(String(255), nullable=True, index=True)
    runtime_seconds: Mapped[float | None] = mapped_column(Float, nullable=True)
    queue_latency_seconds: Mapped[float | None] = mapped_column(Float, nullable=True)

    run_record: Mapped["RunRecord | None"] = relationship("RunRecord", back_populates="simulation_job", lazy="selectin")


Index("ix_simulation_jobs_status_created_at", SimulationJob.status, SimulationJob.created_at)
