"""Reconciliation script (JP naming scheme v2): backfill, custom-name
safety, idempotency. Runs against the same migrated test DB as the API
suite (conftest rebuilds it per test — no create_all)."""

from datetime import datetime

from sqlalchemy import select

from app.db import SessionLocal
from app.models import Run, User
from scripts.reconcile_names import main, reconcile_run_names


def _mk_run(
    db,
    athlete_id: int,
    uuid: str,
    run_number: int,
    *,
    run_type: str = "single",
    source_run_ids: list[int] | None = None,
    name: str | None = None,
) -> Run:
    run = Run(
        client_run_uuid=uuid,
        start_datetime=datetime(2026, 1, 1, 10, 0, 0),
        athlete_id=athlete_id,
        run_number=run_number,
        format_version=1,
        lock_version=1,
        name=name,
        run_type=run_type,
        source_run_ids=source_run_ids,
    )
    db.add(run)
    return run


def _seed_user(db) -> int:
    user = User(name="rec-user", user_type="athlete", password_hash="not-a-real-hash")
    db.add(user)
    db.commit()
    return user.id


def test_reconcile_backfills_singles_and_merges_custom_kept():
    with SessionLocal() as db:
        uid = _seed_user(db)
        s1 = _mk_run(db, uid, "rec-s1", 3)
        s2 = _mk_run(db, uid, "rec-s2", 7)
        db.commit()
        merged = _mk_run(
            db, uid, "rec-m", 1, run_type="merged", source_run_ids=[s1.id, s2.id]
        )
        orphan = _mk_run(
            db, uid, "rec-orphan", 2, run_type="merged", source_run_ids=[424242]
        )
        custom = _mk_run(db, uid, "rec-custom", 9, name="My best line")
        db.commit()

        summary = reconcile_run_names(db)
        db.commit()

        # Singles + merge get the JP scheme names, sources in lineage order.
        assert s1.name == "A#3" and s2.name == "A#7"
        assert merged.name == "A#3 + A#7"
        # Custom user name untouched; merge with a missing source row untouched.
        assert custom.name == "My best line"
        assert orphan.name is None
        assert (summary.singles_renamed, summary.merges_renamed) == (2, 1)
        assert summary.custom_kept == 1
        assert summary.merges_skipped == 1
        assert summary.skipped_rows == [(orphan.id, "missing source run(s) [424242]")]


def test_reconcile_idempotent_second_run():
    with SessionLocal() as db:
        uid = _seed_user(db)
        s1 = _mk_run(db, uid, "rec2-s1", 5)
        db.commit()
        merged = _mk_run(
            db, uid, "rec2-m", 1, run_type="merged", source_run_ids=[s1.id]
        )
        custom = _mk_run(db, uid, "rec2-custom", 9, name="Race day!")
        already = _mk_run(db, uid, "rec2-already", 5, name="A#5")
        db.commit()

        first = reconcile_run_names(db)
        db.commit()
        assert (first.singles_renamed, first.merges_renamed) == (1, 1)
        names = {r.id: r.name for r in (s1, merged, custom, already)}

        # Second pass: nothing renamed, everything classified already-ok/kept.
        second = reconcile_run_names(db)
        db.commit()
        assert second.singles_renamed == 0 and second.merges_renamed == 0
        assert second.already_ok == 3  # s1, merged, already
        assert second.custom_kept == 1
        assert {r.id: r.name for r in (s1, merged, custom, already)} == names


def test_reconcile_cli_dry_run_then_apply():
    with SessionLocal() as db:
        uid = _seed_user(db)
        _mk_run(db, uid, "rec3-s1", 4)
        db.commit()

    # --dry-run (default is APPLY): computes + prints but must not write.
    assert main(["--dry-run"]) == 0
    with SessionLocal() as db:
        run = db.scalar(select(Run).where(Run.client_run_uuid == "rec3-s1"))
        assert run.name is None

    # Default = apply.
    assert main([]) == 0
    with SessionLocal() as db:
        run = db.scalar(select(Run).where(Run.client_run_uuid == "rec3-s1"))
        assert run.name == "A#4"
