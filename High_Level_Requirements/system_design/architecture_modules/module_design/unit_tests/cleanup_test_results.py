#!/usr/bin/env python3
"""
Prune old harness artifacts in tmp_test_results/ so the drop folder does not
grow forever. Deletes only run_YYYYMMDD_HHMM* artifacts; never touches
README.md / secrets*.env or anything outside tmp_test_results/.

Usage (from unit_tests/):
  py cleanup_test_results.py                          # keep 10 runs / 14 days
  py cleanup_test_results.py --dry-run                # preview only
  py cleanup_test_results.py --keep-runs 15
  py cleanup_test_results.py --max-age-days 7
"""
from __future__ import print_function

import argparse
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_RESULTS_DIR = os.path.join(HERE, "tmp_test_results")

RUN_RE = re.compile(r"^(run_\d{8}_\d{4})")
PROTECTED = {"README.md", "secrets.example.env", "secrets.env"}


def main():
    p = argparse.ArgumentParser(description="Prune old SGC run artifacts in tmp_test_results/")
    p.add_argument("--keep-runs", type=int, default=10,
                   help="Keep the N most recent run groups (default 10)")
    p.add_argument("--max-age-days", type=int, default=14,
                   help="Also delete runs older than this many days (default 14)")
    p.add_argument("--dry-run", action="store_true", help="List what would be deleted")
    p.add_argument("--results-dir", default=DEFAULT_RESULTS_DIR,
                   help="Directory to prune (default: tmp_test_results under script)")
    args = p.parse_args()

    if not os.path.isdir(args.results_dir):
        print("No results dir: %s" % args.results_dir)
        return 0

    cutoff = time.time() - args.max_age_days * 86400.0

    # Group run_* files by run id prefix (run_YYYYMMDD_HHMM).
    groups = {}
    skipped = []
    for name in sorted(os.listdir(args.results_dir)):
        if name in PROTECTED:
            continue
        m = RUN_RE.match(name)
        if not m:
            if name.startswith("run_"):
                skipped.append(name)
            continue
        path = os.path.join(args.results_dir, name)
        if not os.path.isfile(path):
            continue
        try:
            mtime = os.path.getmtime(path)
        except OSError:
            continue
        groups.setdefault(m.group(1), []).append((path, name, mtime))

    # Order groups newest-first by their most recent file.
    ordered = []
    for gid, files in groups.items():
        newest = max(mtime for _, _, mtime in files)
        ordered.append((gid, files, newest))
    ordered.sort(key=lambda x: x[2], reverse=True)

    to_delete = []
    for index, (gid, files, newest) in enumerate(ordered):
        outside_keep = index >= args.keep_runs
        too_old = newest < cutoff
        if outside_keep or too_old:
            for path, name, _ in files:
                to_delete.append((path, name, gid))

    deleted_gids = set(gid for _, _, gid in to_delete)
    kept_groups = len(ordered) - len(deleted_gids)

    if not to_delete:
        print("Nothing to prune (%d run group(s), keep %d, max age %d days)."
              % (len(ordered), args.keep_runs, args.max_age_days))
        if skipped:
            print("Skipped (not run_YYYYMMDD_HHMM): %s" % ", ".join(skipped))
        return 0

    print("Will %s %d file(s) across %d run group(s); keeping %d group(s)."
          % ("delete" if not args.dry_run else "delete (dry-run)",
             len(to_delete), len(deleted_gids), kept_groups))
    for path, name, gid in sorted(to_delete, key=lambda x: x[2]):
        print("  %s %s" % ("-" if args.dry_run else "DELETED", name))
        if not args.dry_run:
            try:
                os.remove(path)
            except OSError as e:
                print("  WARNING: could not remove %s: %s" % (name, e))
    if skipped:
        print("Skipped (not run_YYYYMMDD_HHMM): %s" % ", ".join(skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
