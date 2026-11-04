"""/v1/runs — the F23 cloud payload: run metadata + timestamps[] +
barometric_data[], committed in ONE transaction (§2 guardrail 4).

Idempotency: client_run_uuid is unique per run; a retry after a lost ACK
returns 200 with the EXISTING run + already_exists=true — never a
duplicate (F43 offline queue re-sends).

Optimistic concurrency: lock_version is server-owned (1 on create, +1 on
every write). Renames require If-Match: <lock_version>; mismatch ⇒ 409 (§4).
"""

from datetime import datetime

from fastapi import APIRouter, Header, HTTPException, Query, Response, status
from sqlalchemy import delete, select
from sqlalchemy.orm import Session

from app.config import get_settings
from app.db import ensure_utc, to_db_utc
from app.deps import DbDep, UserDep, WriteAllowedDep
from app.models import BarometricData, Run, Timestamp, User
from app.schemas import (
    RunCreate,
    RunCreatedOut,
    RunDetailOut,
    RunListOut,
    RunOut,
    RunRename,
)

router = APIRouter(prefix="/v1/runs", tags=["runs"])


def _run_out(run: Run, already_exists: bool | None = None) -> RunOut:
    return RunOut(
        id=run.id,
        client_run_uuid=run.client_run_uuid,
        start_datetime=ensure_utc(run.start_datetime),  # always ship aware-UTC
        athlete_id=run.athlete_id,
        run_number=run.run_number,
        visibility=run.visibility,
        name=run.name,
        format_version=run.format_version,
        lock_version=run.lock_version,
        course_id=run.course_id,
        run_type=run.run_type,
        source_run_ids=run.source_run_ids,
        already_exists=already_exists,
    )


def _get_owned_run(db: Session, user: User, run_id: int) -> Run:
    """v1: users see only their own runs (§4). A foreign or missing run is a
    plain 404 — never leak existence."""
    run = db.get(Run, run_id)
    if run is None or run.athlete_id != user.id:
        raise HTTPException(status.HTTP_404_NOT_FOUND, "Run not found")
    return run


@router.post("", response_model=RunCreatedOut)
def upload_run(
    payload: RunCreate,
    user: WriteAllowedDep,
    db: DbDep,
    response: Response,
) -> RunCreatedOut:
    """Batch upload. 201 = created; 200 + already_exists=true = idempotent
    retry (same client_run_uuid) — the queue item is DONE either way (§6).
    Upload is a WRITE ⇒ activation-window gate applies.
    v1: athlete/master upload as themselves (§4); athlete_id is implied."""
    cfg = get_settings()
    if len(payload.timestamps) > cfg.max_timestamps_per_run:
        raise HTTPException(
            status.HTTP_413_CONTENT_TOO_LARGE,
            f"More than {cfg.max_timestamps_per_run} timestamps",
        )
    if len(payload.barometric_data) > cfg.max_barometric_rows_per_run:
        raise HTTPException(
            status.HTTP_413_CONTENT_TOO_LARGE,
            f"More than {cfg.max_barometric_rows_per_run} barometric rows",
        )

    # Idempotency check BEFORE insert — the common retry path is a no-op.
    existing = db.scalar(
        select(Run).where(Run.client_run_uuid == payload.client_run_uuid)
    )
    if existing is not None:
        response.status_code = status.HTTP_200_OK
        return RunCreatedOut(
            id=existing.id,
            lock_version=existing.lock_version,
            already_exists=True,
        )

    run = Run(
        client_run_uuid=payload.client_run_uuid,
        start_datetime=to_db_utc(payload.start_datetime),  # canonical naive-UTC
        athlete_id=user.id,
        run_number=payload.run_number,
        visibility=payload.visibility,
        name=payload.name,
        format_version=payload.format_version,
        lock_version=1,  # server-owned, always starts at 1 (§4)
        course_id=None,  # §11.10: always null in v1
        run_type=payload.run_type,  # P1.5: 'single' | 'merged'
        source_run_ids=payload.source_run_ids,  # P1.5 merged lineage (may be None)
    )
    # One transaction for meta + children (§2 guardrail 4): nothing commits
    # until db.commit() below — a crash mid-upload leaves no partial run.
    run.timestamps = [
        Timestamp(timestamp_ms=t.timestamp_ms, arm_side=t.arm_side, guessed=t.guessed)
        for t in payload.timestamps
    ]
    run.barometric_data = [
        BarometricData(
            time_offset_ms=b.time_offset_ms,
            altitude_m=b.altitude_m,
            speed_mps=b.speed_mps,
        )
        for b in payload.barometric_data
    ]
    db.add(run)
    try:
        db.commit()
    except Exception:
        # Lost race: another request committed the same client_run_uuid
        # between our SELECT and COMMIT (queue retry storm). Not a 500 —
        # the run exists, so this IS the idempotent-retry outcome.
        db.rollback()
        existing = db.scalar(
            select(Run).where(Run.client_run_uuid == payload.client_run_uuid)
        )
        if existing is None:
            raise
        response.status_code = status.HTTP_200_OK
        return RunCreatedOut(
            id=existing.id,
            lock_version=existing.lock_version,
            already_exists=True,
        )
    response.status_code = status.HTTP_201_CREATED
    return RunCreatedOut(id=run.id, lock_version=run.lock_version, already_exists=False)


@router.get("", response_model=RunListOut)
def list_runs(
    user: UserDep,
    db: DbDep,
    athlete_id: int | None = Query(default=None),
    start_from: datetime | None = Query(default=None, alias="from"),
    start_to: datetime | None = Query(default=None, alias="to"),
    limit: int = Query(default=50, ge=1, le=200),
    cursor: str | None = Query(default=None),
) -> RunListOut:
    """Own runs only (v1, §4/§7). Deterministic order: start_datetime DESC,
    id DESC (newest first; id breaks same-start ties) — insertion order
    must never leak through the API (JP 2026-10-27). Keyset cursor stays
    id-based (opaque to the client); pages are exact for same-start rows,
    which is the normal phone-upload case."""
    if athlete_id is not None and athlete_id != user.id:
        # P2 adds group/follower reads per the access matrix; until then a
        # foreign athlete_id is out of scope, not an empty result.
        raise HTTPException(status.HTTP_403_FORBIDDEN, "Cannot list other athletes' runs (P2)")

    query = select(Run).where(Run.athlete_id == user.id)
    if start_from is not None:
        query = query.where(Run.start_datetime >= to_db_utc(start_from))
    if start_to is not None:
        query = query.where(Run.start_datetime <= to_db_utc(start_to))
    if cursor is not None:
        try:
            query = query.where(Run.id < int(cursor))
        except ValueError as exc:
            raise HTTPException(status.HTTP_400_BAD_REQUEST, "Invalid cursor") from exc

    # Deterministic order (JP 2026-10-27): newest start first, higher id as
    # the tiebreak — naive-UTC storage keeps the SQLite comparison correct.
    rows = db.scalars(
        query.order_by(Run.start_datetime.desc(), Run.id.desc()).limit(limit + 1)
    ).all()
    # More rows than the page ⇒ cursor = last id OF THIS PAGE (rows[limit]
    # is merely the peek proving another page exists).
    next_cursor = str(rows[limit - 1].id) if len(rows) > limit else None
    return RunListOut(
        items=[_run_out(r) for r in rows[:limit]],
        next_cursor=next_cursor,
    )


@router.get("/{run_id}", response_model=RunDetailOut)
def get_run(
    run_id: int,
    user: UserDep,
    db: DbDep,
    include: str = Query(default=""),
) -> RunDetailOut:
    """Run detail; ?include=timestamps,barometric_data renders the viewer /
    comparison payload (F48 → F22/F32/F34)."""
    run = _get_owned_run(db, user, run_id)
    wants = {part.strip() for part in include.split(",") if part.strip()}
    detail = RunDetailOut(
        **_run_out(run).model_dump(),
        timestamps=run.timestamps if "timestamps" in wants else [],
        barometric_data=run.barometric_data if "barometric_data" in wants else [],
    )
    return detail


@router.put("/{run_id}", response_model=RunOut)
def rename_run(
    run_id: int,
    payload: RunRename,
    user: WriteAllowedDep,
    db: DbDep,
    if_match: str | None = Header(default=None, alias="If-Match"),
) -> RunOut:
    """Rename (F45). Rename is a WRITE ⇒ activation gate. Precondition:
    If-Match must carry the caller's known lock_version —
      missing  ⇒ 428, mismatch ⇒ 409, hit ⇒ lock_version += 1."""
    run = _get_owned_run(db, user, run_id)
    if if_match is None:
        raise HTTPException(
            status.HTTP_428_PRECONDITION_REQUIRED,
            "If-Match: <lock_version> required",
        )
    try:
        expected = int(if_match.strip())
    except ValueError as exc:
        raise HTTPException(status.HTTP_400_BAD_REQUEST, "If-Match must be an integer") from exc
    if expected != run.lock_version:
        raise HTTPException(
            status.HTTP_409_CONFLICT,
            {"code": "lock_version_mismatch",
             "detail": f"Expected lock_version {expected}, current is {run.lock_version}"},
        )
    run.name = payload.name
    run.lock_version += 1  # §4: incremented on EVERY write
    db.commit()
    return _run_out(run)


@router.delete("/{run_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_run(run_id: int, user: UserDep, db: DbDep) -> Response:
    """Cloud leg of the 3-way delete (F46) — 204, child rows cascade.
    Deletion is NEVER activation-blocked (§5: expired users can still
    erase their data), hence plain UserDep, not the write gate."""
    run = _get_owned_run(db, user, run_id)
    # Explicit child deletes keep the cascade obvious and work even where
    # DB-level FK enforcement is unavailable; run row goes in the same txn.
    db.execute(delete(Timestamp).where(Timestamp.run_id == run.id))
    db.execute(delete(BarometricData).where(BarometricData.run_id == run.id))
    db.delete(run)
    db.commit()
    return Response(status_code=status.HTTP_204_NO_CONTENT)
