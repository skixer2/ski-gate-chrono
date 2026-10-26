# SGC — Remote Database Architecture (DRAFT for JP review)

*2026-10-26 — v0.2. Requirement source: `sgc_requirements.md` v6.3 (§REQ-DB, F20/F22/F23/F25/F32/F34/F35/F36/F43/F46/F48/F50, I07). Status: APPROVED-IN-PROGRESS — JP answered §11 on 2026-10-26 (8 of 10 resolved; #3 and #7 clarified). P0 scaffold authorized.*

---

## 1. Scope & Goals

The cloud DB exists to (a) back up gate timestamps + 10 Hz barometric data per F23, (b) let trainers and linked followers view athlete runs (F25, access matrix), and (c) feed the comparison views (F22, F32, F34) with cloud-resident runs (F48). **Raw 100 Hz quaternion/acceleration data never leaves the phone (F23)** — the cloud stores derived, small data only.

| Phase | Scope |
|---|---|
| **v1 MVP** | Single-user: account creation, JWT auth, profile sync (F20 push-to-cloud toggle), run upload (timestamps + barometric_data), run browse/list (own runs), cloud run delete (cloud leg of F46), GDPR deletion (F50), courses tables created but API-stubbed |
| **P2** | Full access-control matrix: trainer group viewing (F25), visibility defaults + per-run override (F35/F36), relationship management (users2athletes rules), trainer/master run deletion |
| **P3** | Cross-athlete cloud comparisons (F48 full: two runs from different athletes), courses API live (F57.1–F57.4), course auto-match on upload (runs.course_id) |

Out of scope for all phases: raw sensor upload (F23 prohibition), firmware OTA hosting (separate concern), video/images.

## 2. Backend Recommendation — Phased, SQLite proto → PostgreSQL

The requirements' V-Model already anticipates both: *"Cloud: REST API + PostgreSQL (or SQLite for prototype)"*. Recommendation: **build on FastAPI + SQLAlchemy 2.x (ORM) + Alembic migrations, run on SQLite for P0/P1, flip to PostgreSQL at P2.** The DB is behind the ORM from day one, so the flip is a connection-string + migration run, not a rewrite.

| Criterion | SQLite proto (P0–P1) | PostgreSQL day 1 |
|---|---|---|
| Ops cost on the existing VPS | Zero extra service: one file inside the `sgc-api` container volume; `sqlite3`-level backup = copy file | Second container + credentials + healthchecks + upgrade cadence |
| Concurrency | Single-writer; fine for v1 (one phone, batched uploads) | Row-level locking; needed when multiple trainers/devices write concurrently |
| Migration risk | Low **if** guardrails held (below); schema is identical via ORM | None later — but day-1 ops burden before the product shape is stable |
| GDPR hard delete (F50) | Trivial: `DELETE` rows + VACUUM | Same, plus scheduled backup purge |
| Docker constraint fit | ✅ fits single-host, single-container MVP | ✅ but doubles the compose file |

**Guardrails that keep the flip cheap** (non-negotiable from day 1):
1. SQLAlchemy declarative models only; **no SQLite-specific pragmas or raw SQL in app code**.
2. Alembic migrations from the first commit (no "create_all in prod").
3. Portable types: INTEGER PKs, TEXT for UUIDs/enum-ish fields (or native `Enum`), `DateTime(timezone=True)` stored UTC.
4. All writes funnel through short transactions (upload = one transaction for run + timestamps + barometric rows).
5. **Migration triggers to Postgres:** first multi-user write concurrency (P2 sharing), any observed `database is locked` in logs, or > ~50 active users.

**Tradeoff accepted:** a small one-time migration exercise at P2 (dump/reload via Alembic + script), in exchange for zero DB-ops during the fragile proto phase.

## 3. Stack & Deployment

| Element | Choice | Notes |
|---|---|---|
| Framework | **FastAPI** (Python 3.12) | Pydantic request/response models give us schema validation for the batch upload for free; auto OpenAPI docs for JP review |
| ORM / migrations | SQLAlchemy 2.x + Alembic | See §2 guardrails |
| DB (P0/P1 → P2) | SQLite (WAL mode) → PostgreSQL 16 container | Flip is config + `alembic upgrade` |
| Container | `sgc-api` on the existing Docker VPS | Joins the reverse-proxy network; DB port never published; API listens on internal :8000 |
| TLS | Via the VPS reverse proxy (Caddy preferred: auto Let's Encrypt; nginx+certbot acceptable) | **HTTPS-only** per I07; HTTP → 308 redirect, HSTS on |
| Domain | e.g. `sgc-api.<jp-domain>` + bootstrap host `sgc-bootstrap.<jp-domain>` | TBD — Open Question #3 |
| Bootstrap (I07) | Static JSON at the hardcoded bootstrap address: `{"api_base": "https://…", "min_app_version": "1.65"}` | One indirection so the API can move host/domain without an app release |
| Backups | Nightly: SQLite file copy (or `pg_dump`) off-box + 30-day retention | Must be GDPR-aware — Open Question #9 |

## 4. REST API v1

All paths prefixed `/v1`. Auth = `Authorization: Bearer <JWT>` unless noted. `format_version`/`lock_version` semantics per REQ-DB `runs` table notes: `format_version` (uint8) set by the phone (bit-packing version), `lock_version` (uint32) incremented server-side on **every** write, used for optimistic concurrency (client sends `If-Match: <lock_version>`; mismatch → `409`).

| Method + Path | Auth | Purpose / Notes |
|---|---|---|
| GET `/healthz` | none | Liveness for Docker/proxy |
| POST `/auth/register` | none (v1) | Creates `users` row (name, age, category, user_type, club). **v1 MVP: athlete/master self-registration only** — trainer/parent/friend onboarding is P2 (relationship rules) |
| POST `/auth/login` | none | Credentials → access JWT + refresh token. **Enforcement point #1 for `ski_clubs`/`users` activation windows** |
| POST `/auth/refresh` | refresh token | **Enforcement point #2**: club window expired → `403 club_expired`, read-only degraded mode possible |
| GET `/profile` / PUT `/profile` | JWT | F20 profile fields incl. `push_to_cloud` toggle |
| POST `/runs` | JWT (athlete/master = self) | **Batch upload, one transaction:** run metadata + `timestamps[]` + `barometric_data[]` (F23 payload only). Idempotency via client-generated `client_run_uuid` (dedupe on retry — offline queue re-sends, F43). Server sets `lock_version=1`, stores `format_version` |
| GET `/runs` | JWT | Query: `?athlete_id&from&to&limit&cursor`. v1: own runs only; P2 adds group/follower filters per access matrix |
| GET `/runs/{id}` | JWT | `?include=timestamps,barometric_data` for viewer/comparison rendering (F48 → F22/F32/F34) |
| PUT `/runs/{id}` | JWT | Rename (F45) / visibility override (F36, P2). Requires `If-Match: lock_version` |
| DELETE `/runs/{id}` | JWT (P2: trainer/master only per F46) | **Cloud leg of 3-way delete** (device Flash + phone + cloud). Server deletes run + child rows; returns `204` |
| POST `/me/gdpr-delete` | JWT + `confirm: true` body flag | **F50** — see §9 |
| GET `/courses` / POST `/courses` | JWT | **Stub in v1**: 501 Not Implemented or empty list; tables exist so P3 is additive |
| GET `/bootstrap-info` | none | Mirrors bootstrap JSON (version gating) |

**Rejections the API owns:** `409` on `lock_version` mismatch; `413` if a run exceeds sane row caps (reject ⇒ queue item stays retryable, never silently truncated); `403` on activation-window violations; `404` post-GDPR (F50 verification criterion).

**Tension flagged (not silently resolved):** REQ-DB says `runs.course_id` "set by phone on first cloud upload when a matching course is found (±2h from run start, same group via trainer)" — but "same group via trainer" logic cannot exist until P2 groups/sharing. v1 therefore always uploads `course_id = null`. → Open Question #10.

## 5. Auth Model — First Proposal

- **Server-side accounts** (REQ-DB `users` table is a cloud table — there is no phone-local account concept in the schema). v1 flow: athlete registers on the phone → server creates `users` row → JWT issued. Profile continues to live in SharedPreferences locally (F20) and mirrors to `users` on login.
- **Credential storage gap:** REQ-DB `users` has **no password/credential field**. Proposal: add `password_hash` (argon2id) — schema addition requires JP sign-off → Open Question #2.
- **Token:** JWT access token, 60 min, + refresh token, 30 d, revocable (server-side denylist). v1 could ship access-only 30 d JWT for simplicity, but refresh+revoke is strongly preferred before any non-author user holds tokens (P2).
- **Activation windows** (`ski_clubs.activation_start/end_date`, `users.activation_start/end_date`): enforced at **token issuance** (login/refresh) — expired club or user ⇒ `403` with reason code; **and** at write endpoints via a dependency check. **DECIDED (JP 2026-10-26):** expired ⇒ **writes blocked; reads and deletion still allowed**. Deletion stays open so an expired user can still erase their data.
- **Retention (JP 2026-10-26, new):** cloud run data older than **1 year** is deleted server-side (nightly purge job: `runs` with `start_datetime` older than 365 d cascade-delete their `timestamps` + `barometric_data` rows). Phone-local copies are unaffected — only cloud data is purged.
- **user_type** (`athlete, trainer, parent, friend, master`) rides in the JWT claims but is **re-checked against the DB** on each request (claims are stale-able; the DB is truth).

## 6. Sync Flow (Run Lifecycle)

```
run ends on device → BLE pull → phone local save (LocalStorage JSON)
        │
        ├─ push_to_cloud == false (F20/F23) → STOP. Stays local forever.
        │
        └─ push_to_cloud == true
                ↓
        enqueue {client_run_uuid, run_ref, status=PENDING, attempts=0}
        into DURABLE upload queue (survives app restart — F43)
                ↓
        upload worker (triggers: app start, connectivity regained,
        post-save) → POST /v1/runs (batch: timestamps + barometric_data)
                │
                ├─ 2xx → mark DONE (keep local data; raw 100 Hz never uploaded)
                ├─ 409 lock_version → refresh server copy, re-apply local
                │    edits (rename), retry once, else flag CONFLICT for UI
                ├─ 4xx validation → POISONED (surface in UI, do not retry)
                └─ 5xx / offline → exponential backoff (1m→2m→…→1h cap),
                     stays queued indefinitely (F43)
```

- **Idempotency:** `client_run_uuid` unique per run ⇒ a crash between server-commit and phone-ACK makes the retry a no-op (`200 already_exists`), not a duplicate run.
- **Conflict reality check:** `lock_version` conflicts are rare by construction (writes come from one phone; P2 trainer renames are the real source). Rule: **server wins on metadata, client data rows are immutable post-upload** (timestamps/barometric never edited, only deleted with the run) — keeps comparison integrity (F22/F32/F34).

## 7. Access-Control Enforcement (REQ-DB matrix → API rules)

Enforced **server-side in the query/dependency layer** — the client is never trusted to filter.

| Role (user_type) | Read rule (GET /runs*) | Write/delete rule | REQ-DB basis |
|---|---|---|---|
| athlete | `athlete_id = self` only | No delete (P2); rename own run | "Own runs only"; F46: athletes cannot delete |
| master | Own runs; others can see his if group `visibility=full` (P2) | Delete own runs; self-manage links | Access matrix; Relationship rule 4 |
| trainer | All runs of athletes in `users2groups(role=trainer)` groups (via users2groups + users2athletes), F25 | Add/remove group members; delete runs (F46); add parents | Rules 1; F25/F46 |
| parent | Linked athletes via `users2athletes(relationship=parent)`, **minus runs where visibility = denied** | Add friends to linked athletes | Rules 2; matrix |
| friend | Same as parent, minus `denied` | — | Rules 3; matrix |
| **Visibility filter (P2)** | Applied to every run read: `denied` → trainers only · `athlete_only` → athlete + linked followers + trainers · `full` → group members | Group default (F35) + per-run override (F36) | F35/F36 |

**Tension flagged:** F48 allows selecting runs "from the same or different athletes" for comparison, but the access matrix limits plain **athletes** to their own runs. Reading: cross-athlete selection is a trainer/parent capability (and athletes only via master-style `full` groups). → Open Question #4.

## 8. App-Side Changes (Flutter, App 1.65)

Current state: LocalStorage JSON persistence for runs; SharedPreferences for profile (settings.dart).

| Change | Detail |
|---|---|
| **Upload queue store** | New durable collection in LocalStorage: `[{client_run_uuid, local_run_id, status: PENDING/DONE/POISONED/CONFLICT, attempts, next_attempt_at, created_at}]`. Must survive restart (F43) ⇒ persisted JSON rewritten atomically (temp+rename), never memory-only |
| **Bootstrap client (I07)** | Fetch bootstrap JSON on app start + on sync failure with stale `api_base`; cache last-known-good |
| **Auth module** | Register/login screens (v1: athlete/master), secure token storage — **flutter_secure_storage, NOT SharedPreferences** (§10) |
| **Sync worker** | Background-triggered (app start, `connectivity_plus` regained, post-run-save); single-flight (no parallel uploads); respects backoff; surfaces queue status on a small sync indicator |
| **F48 browse UI** | New "Cloud runs" list: server-paged `GET /runs`, filters (athlete, date range), multi-select 1–2 runs → hand to existing comparison screens. JP 2026-10-26 #8: date must be visible + filterable; default run label = plain "Run N" (no date prefix — date is its own field) | Runs render through the same decoders, switching on `format_version` (REQ-DB `runs` note) |
| **Delete flow (F46)** | Confirmation dialog; orchestrates 3-way delete: device Flash (BLE), phone LocalStorage, `DELETE /runs/{id}` — cloud leg order: cloud first, then local, so a crash can't orphan a cloud run the user believes deleted |
| **GDPR UI (F50)** | Warning screen enumerating exactly what will be deleted (runs, timestamps, barometric_data, profile), typed confirmation, §9 |

## 9. GDPR Deletion Procedure (F50)

1. Athlete (authenticated) requests deletion in app.
2. **Prominent, explicit warning** listing exactly: all runs, all `timestamps` rows, all `barometric_data` rows, profile — **irreversible**, no export included unless previously exported (F47).
3. Typed/hold-to-confirm. App calls `POST /v1/me/gdpr-delete` with `confirm: true`.
4. Server, in one transaction: delete all runs + child rows for the athlete, delete/anonymize the `users` row (see Open Question #7), revoke all tokens.
5. **Verification (per F50):** every subsequent `GET /runs?athlete_id=<self>` and `GET /runs/{id}` returns **404**; login is impossible.
6. Backups: deleted data persists in backups until rotation — mitigation: 30-day backup retention documented in the privacy notice (Open Question #9).

## 10. Security & Privacy

- **HTTPS-only** (I07): TLS terminated at the reverse proxy, Let's Encrypt, HSTS, no plain HTTP listener beyond the 308 redirect.
- **Minimal PII (F23 as privacy feature):** the server never receives raw 100 Hz quaternion/acceleration — only gate timestamps + 10 Hz altitude/speed. PII = name, age, category, club. No location data leaves the phone in v1 (`course_gates` GPS is P3 and trainer-entered).
- **Tokens:** JWTs in `flutter_secure_storage` (Keychain/Keystore) — never SharedPreferences/LocalStorage JSON. Refresh-token rotation + revocation list.
- **Server hardening:** non-root container user, read-only FS except data volume, dependency scanning in CI, per-IP rate limit on `/auth/*`, request size caps on `/runs`.
- **No third-party analytics/trackers** in the API stack.

## 11. Decisions (JP 2026-10-26) + remaining open items

1. **SQLite-proto → PostgreSQL at P2** — ✅ ACCEPTED.
2. **Account model + credentials** — ✅ ACCEPTED: add `password_hash` (argon2id); v1 onboarding = athlete/master self-registration only.
3. **Hosting** — ✅ DECIDED (JP 2026-10-26 12:18): Hostinger VPS, domain = **sgc-api.vytsolutions.com**. ⏳ REMAINING: DNS A record → VPS IP + Hostinger panel firewall 80/443 inbound.
4. **F48 cross-athlete comparison** — ✅ ACCEPTED (trainer/parent capability). JP: rules will be redefined with real use.
5. **v1 scope cut** — ✅ ACCEPTED (upload / browse own / delete-own / GDPR; trainer viewing, visibility, relationships → P2).
6. **Activation-window semantics** — ✅ DECIDED: writes blocked after expiry; reads + deletion allowed. Plus NEW: 1-year server-side retention purge (see §5).
7. **GDPR erasure depth** — ✅ DECIDED (JP 2026-10-26 12:18): v1 = full hard-delete (user row included — everything 404s). Email-hash denylist only if abuse appears later.
8. **Run identity** — ✅ DECIDED: server run ID unique (primary key). The daily `run_number` stays an informational label — no server uniqueness constraint (phone deletions can legitimately renumber).
9. **Backups vs GDPR** — ✅ RESOLVED: 30-day backup rotation = documented purge mechanism (privacy note states deleted data vanishes from backups within ≤30 days).
10. **course_id = null in v1** — ✅ ACCEPTED (auto-match activates with P2 groups).

## 12. Phased Rollout

| Phase | Deliverable | Requirements covered |
|---|---|---|
| **P0 — proto** | `sgc-api` container + SQLite + Alembic + auth skeleton + `/runs` batch upload smoke-tested with curl; bootstrap JSON live | I07 plumbing, F23 payload shape |
| **P1 — upload + browse** | App: queue (F43), sync worker, own-run cloud browse, profile sync, GDPR flow (F50), cloud delete leg | F20, F23, F43, F45, F48 (own runs), F50, F46 (own cloud leg) |
| **P2 — sharing + roles** | Postgres migration; trainer/parent/friend accounts; groups + users2athletes; visibility engine (F35/F36); trainer viewing (F25); full F46 delete rights | F25, F35, F36, F46, access matrix |
| **P3 — comparisons + courses** | Cloud-backed 1–2 run comparison views across permitted athletes; courses API + course auto-match on upload | F22, F32, F34, F48 (full), F57.1–F57.4 tables/API |

---

*Draft ends. Nothing committed; awaiting JP review — especially §11 items 1–5.*
