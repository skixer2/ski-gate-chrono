"""Nightly retention purge (§5, reqs v6.3, JP 2026-10-26).

Cloud run data (runs + timestamps + barometric_data) older than 1 year is
deleted server-side; phone-local copies are unaffected. Cron entry:

    # nightly 03:15 server time
    15 3 * * *  cd /srv/sgc-api && .venv/bin/python -m app.routers.retention
"""

from datetime import datetime, timedelta, timezone

from sqlalchemy import delete, select

from app.config import get_settings
from app.db import SessionLocal, to_db_utc
from app.models import BarometricData, Run, Timestamp


def purge_runs_older_than(days: int = 365) -> int:
    """Delete runs whose start_datetime is older than `days`; child rows are
    removed explicitly in the same transaction (deletion never blocked by
    activation windows — §5). Returns the number of runs purged."""
    cutoff = to_db_utc(datetime.now(timezone.utc) - timedelta(days=days))
    with SessionLocal() as db:
        run_ids = db.scalars(select(Run.id).where(Run.start_datetime < cutoff)).all()
        if run_ids:
            db.execute(delete(Timestamp).where(Timestamp.run_id.in_(run_ids)))
            db.execute(delete(BarometricData).where(BarometricData.run_id.in_(run_ids)))
            db.execute(delete(Run).where(Run.id.in_(run_ids)))
            db.commit()
        return len(run_ids)


if __name__ == "__main__":
    purged = purge_runs_older_than(get_settings().retention_days)
    print(f"retention purge: removed {purged} run(s) older than "
          f"{get_settings().retention_days} days")
