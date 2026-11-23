import subprocess, os, json

R = r"C:\Users\v17ni\Documents\projects\ski-gate-chrono"
PCB = os.path.join(R, "High_Level_Requirements", "system_design",
                   "architecture_modules", "pcb", "sgc_p1")

def g(*a):
    return subprocess.run(["git", "-C", R] + list(a), capture_output=True,
                          text=True, errors="ignore")

# stage only the schematic project dir + the pretty lib
for p in ("High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sgc_netlist.py",
          "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/gen_schematic.py",
          "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sgc_p1.pretty",
          "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sgc_p1.kicad_sch",
          "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sch_power.kicad_sch",
          "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sch_mcu.kicad_sch",
          "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sch_sensors.kicad_sch",
          "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sch_storage.kicad_sch",
          "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sch_boost_led.kicad_sch",
          "High_Level_Requirements/system_design/architecture_modules/pcb/sgc_p1/sch_connectors.kicad_sch"):
    g("add", "-f", "--", p)

print("staged:")
print(g("diff", "--cached", "--stat").stdout.strip()[:1500])

c = g("commit", "-m", "schematic: fix 5 unresolved footprints + portable temp path")
print("\ncommit:", (c.stdout + c.stderr).strip()[:300])

p = g("push", "origin", "master")
print("push:", (p.stdout + p.stderr).strip()[:300])

# ERC delta proof
rpt = os.path.join(PCB, "sgc_p1_erc2.rpt")
print("\nerc2 exists:", os.path.exists(rpt))
