"""Untrack files that the repo's ignore rules already match.

JP approved this on 2026-11-24. These are files that were committed BEFORE the
ignore patterns existed, so they stay tracked forever and quietly re-appear in
`git status` diffs. `git rm --cached` removes them from the index but KEEPS
them on disk - nothing is deleted.

Deliberately NOT touched: review artifacts (.net/.rpt under review/) and any
real source. Only files the ignore rules already match.
"""
import os
import subprocess
import sys

REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"


def git(*args, check=False):
    r = subprocess.run(["git", "-C", REPO] + list(args),
                       capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        raise SystemExit("git %s failed: %s" % (" ".join(args), r.stderr))
    return r


def main():
    apply = "--apply" in sys.argv

    r = git("ls-files", "-i", "-c", "--exclude-standard")
    if r.returncode != 0:
        print("cannot list: %s" % r.stderr)
        return 2
    files = [l for l in r.stdout.splitlines() if l.strip()]

    if not files:
        print("nothing to do - no tracked file matches an ignore rule")
        return 0

    print("%d tracked file(s) already matched by ignore rules:\n" % len(files))
    for f in files:
        # confirm which rule matches, for the record
        c = git("check-ignore", "-v", f)
        rule = c.stdout.strip().split("\t")[0] if c.stdout.strip() else "?"
        print("  %-74s  %s" % (f, rule))

    print()
    # Protect by MEANING, not just extension. The first version of this was
    # extension-based and would have untracked the Android app's launcher
    # icons, which AndroidManifest.xml references by name - breaking the build.
    PROTECT_PATHS = (
        "android/app/src/main/res/",   # launcher icons: referenced by manifest
        "mechanics/",                   # design figures: the only copies
        "InputInformation/",            # vendor reference captures
    )
    PROTECT = (".py", ".md", ".kicad_sch", ".kicad_sym", ".kicad_pro",
               ".kicad_pcb", ".kicad_mod", ".net", ".rpt", ".txt", ".yaml",
               ".yml", ".json", ".bat", ".ps1")
    keep = [f for f in files
            if f.lower().endswith(PROTECT)
            or any(seg in f.replace("\\", "/") for seg in PROTECT_PATHS)]
    drop = [f for f in files if f not in keep]

    if keep:
        print("KEPT (source / review artifact, will NOT untrack):")
        for f in keep:
            print("  " + f)
        print()

    if not drop:
        print("nothing safe to untrack")
        return 0

    print("WOULD UNTRACK (%d):" % len(drop))
    for f in drop:
        print("  " + f)
    print()

    if not apply:
        print("dry run - pass --apply to execute")
        return 0

    r = git("rm", "--cached", "--quiet", "--", *drop)
    print("git rm --cached exit=%d" % r.returncode)
    if r.stderr.strip():
        print(r.stderr.strip())

    r = git("commit", "-m",
            "chore: untrack files already matched by .gitignore\n\n"
            "These were committed before the ignore rules existed. Removed from\n"
            "the index only - the files stay on disk. Dry-run reviewed first;\n"
            "source files and review artifacts were excluded deliberately.")
    print("commit exit=%d" % r.returncode)
    for ln in (r.stdout + r.stderr).strip().splitlines()[:12]:
        print("  " + ln)

    r = git("ls-files", "-i", "-c", "--exclude-standard")
    remaining = [l for l in r.stdout.splitlines() if l.strip()]
    print()
    print("still tracked-but-ignored: %d" % len(remaining))
    for f in remaining:
        print("  " + f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
