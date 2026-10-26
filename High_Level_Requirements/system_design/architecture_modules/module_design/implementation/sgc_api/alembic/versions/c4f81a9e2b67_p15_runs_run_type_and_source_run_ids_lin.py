"""P1.5: runs.run_type + runs.source_run_ids (merged-run lineage)

Revision ID: c4f81a9e2b67
Revises: 2ac92a582154
Create Date: 2026-10-26 19:10:00

JP-approved P1.5 design (2026-10-26): the app now uploads MERGED courses,
so runs carry a provenance type and optional lineage to their source runs.

Migration notes / documented compromise:
  * run_type: NOT NULL with server_default 'single' — all pre-existing
    production rows (38 at deploy time) become 'single' with NO data
    change, and INSERTs from older code paths still succeed.
  * source_run_ids: nullable JSON column (JSON text on SQLite, native JSON
    on PostgreSQL — portable per §2 guardrail; no dialect-specific types).
  * CHECK constraint (run_type IN ('single','merged')): SQLite cannot
    ADD CONSTRAINT via ALTER TABLE. Rebuilding the live `runs` table in
    batch mode on the production DB was judged not worth the risk for a
    defense-in-depth check, so this migration adds COLUMNS ONLY. The
    constraint exists where tables are created from the ORM models
    (models.py __table_args__); the enforcement point that matters for
    this service is the Pydantic schema (422 on anything but
    'single' | 'merged').
"""
from collections.abc import Sequence

import sqlalchemy as sa
from alembic import op


revision: str = "c4f81a9e2b67"
down_revision: str | None = "2ac92a582154"
branch_labels: str | Sequence[str] | None = None
depends_on: str | Sequence[str] | None = None


def upgrade() -> None:
    op.add_column(
        "runs",
        sa.Column(
            "run_type",
            sa.Text(),
            nullable=False,
            server_default=sa.text("'single'"),
        ),
    )
    op.add_column(
        "runs",
        sa.Column("source_run_ids", sa.JSON(), nullable=True),
    )


def downgrade() -> None:
    op.drop_column("runs", "source_run_ids")
    op.drop_column("runs", "run_type")
