"""Move the remaining in-repo stale-files folder out to zio_claw."""
import os
import shutil
import subprocess

SRC = (r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
       r"\High_Level_Requirements\system_design\architecture_modules"
       r"\module_design\implementation\Firmware_Zephyr\sch_trash_20261123")
DST = r"C:\Users\v17ni\zio_claw\sgc_sch_trash_20261124\stale_files_20261123"
REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"


def main():
    if os.path.isdir(SRC):
        os.makedirs(os.path.dirname(DST), exist_ok=True)
        if os.path.exists(DST):
            shutil.rmtree(DST, ignore_errors=True)
        try:
            shutil.move(SRC, DST)
            print("moved to %s" % DST)
        except Exception as e:
            print("FAILED: %s" % e)
            return 1
    else:
        print("already gone: %s" % SRC)

    print()
    print("=== zio_claw trash ===")
    base = r"C:\Users\v17ni\zio_claw\sgc_sch_trash_20261124"
    for e in sorted(os.listdir(base)):
        p = os.path.join(base, e)
        n = len(os.listdir(p)) if os.path.isdir(p) else 1
        print("  %-26s %d entries" % (e, n))

    print()
    r = subprocess.run(["git", "-C", REPO, "status", "--short"],
                       capture_output=True, text=True)
    junk = [l for l in r.stdout.splitlines()
            if "sch_trash" in l or "sch_old" in l or "gentest" in l
            or "roundtrip" in l]
    print("git status junk: %s" % (junk if junk else "NONE"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
