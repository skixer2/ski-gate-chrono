"""GDPR hard delete (§9), retention purge (§5), activation-window write gate."""

from datetime import datetime, timedelta, timezone

from sqlalchemy import select

from app.db import SessionLocal, to_db_utc
from app.models import BarometricData, Run, Timestamp, User
from app.routers.retention import purge_runs_older_than
from tests.conftest import auth_headers, register_and_login, sample_run_payload


def _upload(client, tokens, payload):
    return client.post("/v1/runs", json=payload, headers=auth_headers(tokens))


def _set_activation_window(user_name: str, start=None, end=None) -> None:
    """Tests manipulate the window directly: the DB is truth (§5) and the
    gate re-reads it on every request — no token reissue needed."""
    with SessionLocal() as db:
        user = db.scalar(select(User).where(User.name == user_name))
        user.activation_start_date = to_db_utc(start) if start else None
        user.activation_end_date = to_db_utc(end) if end else None
        db.commit()


# --- GDPR ----------------------------------------------------------------------


def test_gdpr_requires_explicit_confirm(client):
    tokens = register_and_login(client, name="gdpr-1")
    _upload(client, tokens, sample_run_payload("uuid-gdpr-1"))

    r = client.post("/v1/me/gdpr-delete", json={"confirm": False},
                    headers=auth_headers(tokens))
    assert r.status_code == 400
    # Nothing was deleted.
    assert client.get("/v1/runs", headers=auth_headers(tokens)).status_code == 200


def test_gdpr_hard_delete_404_afterwards(client):
    tokens = register_and_login(client, name="gdpr-2")
    run_id = _upload(client, tokens, sample_run_payload("uuid-gdpr-2")).json()["id"]

    r = client.post("/v1/me/gdpr-delete", json={"confirm": True},
                    headers=auth_headers(tokens))
    assert r.status_code == 204

    # §9 step 5 / F50: every subsequent authenticated call 404s (the user
    # row is gone — this also neutralises residual access tokens, §11.7).
    assert client.get("/v1/runs", headers=auth_headers(tokens)).status_code == 404
    assert client.get(f"/v1/runs/{run_id}", headers=auth_headers(tokens)).status_code == 404

    # Login impossible; re-registration with the same name works (§11.7).
    assert client.post(
        "/v1/auth/login", json={"name": "gdpr-2", "password": "gate-timer-pass"}
    ).status_code == 401
    assert client.post(
        "/v1/auth/register",
        json={"name": "gdpr-2", "password": "gate-timer-pass", "user_type": "athlete"},
    ).status_code == 201


def test_gdpr_removes_all_child_rows(client):
    tokens = register_and_login(client, name="gdpr-3")
    _upload(client, tokens, sample_run_payload("uuid-gdpr-3"))
    with SessionLocal() as db:
        user = db.scalar(select(User).where(User.name == "gdpr-3"))
        user_id = user.id

    assert client.post("/v1/me/gdpr-delete", json={"confirm": True},
                       headers=auth_headers(tokens)).status_code == 204

    with SessionLocal() as db:
        assert db.scalar(select(Run).where(Run.athlete_id == user_id)) is None
        assert db.scalars(select(Timestamp)).all() == []
        assert db.scalars(select(BarometricData)).all() == []
        from app.security import RefreshToken

        assert db.scalars(
            select(RefreshToken).where(RefreshToken.user_id == user_id)
        ).all() == []


# --- Retention (§5, reqs v6.3) ---------------------------------------------------


def test_retention_purges_old_runs_only(client):
    tokens = register_and_login(client, name="old-timer")
    old = sample_run_payload("uuid-old-run")
    old["start_datetime"] = (
        datetime.now(timezone.utc) - timedelta(days=400)
    ).isoformat()
    new = sample_run_payload("uuid-new-run")
    new["start_datetime"] = datetime.now(timezone.utc).isoformat()
    old_id = _upload(client, tokens, old).json()["id"]
    new_id = _upload(client, tokens, new).json()["id"]

    purged = purge_runs_older_than(days=365)
    assert purged == 1

    with SessionLocal() as db:
        assert db.get(Run, old_id) is None  # old run gone…
        assert db.scalars(
            select(Timestamp).where(Timestamp.run_id == old_id)
        ).all() == []  # …including children
        assert db.get(Run, new_id) is not None  # recent run untouched


# --- Activation window (§5, JP-decided 2026-10-26) --------------------------------


def test_expired_user_writes_blocked_reads_and_deletes_allowed(client):
    tokens = register_and_login(client, name="expired-kid")
    run_id = _upload(client, tokens, sample_run_payload("uuid-exp-1")).json()["id"]

    now = datetime.now(timezone.utc)
    _set_activation_window(
        "expired-kid",
        start=now - timedelta(days=30),
        end=now - timedelta(days=1),  # expired yesterday
    )

    # WRITE blocked — upload…
    payload = sample_run_payload("uuid-exp-2")
    r = _upload(client, tokens, payload)
    assert r.status_code == 403
    assert r.json()["detail"]["code"] == "activation_expired"
    # …and rename.
    r = client.put(
        f"/v1/runs/{run_id}", json={"name": "nope"},
        headers={**auth_headers(tokens), "If-Match": "1"},
    )
    assert r.status_code == 403

    # READ allowed.
    r = client.get("/v1/runs", headers=auth_headers(tokens))
    assert r.status_code == 200 and len(r.json()["items"]) == 1

    # DELETE allowed (§5: deletion stays open so data remains erasable).
    assert client.delete(
        f"/v1/runs/{run_id}", headers=auth_headers(tokens)
    ).status_code == 204


def test_future_window_blocks_writes(client):
    tokens = register_and_login(client, name="early-kid")
    now = datetime.now(timezone.utc)
    _set_activation_window("early-kid", start=now + timedelta(days=7), end=None)
    r = _upload(client, tokens, sample_run_payload("uuid-early-1"))
    assert r.status_code == 403


def test_expired_club_blocks_writes(client):
    from app.models import SkiClub

    tokens = register_and_login(client, name="clubbed")
    now = datetime.now(timezone.utc)
    with SessionLocal() as db:
        club = SkiClub(
            name="skiclub-x",
            activation_start_date=to_db_utc(now - timedelta(days=400)),
            activation_end_date=to_db_utc(now - timedelta(days=10)),  # expired
        )
        db.add(club)
        db.flush()
        user = db.scalar(select(User).where(User.name == "clubbed"))
        user.club_id = club.id
        db.commit()

    r = _upload(client, tokens, sample_run_payload("uuid-club-1"))
    assert r.status_code == 403
    assert r.json()["detail"]["code"] == "club_expired"
    # Reads unaffected.
    assert client.get("/v1/runs", headers=auth_headers(tokens)).status_code == 200
