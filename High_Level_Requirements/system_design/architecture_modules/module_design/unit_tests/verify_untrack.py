"""Verify the gitignore cleanup did not lose anything real."""
import subprocess

REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"


def git(*a):
    r = subprocess.run(["git", "-C", REPO] + list(a), capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout


def main():
    rc, out = git("show", "--stat", "--oneline", "b312e12c")
    print("=== commit b312e12c ===")
    print(out.strip()[:900])
    print()

    rc, files = git("ls-files")
    files = files.splitlines()
    print("total tracked files: %d" % len(files))

    launcher = [f for f in files if "ic_launcher" in f]
    mech = [f for f in files if "mechanics/" in f and f.endswith(".png")]
    inputinfo = [f for f in files if "InputInformation/" in f]

    print("  launcher icons tracked : %d  (must be 5)" % len(launcher))
    for f in launcher:
        print("      " + f.split("/")[-2] + "/" + f.split("/")[-1])
    print("  mechanics figures      : %d  (must be 6)" % len(mech))
    print("  InputInformation       : %d" % len(inputinfo))

    rc, still = git("ls-files", "-i", "-c", "--exclude-standard")
    still = [l for l in still.splitlines() if l.strip()]
    print()
    print("remaining tracked-but-ignored: %d" % len(still))
    for f in still:
        print("      " + f)

    print()
    ok = len(launcher) == 5 and len(mech) == 6
    print("RESULT: %s" % ("assets preserved" if ok else "ASSETS LOST - PROBLEM"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
