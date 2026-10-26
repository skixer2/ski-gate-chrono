"""Run upload happy path, idempotent retry, If-Match 409, delete cascade."""

import pytest

from sqlalchemy import select

from app.db import SessionLocal
from app.models import BarometricData, Run, Timestamp
from tests.conftest import auth_headers, register_and_login, sample_run_payload


def _upload(client, tokens, payload):
    return client.post("/v1/runs", json=payload, headers=auth_headers(tokens))


def test_upload_happy_path_one_transaction(client):
    tokens = register_and_login(client, name="uploader")
    r = _upload(client, tokens, sample_run_payload())

    assert r.status_code == 201, r.text
    body = r.json()
    assert body["lock_version"] == 1  # server-set (§4)
    assert body["already_exists"] is False

    # All three parts landed in the single transaction.
    with SessionLocal() as db:
        run = db.scalar(select(Run).where(Run.client_run_uuid == "uuid-run-0001"))
        assert run is not None
        assert run.athlete_id == 1 and run.name is None and run.course_id is None
        assert len(run.timestamps) == 2
        assert len(run.barometric_data) == 2


def test_upload_idempotent_retry(client):
    tokens = register_and_login(client, name="retrier")
    payload = sample_run_payload("uuid-retry-1")

    first = _upload(client, tokens, payload)
    assert first.status_code == 201

    # F43: the queue re-sends after a lost ACK — same uuid ⇒ 200 + flag,
    # no duplicate run, original lock_version untouched.
    retry = _upload(client, tokens, payload)
    assert retry.status_code == 200
    assert retry.json()["already_exists"] is True
    assert retry.json()["id"] == first.json()["id"]

    with SessionLocal() as db:
        runs = db.scalars(select(Run).where(Run.client_run_uuid == "uuid-retry-1")).all()
        assert len(runs) == 1


def test_list_runs_own_only_with_pagination(client):
    tokens = register_and_login(client, name="lister")
    for i in range(3):
        payload = sample_run_payload(f"uuid-list-{i}")
        payload["run_number"] = i
        assert _upload(client, tokens, payload).status_code == 201

    r = client.get("/v1/runs", headers=auth_headers(tokens))
    assert r.status_code == 200
    assert [item["run_number"] for item in r.json()["items"]] == [2, 1, 0]  # newest first

    # Keyset cursor: limit=2 → 2 items + next_cursor; follow it for the rest.
    r = client.get("/v1/runs?limit=2", headers=auth_headers(tokens))
    page1 = r.json()
    assert len(page1["items"]) == 2 and page1["next_cursor"] is not None
    r = client.get(
        f"/v1/runs?limit=2&cursor={page1['next_cursor']}", headers=auth_headers(tokens)
    )
    page2 = r.json()
    assert [i["run_number"] for i in page2["items"]] == [0]
    assert page2["next_cursor"] is None

    # Date filter (?from&to).
    r = client.get(
        "/v1/runs?from=2026-03-01T00:00:00Z", headers=auth_headers(tokens)
    )
    assert r.json()["items"] == []  # runs are from 2026-02-15

    # Foreign athlete_id is out of scope in v1 → 403 (P2 adds group reads).
    r = client.get("/v1/runs?athlete_id=999", headers=auth_headers(tokens))
    assert r.status_code == 403


def test_get_run_detail_include(client):
    tokens = register_and_login(client, name="viewer")
    run_id = _upload(client, tokens, sample_run_payload("uuid-detail-1")).json()["id"]

    r = client.get(f"/v1/runs/{run_id}", headers=auth_headers(tokens))
    assert r.status_code == 200
    assert r.json()["timestamps"] == []  # no include ⇒ meta only

    r = client.get(
        f"/v1/runs/{run_id}?include=timestamps,barometric_data",
        headers=auth_headers(tokens),
    )
    body = r.json()
    assert [t["arm_side"] for t in body["timestamps"]] == ["L", "R"]
    assert body["timestamps"][1]["guessed"] is True  # F27 estimated gate
    assert len(body["barometric_data"]) == 2

    # Foreign/missing run must not leak existence (§4, own-runs-only).
    other = register_and_login(client, name="viewer2")
    r = client.get(f"/v1/runs/{run_id}", headers=auth_headers(other))
    assert r.status_code == 404


def test_rename_if_match_flow(client):
    tokens = register_and_login(client, name="renamer")
    run_id = _upload(client, tokens, sample_run_payload("uuid-rename-1")).json()["id"]

    # Missing If-Match ⇒ 428; wrong lock_version ⇒ 409 (§4).
    r = client.put(
        f"/v1/runs/{run_id}", json={"name": "Stelvio run"},
        headers=auth_headers(tokens),
    )
    assert r.status_code == 428

    r = client.put(
        f"/v1/runs/{run_id}", json={"name": "Stelvio run"},
        headers={**auth_headers(tokens), "If-Match": "99"},
    )
    assert r.status_code == 409
    assert r.json()["detail"]["code"] == "lock_version_mismatch"

    # Correct If-Match ⇒ 200, name set, lock_version bumped to 2.
    r = client.put(
        f"/v1/runs/{run_id}", json={"name": "Stelvio run"},
        headers={**auth_headers(tokens), "If-Match": "1"},
    )
    assert r.status_code == 200
    assert r.json()["name"] == "Stelvio run"
    assert r.json()["lock_version"] == 2

    # Stale token-of-truth: retry with the OLD lock_version now 409s.
    r = client.put(
        f"/v1/runs/{run_id}", json={"name": "Stelvio run v2"},
        headers={**auth_headers(tokens), "If-Match": "1"},
    )
    assert r.status_code == 409


def test_delete_cascades_children(client):
    tokens = register_and_login(client, name="deleter")
    run_id = _upload(client, tokens, sample_run_payload("uuid-del-1")).json()["id"]

    r = client.delete(f"/v1/runs/{run_id}", headers=auth_headers(tokens))
    assert r.status_code == 204
    assert client.get(f"/v1/runs/{run_id}", headers=auth_headers(tokens)).status_code == 404

    # F46 cloud leg: children are gone, not orphaned.
    with SessionLocal() as db:
        assert db.scalar(select(Timestamp).where(Timestamp.run_id == run_id)) is None
        assert (
            db.scalar(select(BarometricData).where(BarometricData.run_id == run_id))
            is None
        )


def test_upload_row_cap_413(client):
    tokens = register_and_login(client, name="flooder")
    payload = sample_run_payload("uuid-cap-1")
    payload["barometric_data"] = [
        {"time_offset_ms": i * 100, "altitude_m": 1200.0, "speed_mps": 0.0}
        for i in range(10_001)  # one over the cap (§4: 413, never truncate)
    ]
    r = _upload(client, tokens, payload)
    assert r.status_code == 413


@pytest.mark.parametrize("bad_uuid", ["", "x"])
def test_upload_validates_client_uuid(client, bad_uuid):
    tokens = register_and_login(client, name="validator")
    payload = sample_run_payload("uuid-valid-1")
    payload["client_run_uuid"] = bad_uuid
    assert _upload(client, tokens, payload).status_code == 422


def test_course_id_rejected_in_v1(client):
    # §11.10: v1 always uploads course_id = null (auto-match is P3).
    tokens = register_and_login(client, name="nocourse")
    payload = sample_run_payload("uuid-course-1")
    payload["course_id"] = 1
    assert _upload(client, tokens, payload).status_code == 422
