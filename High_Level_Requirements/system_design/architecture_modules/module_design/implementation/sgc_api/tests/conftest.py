"""Test fixtures.

Env vars are set BEFORE any app import (config is read once per process).
Schema comes from the real Alembic migration — no create_all anywhere, so
every test run also proves `alembic upgrade head` builds a working DB.
"""

import os
import tempfile

# Must happen before importing app.* — Settings is cached at first use.
os.environ.setdefault("SGC_API_SECRET_KEY", "test-secret-key-0123456789abcdef-32-bytes-min")
_TMPDIR = tempfile.mkdtemp(prefix="sgc_api_test_")
os.environ["SGC_API_DATABASE_URL"] = f"sqlite:///{_TMPDIR}/test.db"

from alembic import command  # noqa: E402
from alembic.config import Config  # noqa: E402
from fastapi.testclient import TestClient  # noqa: E402
import pytest  # noqa: E402

from app.db import engine  # noqa: E402
from app.main import app  # noqa: E402

_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
_ALEMBIC_CFG = Config(os.path.join(_ROOT, "alembic.ini"))
_ALEMBIC_CFG.set_main_option("script_location", os.path.join(_ROOT, "alembic"))


def _migrate_to_head() -> None:
    command.upgrade(_ALEMBIC_CFG, "head")


@pytest.fixture(scope="session", autouse=True)
def database() -> None:
    _migrate_to_head()
    yield
    engine.dispose()


@pytest.fixture(autouse=True)
def clean_tables(database) -> None:
    """Fresh DB per test: drop everything, re-run the migration."""
    engine.dispose()
    _recreate_file_db()


def _recreate_file_db() -> None:
    from sqlalchemy import text

    with engine.begin() as conn:
        # Alembic-versioned drop: remove rows + schema, then rebuild via
        # the migration (keeps the "no create_all" invariant in tests too).
        conn.execute(text("DROP TABLE IF EXISTS refresh_tokens"))
        conn.execute(text("DROP TABLE IF EXISTS timestamps"))
        conn.execute(text("DROP TABLE IF EXISTS barometric_data"))
        conn.execute(text("DROP TABLE IF EXISTS runs"))
        conn.execute(text("DROP TABLE IF EXISTS users2groups"))
        conn.execute(text("DROP TABLE IF EXISTS users2athletes"))
        conn.execute(text("DROP TABLE IF EXISTS course_gates"))
        conn.execute(text("DROP TABLE IF EXISTS courses"))
        conn.execute(text("DROP TABLE IF EXISTS groups"))
        conn.execute(text("DROP TABLE IF EXISTS users"))
        conn.execute(text("DROP TABLE IF EXISTS ski_clubs"))
        conn.execute(text("DROP TABLE IF EXISTS alembic_version"))
    _migrate_to_head()


@pytest.fixture()
def client() -> TestClient:
    return TestClient(app)


# --- Shared helpers -----------------------------------------------------------


def register_and_login(
    client: TestClient,
    name: str = "athlete1",
    password: str = "gate-timer-pass",
    user_type: str = "athlete",
) -> dict:
    """Register + login; returns the token response dict."""
    r = client.post(
        "/v1/auth/register",
        json={"name": name, "password": password, "user_type": user_type},
    )
    assert r.status_code == 201, r.text
    r = client.post("/v1/auth/login", json={"name": name, "password": password})
    assert r.status_code == 200, r.text
    return r.json()


def auth_headers(tokens: dict) -> dict:
    return {"Authorization": f"Bearer {tokens['access_token']}"}


def sample_run_payload(client_run_uuid: str = "uuid-run-0001") -> dict:
    """F23-shaped batch: run meta + 2 timestamps + 2 barometric rows."""
    return {
        "client_run_uuid": client_run_uuid,
        "start_datetime": "2026-02-15T10:30:00Z",
        "run_number": 3,
        "format_version": 1,
        "name": None,
        "visibility": None,
        "course_id": None,
        "timestamps": [
            {"timestamp_ms": 15230, "arm_side": "L", "guessed": False},
            {"timestamp_ms": 22105, "arm_side": "R", "guessed": True},
        ],
        "barometric_data": [
            {"time_offset_ms": 0, "altitude_m": 1204.5, "speed_mps": 0.2},
            {"time_offset_ms": 100, "altitude_m": 1204.1, "speed_mps": -3.9},
        ],
    }
