"""Move the scratch trash OUT of the repo.

C:\\sgc is a JUNCTION to Firmware_Zephyr, so anything written under C:\\sgc
lands inside the git repo. My earlier clean_scratch.py put the trash there by
mistake. This moves it to zio_claw (the correct artifact home) and verifies
git no longer sees it.
"""
import os
import shutil
import subprocess

WRONG = (r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
         r"\High_Level_Requirements\system_design\architecture_modules"
         r"\module_design\implementation\Firmware_Zephyr"
         r"\sch_trash_20261124")
GOOD = r"C:\Users\v17ni\zio_claw\sgc_sch_trash_20261124"
REPO = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"


def main():
    if os.path.isdir(WRONG):
        os.makedirs(GOOD, exist_ok=True)
        for entry in sorted(os.listdir(WRONG)):
            src = os.path.join(WRONG, entry)
            dst = os.path.join(GOOD, entry)
            if os.path.exists(dst):
                shutil.rmtree(dst, ignore_errors=True)
            try:
                shutil.move(src, dst)
                print("moved %-22s -> %s" % (entry, dst))
            except Exception as e:
                print("FAILED %-22s : %s" % (entry, e))
        try:
            os.rmdir(WRONG)
            print("removed empty %s" % WRONG)
        except OSError as e:
            print("could not remove %s : %s" % (WRONG, e))
    else:
        print("wrong-location trash already gone")

    print()
    print("trash now at: %s" % GOOD)
    for entry in sorted(os.listdir(GOOD)) if os.path.isdir(GOOD) else []:
        print("  " + entry)

    print()
    r = subprocess.run(["git", "-C", REPO, "status", "--short"],
                       capture_output=True, text=True)
    hits = [l for l in r.stdout.splitlines() if "sch_trash" in l]
    print("git status mentions sch_trash: %s" % (hits if hits else "NO"))


if __name__ == "__main__":
    main()
