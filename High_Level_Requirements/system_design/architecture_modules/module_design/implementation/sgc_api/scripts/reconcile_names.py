r"""One-time run-name reconciliation — JP naming scheme v2 (2026-10-27).

Scheme: a run's cloud name is "<device-letter>#<run_number>", where
run_number is the PAIRED DEVICE's own counter (runs.run_number) and the
letter identifies the paired device. Current deployment = one paired
device → "A". A phone-side merged run names itself after its sources:
"A#3 + A#7".

Rows uploaded before the phone started sending names have name IS NULL;
this script backfills them from runs.run_number / runs.source_run_ids.
Safety contract:

  * ONLY rows whose name is NULL or already machine-generated are touched
    (pattern: ^A#\d+( \+ A#\d+)*$ — the single and merged auto shapes).
    A CUSTOM user name is never matched, hence never overwritten.
  * merged rows are recomposed from each source's run_number, resolved
    via source_run_ids in list order; if the lineage is empty or any
    source row is missing (pre-cloud / deleted), the row is left
    untouched and logged.
  * idempotent: names are a pure function of (run_number, lineage), so a
    second pass recomputes the same values and changes nothing.

Usage against the LIVE SQLite volume (repo root inside the container;
the image must include scripts/ — see Dockerfile):

    docker exec sgc-api python -m scripts.reconcile_names             # apply
    docker exec sgc-api python -m scripts.reconcile_names --dry-run   # preview
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field

from sqlalchemy import select
from sqlalchemy.orm import Session

from app.db import SessionLocal
from app.models import Run

# Letter of the currently paired device (JP naming scheme v2): one device
# today → "A". Extra devices extend this later (B, C, ...).
DEVICE_LETTER = "A"

# Machine-generated names ONLY — anything else is a CUSTOM user name and is
# never touched. One term = single shape, more terms = merged shape.
_AUTO_NAME = re.compile(
    rf"^{re.escape(DEVICE_LETTER)}#\d+( \+ {re.escape(DEVICE_LETTER)}#\d+)*$"
)


def is_auto_name(name: str | None) -> bool:
    """True when name IS NULL or matches the auto-generated pattern (safe to
    rewrite). Custom user names never match → never overwritten."""
    return name is None or _AUTO_NAME.fullmatch(name) is not None


def single_name(run_number: int) -> str:
    return f"{DEVICE_LETTER}#{run_number}"


def merged_name(source_run_numbers: list[int]) -> str:
    return " + ".join(f"{DEVICE_LETTER}#{n}" for n in source_run_numbers)


@dataclass
class ReconcileSummary:
    total: int = 0
    singles_renamed: int = 0
    merges_renamed: int = 0
    already_ok: int = 0  # name already equals the computed value (2nd run)
    custom_kept: int = 0  # custom user names, untouched
    merges_skipped: int = 0  # missing sources / no lineage → untouched
    skipped_rows: list[tuple[int, str]] = field(default_factory=list)


def reconcile_run_names(db: Session) -> ReconcileSummary:
    """Compute + assign reconciled names on `db` WITHOUT committing — the
    caller decides (so --dry-run can simply roll back)."""
    summary = ReconcileSummary()
    runs = db.scalars(select(Run)).all()
    numbers_by_id = {r.id: r.run_number for r in runs}
    summary.total = len(runs)

    for run in runs:
        if not is_auto_name(run.name):
            summary.custom_kept += 1
            continue

        if run.run_type == "merged":
            src_ids = list(run.source_run_ids or [])
            missing = [sid for sid in src_ids if sid not in numbers_by_id]
            if not src_ids or missing:
                reason = (
                    f"missing source run(s) {missing}"
                    if missing
                    else "no lineage (source_run_ids empty)"
                )
                summary.merges_skipped += 1
                summary.skipped_rows.append((run.id, reason))
                print(f"skip run {run.id}: merged, {reason} — name left as {run.name!r}")
                continue
            new_name = merged_name([numbers_by_id[sid] for sid in src_ids])
        else:
            new_name = single_name(run.run_number)

        if run.name == new_name:
            summary.already_ok += 1
            continue
        print(f"run {run.id}: {run.name!r} -> {new_name!r}")
        run.name = new_name
        if run.run_type == "merged":
            summary.merges_renamed += 1
        else:
            summary.singles_renamed += 1

    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Backfill runs.name per the JP naming scheme "
        f"('{DEVICE_LETTER}#<run_number>'; merged = '{DEVICE_LETTER}#x + "
        f"{DEVICE_LETTER}#y'). Idempotent; custom user names are never touched."
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="preview only: compute and print, do not commit",
    )
    args = parser.parse_args(argv)

    with SessionLocal() as db:
        summary = reconcile_run_names(db)
        if args.dry_run:
            db.rollback()
            verdict = "DRY RUN — nothing committed"
        else:
            db.commit()  # one transaction: all renames land or none do
            verdict = "APPLIED (one transaction)"
        changed = summary.singles_renamed + summary.merges_renamed
        print(
            f"\nreconcile_names [{verdict}]: "
            f"{summary.total} run(s) scanned, {changed} renamed "
            f"({summary.singles_renamed} single, {summary.merges_renamed} merged), "
            f"{summary.already_ok} already correct, "
            f"{summary.custom_kept} custom name(s) kept, "
            f"{summary.merges_skipped} merged row(s) skipped"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
