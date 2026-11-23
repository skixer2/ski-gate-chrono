import os, subprocess, json, re, sys

R = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
PDF = os.path.join(R, "High_Level_Requirements", "system_design",
                  "architecture_modules", "pcb", "sgc_p1", "sgc_p1_review.pdf")

print("PDF exists:", os.path.exists(PDF), os.path.getsize(PDF) if os.path.exists(PDF) else 0)

# page count via minimal PDF parse
data = open(PDF, "rb").read()
pages = len(re.findall(rb"/Type\s*/Page[^s]", data))
print("PDF pages (approx):", pages)
print("PDF has text ops:", data.count(b"BT") > 10)

# is it tracked by git?
out = subprocess.run(["git", "-C", R, "ls-files", "erc_review"],
                     capture_output=True, text=True).stdout.strip()
print("git tracks erc_review:", repr(out[:200]) if out else "NO")

# U2 / J3 status in the spec
SPEC = os.path.join(R, "High_Level_Requirements", "system_design",
                    "architecture_modules", "pcb", "sgc_p1", "sgc_netlist.py")
print("\nnetlist script exists:", os.path.exists(SPEC))
if os.path.exists(SPEC):
    s = open(SPEC, encoding="utf-8", errors="ignore").read()
    for key in ("U2", "U7", "J3", "MX25R"):
        for ln in s.splitlines():
            if key in ln and ("dnp" in ln.lower() or "MX25" in ln or "SWD" in ln):
                print(f"  [{key}] {ln.strip()[:120]}")
