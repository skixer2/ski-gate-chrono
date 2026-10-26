"""Auth flow: register → login → profile (v1 = athlete/master only)."""

from tests.conftest import auth_headers, register_and_login


def test_register_login_profile_happy_path(client):
    tokens = register_and_login(client, name="ski-kid", user_type="athlete")

    r = client.get("/v1/profile", headers=auth_headers(tokens))
    assert r.status_code == 200
    body = r.json()
    assert body["name"] == "ski-kid"
    assert body["user_type"] == "athlete"
    assert body["activation_start_date"] is None  # open-ended ⇒ active


def test_register_rejects_trainer_in_v1(client):
    # §4: v1 self-registration = athlete/master only; P2 adds the rest.
    r = client.post(
        "/v1/auth/register",
        json={"name": "coach", "password": "gate-timer-pass",
              "user_type": "trainer"},
    )
    assert r.status_code == 422


def test_register_duplicate_name_conflict(client):
    register_and_login(client, name="dup")
    r = client.post(
        "/v1/auth/register",
        json={"name": "dup", "password": "gate-timer-pass", "user_type": "master"},
    )
    assert r.status_code == 409


def test_register_unknown_club_404(client):
    r = client.post(
        "/v1/auth/register",
        json={"name": "clubless", "password": "gate-timer-pass",
              "user_type": "athlete", "club_id": 999},
    )
    assert r.status_code == 404


def test_login_wrong_password_401(client):
    register_and_login(client, name="pw-test")
    r = client.post(
        "/v1/auth/login", json={"name": "pw-test", "password": "wrong-pass"}
    )
    assert r.status_code == 401


def test_profile_requires_token(client):
    assert client.get("/v1/profile").status_code == 401
    assert client.get(
        "/v1/profile", headers={"Authorization": "Bearer not-a-jwt"}
    ).status_code == 401


def test_refresh_rotates_and_revokes(client):
    tokens = register_and_login(client, name="rotator")

    r = client.post("/v1/auth/refresh", json={"refresh_token": tokens["refresh_token"]})
    assert r.status_code == 200
    new_tokens = r.json()
    assert new_tokens["refresh_token"] != tokens["refresh_token"]

    # Rotation: the presented token is single-use (§10) — replay must fail.
    r = client.post("/v1/auth/refresh", json={"refresh_token": tokens["refresh_token"]})
    assert r.status_code == 401

    # New access token works.
    assert client.get(
        "/v1/profile", headers=auth_headers(new_tokens)
    ).status_code == 200


def test_master_can_register(client):
    tokens = register_and_login(client, name="adult-racer", user_type="master")
    r = client.get("/v1/profile", headers=auth_headers(tokens))
    assert r.json()["user_type"] == "master"


def test_profile_update_and_rename_conflict(client):
    register_and_login(client, name="upd-a")
    tokens = register_and_login(client, name="upd-b")

    r = client.put(
        "/v1/profile", headers=auth_headers(tokens),
        json={"age": 17, "category": "U18"},
    )
    assert r.status_code == 200
    assert r.json()["age"] == 17 and r.json()["category"] == "U18"

    # name is the unique login identifier — collision must 409.
    r = client.put(
        "/v1/profile", headers=auth_headers(tokens), json={"name": "upd-a"}
    )
    assert r.status_code == 409
