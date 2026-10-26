"""Database engine and session management.

§2 guardrails (non-negotiable from day 1):
  * SQLAlchemy declarative models only — ORM for all data access, no raw SQL
    in application code.
  * Portable types: INTEGER PKs, TEXT for UUIDs/enum-ish values,
    DateTime(timezone=True) stored UTC.
  * All writes funnel through short transactions (run upload = one
    transaction for run + timestamps + barometric rows).
"""

from collections.abc import Iterator
from datetime import datetime, timezone

from sqlalchemy import create_engine, event
from sqlalchemy.engine import Engine
from sqlalchemy.orm import DeclarativeBase, Session, sessionmaker

from app.config import get_settings

UTC = timezone.utc


def ensure_utc(dt: datetime) -> datetime:
    """Return dt as timezone-aware UTC (for Python-side comparisons).

    SQLite reads DateTime columns back as naive values; PostgreSQL returns
    aware ones. This helper makes comparisons correct on both, so the P2
    flip cannot introduce tz bugs (§2 guardrail)."""
    if dt.tzinfo is None:
        return dt.replace(tzinfo=UTC)
    return dt.astimezone(UTC)


def to_db_utc(dt: datetime) -> datetime:
    """Normalize any datetime to naive UTC before storing/binding.

    All timestamps cross the app boundary as aware-UTC ISO strings (JSON);
    inside the DB they are stored as naive UTC — one canonical form, and
    SQLite string comparisons stay lexicographically correct."""
    return ensure_utc(dt).replace(tzinfo=None)


class Base(DeclarativeBase):
    """Declarative base shared by every ORM model."""


def _utcnow() -> datetime:
    """Naive-UTC helper for server defaults (SQLite has no native timestamptz)."""
    return datetime.now(UTC).replace(tzinfo=None)


def _build_engine(database_url: str) -> Engine:
    kwargs: dict = {"future": True}
    is_sqlite = database_url.startswith("sqlite")

    if is_sqlite:
        # FastAPI may touch the session from different threads; SQLite checks
        # this by default. Safe for P0 (single uvicorn worker by design).
        kwargs["connect_args"] = {"check_same_thread": False}

    engine = create_engine(database_url, **kwargs)

    if is_sqlite:
        # The ONLY place connection-level pragmas are set. This is connection
        # configuration, not data access: WAL = crash-safe concurrent readers
        # (§3 SQLite choice), foreign_keys=ON makes ON DELETE CASCADE real
        # (SQLite ships with FK enforcement OFF by default). PostgreSQL needs
        # neither — the flip is just the connection URL (§2).
        @event.listens_for(engine, "connect")
        def _sqlite_pragmas(dbapi_conn, _record) -> None:  # noqa: ANN001
            cursor = dbapi_conn.cursor()
            cursor.execute("PRAGMA journal_mode=WAL")
            cursor.execute("PRAGMA foreign_keys=ON")
            cursor.close()

    return engine


settings = get_settings()
engine = _build_engine(settings.database_url)

SessionLocal = sessionmaker(bind=engine, autoflush=False, expire_on_commit=False)


def get_db() -> Iterator[Session]:
    """FastAPI dependency: one short-lived session per request."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
