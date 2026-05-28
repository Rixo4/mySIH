"""add simulation job runtime fields

Revision ID: 9b7a4d1c2f31
Revises: 10229a1c5d96
Create Date: 2026-05-27 14:10:00.000000

"""
from __future__ import annotations

from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql


# revision identifiers, used by Alembic.
revision: str = "9b7a4d1c2f31"
down_revision: Union[str, Sequence[str], None] = "10229a1c5d96"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column("simulation_jobs", sa.Column("worker_hostname", sa.String(length=255), nullable=True))
    op.add_column("simulation_jobs", sa.Column("runtime_seconds", sa.Float(), nullable=True))
    op.add_column("simulation_jobs", sa.Column("queue_latency_seconds", sa.Float(), nullable=True))

    op.create_index(op.f("ix_simulation_jobs_worker_hostname"), "simulation_jobs", ["worker_hostname"], unique=False)

    op.add_column("dose_results", sa.Column("suppression_effect", sa.Float(), nullable=True))
    op.add_column("dose_results", sa.Column("excitation_effect", sa.Float(), nullable=True))
    op.add_column("dose_results", sa.Column("stabilization_effect", sa.Float(), nullable=True))


def downgrade() -> None:
    op.drop_column("dose_results", "stabilization_effect")
    op.drop_column("dose_results", "excitation_effect")
    op.drop_column("dose_results", "suppression_effect")

    op.drop_index(op.f("ix_simulation_jobs_worker_hostname"), table_name="simulation_jobs")
    op.drop_column("simulation_jobs", "queue_latency_seconds")
    op.drop_column("simulation_jobs", "runtime_seconds")
    op.drop_column("simulation_jobs", "worker_hostname")
