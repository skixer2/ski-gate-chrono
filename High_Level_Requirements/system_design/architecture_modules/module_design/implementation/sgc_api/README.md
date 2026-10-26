# sgc-api — SGC Remote Database P0 Backend

Prototype REST API for the Ski Gate Chrono cloud DB. Implements the P0
slice of `remote_db_architecture.md` (v0.2, JP-approved 2026-10-26):
auth skeleton, F23 batch upload, own-run browse, F46 cloud delete leg,
F50 GDPR hard delete, nightly retention purge, bootstrap JSON.

Requirement source of truth: `sgc_requirements.md` §REQ-DB (v6.3) —
all table/field names copied EXACTLY from there. Do not rename fields
without a REQ-DB change.

## Stack (per architecture doc §3)

| Element    | Choice                                        |
|------------|-----------------------------------------------|
| Framework  | FastAPI, Python 3.12 (dev box: 3.11 works)    |
| ORM        | SQLAlchemy 2.x declarative + Alembic          |
| DB (P0)    | SQLite, WAL mode → PostgreSQL at P2 (§2)      |
| Auth       | PyJWT (HS256, 60 min) + argon2id passwords    |
| Tests      | pytest (schema built by the real migration)   |

## Layout

```
app/
  config.py     env-driven settings (SGC_API_* prefix)
  db.py         engine/session; SQLite WAL + FK pragmas in ONE place
  models.py     all 10 REQ-DB tables (exact fields) + additions flagged
  schemas.py    pydantic v2 request/response contracts
  security.py   argon2id, JWT, refresh_tokens table (rotation/revocation)
  deps.py       get_db, get_current_user, require_write_allowed (§5 gate)
  routers/      health, auth, profile, runs, gdpr, bootstrap, retention
alembic/        ONE initial revision — schema changes only via migrations
tests/          pytest suite (runs the migration, not create_all)
scripts/smoke.sh  curl end-to-end smoke test
```

## Run it

```bash
cd sgc_api
python3 -m venv .venv
.venv/bin/pip install -e ".[dev]"

# schema (SQLite file at ./data/sgc.db by default)
SGC_API_SECRET_KEY=change-me .venv/bin/alembic upgrade head

# serve (dev)
SGC_API_SECRET_KEY=change-me .venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8000

# interactive API docs (kept for JP review, §3): http://127.0.0.1:8000/docs
```

Environment: `SGC_API_SECRET_KEY` (required in prod — JWT signing),
`SGC_API_DATABASE_URL` (default `sqlite:///./data/sgc.db`),
`SGC_API_API_BASE` + `SGC_API_MIN_APP_VERSION` (bootstrap payload).

## Tests + smoke

```bash
.venv/bin/pytest -q              # full suite (fresh migrated DB per test)
bash scripts/smoke.sh 8901       # curl end-to-end vs throwaway SQLite DB
```

## API v1 summary (§4 — all paths under /v1)

| Method + Path                        | Auth       | Notes |
|--------------------------------------|------------|-------|
| GET  `/healthz`                      | none       | liveness |
| GET  `/bootstrap-info`               | none       | I07 `{api_base, min_app_version}` |
| POST `/v1/auth/register`             | none       | **athlete/master only in v1** |
| POST `/v1/auth/login`                | none       | access + refresh (rotated) |
| POST `/v1/auth/refresh`              | refresh    | single-use refresh tokens |
| GET/PUT `/v1/profile`                | JWT        | F20; PUT is activation-gated |
| POST `/v1/runs`                      | JWT+gate   | meta + `timestamps[]` + `barometric_data[]` in ONE txn; idempotent on `client_run_uuid` (retry → 200 `already_exists`); `lock_version=1` server-set; 413 over row caps; P1.5: optional `run_type` (`single`\|`merged`, default `single`) + `source_run_ids` (list of cloud run ids, merged-run lineage) |
| GET  `/v1/runs`                      | JWT        | own runs; `?athlete_id&from&to&limit&cursor`; items include `run_type` + `source_run_ids` verbatim (P1.5) |
| GET  `/v1/runs/{id}`                 | JWT        | `?include=timestamps,barometric_data`; returns `run_type` + `source_run_ids` verbatim (P1.5) |
| PUT  `/v1/runs/{id}`                 | JWT+gate   | rename (F45); `If-Match: <lock_version>` → 428 missing / 409 mismatch |
| DELETE `/v1/runs/{id}`               | JWT        | 204; children cascade; **never activation-blocked** |
| POST `/v1/me/gdpr-delete`            | JWT        | `{confirm: true}` → hard-delete user+runs+children+tokens; everything 404s after (§9) |
| (cron) `python -m app.routers.retention` | —      | purge runs older than 365 d (§5, reqs v6.3) |

Activation windows (§5, JP-decided): expired ⇒ **writes 403** (`activation_expired` / `club_expired`), reads + deletion always allowed.

### Merged runs (P1.5, JP-approved 2026-10-26)

POST `/v1/runs` accepts two optional fields: `run_type`
(`"single"` \| `"merged"`, default `"single"`) and `source_run_ids`
(list of cloud run ids — lineage of a merged run; may be absent/empty
when the source runs predate the cloud). Validation: anything but
`single`/`merged` ⇒ 422; non-integer lineage entries ⇒ 422; merged
without `source_run_ids` is **allowed**. All run GETs return both
fields verbatim; pre-P1.5 rows read back as `run_type="single"`,
`source_run_ids=null` (migration `c4f81a9e2b67` server_default, no
data change). Note: the DB-level CHECK on `run_type` exists only on
tables created from the ORM models — SQLite cannot ADD CONSTRAINT via
ALTER TABLE, so on migrated DBs the vocabulary is enforced by the
Pydantic schema (422).

## Docker (P0 proto posture)

```bash
SGC_API_SECRET_KEY=real-secret-here docker compose up -d --build
# → http://127.0.0.1:8000 (host loopback only; TLS arrives with the VPS
#   reverse proxy per §3/§10). SQLite lives on the sgc-api-data volume.
```

Non-root container, healthcheck on `/healthz`, DB port never published.

## P0 decisions embedded in code (worth a JP glance)

1. **Login identifier = `users.name`** (unique). REQ-DB has no email
   field; adding one would be a schema invention, so name is the
   credential handle. Flag for P2 if an email column is wanted.
2. **`users.club_id`** added (nullable FK) — implied by the REQ-DB
   diagram `ski_clubs ──< users`; needed for the club-window gate.
3. **`runs.client_run_uuid`** added (unique TEXT) — the §4 idempotency
   key for the F43 offline queue.
4. **`refresh_tokens`** server-side table (hashed, rotated, revocable) —
   §10 "refresh-token rotation + revocation list".
5. **JWT subject deleted ⇒ 404** (not 401) on all authenticated routes —
   makes the §9 "everything 404s after GDPR" criterion literal and
   neutralises residual access tokens within their 60 min TTL.
6. **GDPR = full hard delete incl. user row** (§11.7 clarified) —
   re-registration with the same name is therefore possible.
