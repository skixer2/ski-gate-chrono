import os, subprocess, re, json

R = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
PDF = os.path.join(R, "High_Level_Requirements", "system_design",
                   "architecture_modules", "pcb", "sgc_p1", "sgc_p1_review.pdf")

summary = {}

# ---- 1. Is U2 DNP? Look at the netlist component block with a real parser ----
NET = os.path.join(R, "High_Level_Requirements", "system_design",
                   "architecture_modules", "pcb", "sgc_p1", "sgc_netlist.py")
src = open(NET, encoding="utf-8", errors="ignore").read()

def grab(ref):
    i = src.find('ref="%s"' % ref)
    if i < 0:
        return None
    start = src.rfind("dict(", 0, i)
    # balance parens
    depth = 0
    for j in range(start, len(src)):
        if src[j] == "(":
            depth += 1
        elif src[j] == ")":
            depth -= 1
            if depth == 0:
                return src[start:j+1]
    return None

comps = {}
for ref in ("U2", "U7", "U9", "U13", "D6", "J3", "J4", "MH1", "MH2"):
    b = grab(ref)
    if b:
        dnp = "dnp=True" in b.replace(" ", "")
        fp = re.search(r'fp=("?)([^",\)]+)\1', b)
        comps[ref] = {"dnp": dnp, "fp": fp.group(2) if fp else "?", "len": len(b)}
summary["components"] = comps

# ---- 2. Copy PDF + push so it lands on the gateway host ----
subprocess.run(["git", "-C", R, "add", "-f",
                r"erc_review"], capture_output=True, text=True)
c = subprocess.run(["git", "-C", R, "commit", "-m", "erc_review_pdf"],
                   capture_output=True, text=True)
summary["commit"] = (c.stdout + c.stderr).strip()[:300]
p = subprocess.run(["git", "-C", R, "push", "origin", "master"],
                   capture_output=True, text=True)
summary["push"] = (p.stdout + p.stderr).strip()[:300]

print(json.dumps(summary, indent=2))
