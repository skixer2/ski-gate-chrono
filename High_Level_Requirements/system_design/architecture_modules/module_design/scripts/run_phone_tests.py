#!/usr/bin/env python3
"""
run_phone_tests.py — SGC Phone App test runner with per-test output files.

Usage:
    python scripts/run_phone_tests.py                  # run all, save per-test outputs
    python scripts/run_phone_tests.py --file decompressor  # run one test file
    python scripts/run_phone_tests.py --latest          # show latest results

Output:
    test/results/YYYY-MM-DD_HHMMSS/
        summary.json              — index with links to all outputs
        decompressor_test/
            01_parses_valid_16_byte_header.log
            02_throws_on_short_data.log
            ...
        impact_detector_test/
            01_detects_single_impact_spike.log
            ...

Equivalent to the device test pattern where each scenario produces
its own output file + a summary index.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

# ═══════════════════════════════════════════════════════════════════
# Paths
# ═══════════════════════════════════════════════════════════════════

SCRIPT_DIR = Path(__file__).resolve().parent
IMPL_DIR = SCRIPT_DIR.parent / "implementation"
PROJECT_DIR = IMPL_DIR / "Phone_app_prototype"
RESULTS_ROOT = PROJECT_DIR / "test" / "results"


def _find_flutter():
    for name in ["flutter", "flutter.bat"]:
        found = shutil.which(name)
        if found:
            return found
    # Windows fallbacks
    for base in ["F:\\Programs\\flutter\\bin\\flutter.bat",
                 "C:\\flutter\\bin\\flutter.bat"]:
        if os.path.isfile(base):
            return base
    return "flutter"


def _run(cmd, **kwargs):
    """Run a command and return (code, stdout, stderr)."""
    kwargs.setdefault("cwd", str(PROJECT_DIR))
    kwargs.setdefault("capture_output", True)
    kwargs.setdefault("text", True)
    kwargs.setdefault("timeout", 120)
    if os.name == "nt":
        kwargs["shell"] = True
        if isinstance(cmd, list):
            cmd = " ".join(cmd)
    return subprocess.run(cmd, **kwargs)


# ═══════════════════════════════════════════════════════════════════
# ANSI / terminal cleanup
# ═══════════════════════════════════════════════════════════════════

def _clean(text):
    """Strip ANSI escape codes and normalize line endings."""
    clean = re.sub(r'\x1b\[[0-9;]*m', '', text)
    clean = clean.replace('\r\n', '\n').replace('\r', '\n')
    return clean


def _safe_filename(name, max_len=50):
    """Sanitize a test name into a safe filename fragment.

    Replaces chars that cause trouble on Windows/Linux filesystems,
    strips trailing dots/spaces (which Windows silently drops),
    and truncates to max_len.
    """
    # Replace filesystem-hostile and markdown-link-breaking characters
    safe = re.sub(r'[<>:"/\\|?*()\[\]\s]', '_', name)
    # Collapse runs of underscores
    safe = re.sub(r'_{2,}', '_', safe)
    # Strip leading/trailing dots, underscores
    safe = safe.strip('._')
    # Truncate
    safe = safe[:max_len]
    # Strip again after truncation (may have cut mid-word)
    safe = safe.rstrip('._')
    if not safe:
        safe = 'unnamed'
    return safe


def _link(rel_path):
    """URL-encode a relative path for use in markdown links.

    Replaces spaces and other chars that break markdown link parsing.
    """
    return rel_path.replace(' ', '%20').replace('(', '%28').replace(')', '%29')
# ═══════════════════════════════════════════════════════════════════

def discover_test_files():
    """Return sorted list of *_test.dart paths relative to PROJECT_DIR."""
    test_dir = PROJECT_DIR / "test"
    files = sorted(test_dir.rglob("*_test.dart"))
    return [f.relative_to(PROJECT_DIR).as_posix() for f in files]


def discover_test_cases(test_file):
    """Parse a test file to extract individual test case names."""
    path = PROJECT_DIR / test_file
    if not path.exists():
        return []
    cases = []
    content = path.read_text(encoding="utf-8", errors="replace")
    for m in re.finditer(r"""test\(\s*['\"](.+?)['\"]""", content):
        cases.append(m.group(1))
    return cases


# ═══════════════════════════════════════════════════════════════════
# Runner
# ═══════════════════════════════════════════════════════════════════

def run_test_file(test_file):
    """Run a single test file and return parsed results."""
    flutter = _find_flutter()
    cmd = [flutter, "test", str(test_file)]
    result = _run(cmd)
    return result.returncode, _clean(result.stdout), _clean(result.stderr)


def run_single_test(test_name):
    """Run a single test case by --plain-name."""
    flutter = _find_flutter()
    cmd = [flutter, "test", "--plain-name", test_name]
    result = _run(cmd)
    return result.returncode, _clean(result.stdout), _clean(result.stderr)


def parse_test_output(stdout):
    """Parse flutter test output to extract per-test results.

    Each test appears twice in the output (start + done).
    We keep only the last occurrence to get the final result.
    """
    seen = {}

    for line in stdout.split('\n'):
        line = line.strip()
        if not line:
            continue

        # Progress line: "00:03 +5 -1: Test name [E]"
        # Skip "loading" lines (file paths) and summary lines
        m = re.match(r'\d{2}:\d{2}\s+\+\d+\s*(-\d+)?\s*:?\s*(.+)', line)
        if not m:
            continue

        name = m.group(2).strip()

        # Skip loading/summary noise and path-like strings
        if 'loading' in name.lower():
            continue
        if name.startswith('/') or name.startswith('F:') or 'workspace' in name:
            continue
        if 'All tests passed' in name or 'Some tests failed' in name:
            continue

        # Update (overwrite) — keep last occurrence
        has_error = '[E]' in line
        has_fail = (m.group(1) is not None)  # "-N" present
        seen[name] = {
            "name": name,
            "result": "failure" if (has_error or has_fail) else "success",
        }

    # If flutter test summary says all passed, mark remaining as success
    if 'All tests passed' in stdout:
        for v in seen.values():
            if v["result"] != "failure":
                v["result"] = "success"

    return list(seen.values())


def run_all_per_test(test_file_filter=None):
    """Run each test case individually, save per-case output files.

    Returns (results_dir, summary).
    """
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d_%H%M%S")
    run_dir = RESULTS_ROOT / timestamp
    run_dir.mkdir(parents=True, exist_ok=True)

    test_files = discover_test_files()
    if test_file_filter:
        test_files = [f for f in test_files if test_file_filter in f]

    total = 0
    passed = 0
    failed = 0
    summary_tests = []

    for test_file in test_files:
        file_name = Path(test_file).stem  # e.g. "decompressor_test"
        file_dir = run_dir / file_name
        file_dir.mkdir(parents=True, exist_ok=True)

        cases = discover_test_cases(test_file)
        if not cases:
            continue

        print(f"  {file_name} ({len(cases)} tests)...", end=" ", flush=True)

        file_passed = 0
        file_failed = 0
        file_tests = []

        for i, case_name in enumerate(cases):
            total += 1
            code, stdout, stderr = run_single_test(case_name)

            # Build safe filename
            safe = _safe_filename(case_name)
            num = f"{i + 1:02d}"
            case_file = file_dir / f"{num}_{safe}.log"

            is_pass = (code == 0)
            if is_pass:
                passed += 1
                file_passed += 1
                result = "success"
            else:
                failed += 1
                file_failed += 1
                result = "failure"

            # Write per-test output
            with open(case_file, "w", encoding="utf-8") as f:
                f.write(f"# Test: {case_name}\n")
                f.write(f"# File: {test_file}\n")
                f.write(f"# Result: {result}\n")
                f.write(f"# Exit code: {code}\n")
                f.write(f"# {'='*60}\n\n")
                f.write(stdout)
                if stderr:
                    f.write("\n\n# STDERR:\n")
                    f.write(stderr)

            rel_path = case_file.relative_to(run_dir).as_posix()
            file_tests.append({
                "name": case_name,
                "result": result,
                "output": rel_path,
            })

        summary_tests.append({
            "file": test_file,
            "passed": file_passed,
            "failed": file_failed,
            "total": file_passed + file_failed,
            "tests": file_tests,
        })

        status = "✅" if file_failed == 0 else f"❌ {file_failed} fail"
        print(status)

    # Write summary index
    summary = {
        "timestamp": timestamp,
        "total": total,
        "passed": passed,
        "failed": failed,
        "files": summary_tests,
    }
    summary_path = run_dir / "summary.json"
    with open(summary_path, "w") as f:
        json.dump(summary, f, indent=2)

    # Write human-readable markdown with clickable links
    _write_summary_md(run_dir, summary)

    # Update latest pointer
    latest_path = RESULTS_ROOT / "latest.json"
    with open(latest_path, "w") as f:
        json.dump(summary, f, indent=2)

    return run_dir, summary


# ═══════════════════════════════════════════════════════════════════
# Fast mode: one run per file (not per test case)
# ═══════════════════════════════════════════════════════════════════

def run_all_per_file(test_file_filter=None):
    """Run each test file once, save per-file output + parse into per-test files."""
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d_%H%M%S")
    run_dir = RESULTS_ROOT / timestamp
    run_dir.mkdir(parents=True, exist_ok=True)

    test_files = discover_test_files()
    if test_file_filter:
        test_files = [f for f in test_files if test_file_filter in f]

    total = 0
    passed = 0
    failed = 0
    summary_files = []

    for test_file in test_files:
        file_name = Path(test_file).stem
        file_dir = run_dir / file_name
        file_dir.mkdir(parents=True, exist_ok=True)

        print(f"  {file_name}...", end=" ", flush=True)
        code, stdout, stderr = run_test_file(test_file)

        # Save raw file output
        raw_file = file_dir / "_raw.log"
        with open(raw_file, "w", encoding="utf-8") as f:
            f.write(stdout)
            if stderr:
                f.write("\n\n# STDERR:\n")
                f.write(stderr)

        # Parse into individual test outputs
        tests = parse_test_output(stdout)
        file_tests = []
        file_passed = 0
        file_failed = 0

        for i, t in enumerate(tests):
            total += 1
            safe = _safe_filename(t["name"])
            num = f"{i + 1:02d}"
            case_file = file_dir / f"{num}_{safe}.log"

            # Belt-and-suspenders: ensure parent dir exists
            file_dir.mkdir(parents=True, exist_ok=True)

            is_pass = (t["result"] == "success")
            if is_pass:
                passed += 1
                file_passed += 1
            else:
                failed += 1
                file_failed += 1

            with open(case_file, "w", encoding="utf-8") as f:
                f.write(f"# Test: {t['name']}\n")
                f.write(f"# File: {test_file}\n")
                f.write(f"# Result: {t['result']}\n")
                if t.get("error"):
                    f.write(f"# Error: {t['error']}\n")
                f.write(f"# {'='*60}\n")

            rel_path = case_file.relative_to(run_dir).as_posix()
            file_tests.append({
                "name": t["name"],
                "result": t["result"],
                "output": rel_path,
                "error": t.get("error"),
            })

        summary_files.append({
            "file": test_file,
            "passed": file_passed,
            "failed": file_failed,
            "total": file_passed + file_failed,
            "raw_output": raw_file.relative_to(run_dir).as_posix(),
            "tests": file_tests,
        })

        status = "✅" if file_failed == 0 else f"❌ {file_failed} fail"
        print(status)

    summary = {
        "timestamp": timestamp,
        "total": total,
        "passed": passed,
        "failed": failed,
        "files": summary_files,
    }
    summary_path = run_dir / "summary.json"
    with open(summary_path, "w") as f:
        json.dump(summary, f, indent=2)

    # Write human-readable markdown with clickable links
    _write_summary_md(run_dir, summary)

    latest_path = RESULTS_ROOT / "latest.json"
    with open(latest_path, "w") as f:
        json.dump(summary, f, indent=2)

    return run_dir, summary


# ═══════════════════════════════════════════════════════════════════
# Markdown summary (clickable links like unit_tests/run_*.md)
# ═══════════════════════════════════════════════════════════════════

def _write_summary_md(run_dir, summary):
    """Write a summary.md with clickable relative links to each log file."""
    ts = summary["timestamp"]
    lines = []
    lines.append(f"# SGC Phone App Test Results — {ts}")
    lines.append("")

    total_p = summary["passed"]
    total_f = summary["failed"]
    total_all = summary["total"]

    # Status badge
    if total_f == 0:
        badge = f"✅ {total_p}/{total_all} passed"
    else:
        badge = f"❌ {total_f} failed · {total_p}/{total_all} passed"
    lines.append(badge)
    lines.append("")

    # Per-file summary table
    lines.append("| Test file | Result |")
    lines.append("|-----------|--------|")

    for fi in summary.get("files", []):
        fname = fi["file"].replace(".dart", "")
        fp, ff, ft = fi["passed"], fi["failed"], fi["total"]
        status = "✅" if ff == 0 else "❌"
        raw = fi.get("raw_output", "")
        if raw:
            lines.append(f"| [{fname}]({_link(raw)}) | {status} {fp}/{ft} |")
        else:
            lines.append(f"| {fname} | {status} {fp}/{ft} |")

    lines.append("")

    # Per-test details: always show — easy jump to any test log
    lines.append("## Details")
    lines.append("")

    for fi in summary.get("files", []):
        fname = fi["file"].replace(".dart", "")
        fp, ff = fi["passed"], fi["failed"]
        status = "✅" if ff == 0 else "❌"
        lines.append(f"### {fname}  {status} {fp}/{fp + ff}")
        lines.append("")

        tests = fi.get("tests", [])
        if tests:
            lines.append("| Test | Result |")
            lines.append("|------|--------|")
            for t in tests:
                out = t.get("output", "")
                name = t["name"]
                r = "✅" if t["result"] == "success" else "❌"
                if t.get("error"):
                    r += f" — {t['error'][:60]}"
                lines.append(f"| [{name}]({_link(out)}) | {r} |")
            lines.append("")

    md_path = run_dir / "summary.md"
    with open(md_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

def main():
    parser = argparse.ArgumentParser(description="SGC Phone App test runner")
    parser.add_argument("--file", "-f", help="Filter by test file name")
    parser.add_argument("--per-test", "-p", action="store_true",
                        help="Run each test case individually (slower, richer)")
    parser.add_argument("--latest", action="store_true",
                        help="Show latest test results")
    args = parser.parse_args()

    if args.latest:
        latest = RESULTS_ROOT / "latest.json"
        if latest.exists():
            with open(latest) as f:
                s = json.load(f)
            print(f"Latest: {s['timestamp']}")
            print(f"  Passed: {s['passed']}  Failed: {s['failed']}  Total: {s['total']}")
            for f_info in s.get("files", []):
                status = "✅" if f_info["failed"] == 0 else "❌"
                print(f"  {status} {f_info['file']}  ({f_info['passed']}/{f_info['total']})")
                for t in f_info.get("tests", []):
                    if t["result"] == "failure":
                        err = t.get("error", "")
                        print(f"       ❌ {t['name']} — {err[:80]}")
        else:
            print("No results yet.")
        return

    print("Running phone tests...")

    if args.per_test:
        run_dir, summary = run_all_per_test(args.file)
    else:
        run_dir, summary = run_all_per_file(args.file)

    s = summary
    status = "✅ ALL PASSED" if s["failed"] == 0 else f"❌ {s['failed']} FAILED"
    print(f"\n{status} — {s['passed']}/{s['total']} tests")
    print(f"Results: {run_dir}")
    print(f"Summary: {run_dir / 'summary.json'}")

    sys.exit(0 if s["failed"] == 0 else 1)


if __name__ == "__main__":
    main()
