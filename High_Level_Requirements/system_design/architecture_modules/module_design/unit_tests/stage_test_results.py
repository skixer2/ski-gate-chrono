#!/usr/bin/env python3
"""
Move/copy loose harness artifacts into tmp_test_results/.

Usage (from unit_tests/):
  py stage_test_results.py              # move run_* .md/.log into tmp_test_results/
  py stage_test_results.py --copy       # copy instead of move
  py stage_test_results.py --pattern 'run_20260814*'
"""
from __future__ import print_function

import argparse
import glob
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DEST = os.path.join(HERE, "tmp_test_results")


def main():
    p = argparse.ArgumentParser(description="Stage SGC harness results into tmp_test_results/")
    p.add_argument("--dest", default=DEFAULT_DEST, help="Destination directory")
    p.add_argument("--copy", action="store_true", help="Copy instead of move")
    p.add_argument("--pattern", default="run_*",
                   help="Glob under cwd / unit_tests (default: run_*)")
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()

    os.makedirs(args.dest, exist_ok=True)
    roots = [os.getcwd(), HERE]
    seen = set()
    files = []
    for root in roots:
        for path in glob.glob(os.path.join(root, args.pattern)):
            ap = os.path.abspath(path)
            if ap in seen:
                continue
            if os.path.isdir(ap):
                continue
            # Only stage summary/log style artifacts
            base = os.path.basename(ap)
            if not (base.endswith(".md") or base.endswith(".log") or base.endswith(".txt")):
                continue
            # Skip if already inside dest
            if os.path.abspath(args.dest) in ap:
                continue
            seen.add(ap)
            files.append(ap)

    if not files:
        print("No matching artifacts to stage.")
        return 0

    op = shutil.copy2 if args.copy else shutil.move
    verb = "COPY" if args.copy else "MOVE"
    for src in sorted(files):
        dst = os.path.join(args.dest, os.path.basename(src))
        print("%s %s -> %s" % (verb, src, dst))
        if not args.dry_run:
            if os.path.exists(dst) and not args.copy:
                os.remove(dst)
            op(src, dst)
    print("Staged %d file(s) into %s" % (len(files), args.dest))
    return 0


if __name__ == "__main__":
    sys.exit(main())
