from __future__ import annotations

import uuid
from datetime import datetime

from sqlalchemy import DateTime, Float, ForeignKey, Index, String, Uuid
from sqlalchemy.orm import Mapped, mapped_column, relationship

from ..database import Base


class DoseResult(Base):
    __tablename__ = "dose_results"

    id: Mapped[uuid.UUID] = mapped_column(Uuid(as_uuid=True), primary_key=True, default=uuid.uuid4)
    run_id: Mapped[str] = mapped_column(String(128), ForeignKey("run_records.run_id", ondelete="CASCADE"), nullable=False, index=True)
    dose: Mapped[float] = mapped_column(Float, nullable=False)
    firing_rate: Mapped[float | None] = mapped_column(Float, nullable=True)
    seizure_score: Mapped[float | None] = mapped_column(Float, nullable=True)
    sync_index: Mapped[float | None] = mapped_column(Float, nullable=True)
    nii: Mapped[float | None] = mapped_column(Float, nullable=True)
    toxicity_score: Mapped[float | None] = mapped_column(Float, nullable=True)
    response_mode: Mapped[str | None] = mapped_column(String(128), nullable=True)
    biological_state: Mapped[str | None] = mapped_column(String(128), nullable=True)
    suppression_effect: Mapped[float | None] = mapped_column(Float, nullable=True)
    excitation_effect: Mapped[float | None] = mapped_column(Float, nullable=True)
    stabilization_effect: Mapped[float | None] = mapped_column(Float, nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, index=True)

    run_record: Mapped["RunRecord"] = relationship("RunRecord", back_populates="dose_results", lazy="selectin")


Index("ix_dose_results_run_id_dose", DoseResult.run_id, DoseResult.dose)
