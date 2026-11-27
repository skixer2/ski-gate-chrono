#!/usr/bin/env python3
# gen_pcb.py - generate sgc_p1.kicad_pcb: 22x55 board, all 79 parts placed per
# PLACEMENT_PLAN v2 (placement_check.P = single source of truth), pads netted
# from sgc_netlist PARTS. Footprints come from sgc_p1.pretty (project-local,
# official KiCad geometry, quoted or unquoted atom formats). Run: python3 gen_pcb.py
#
# Coordinate transform (plan y-up bottom-left -> KiCad file y-down top-left):
#   file_x = plan_x ;  file_y = 55 - plan_y ;  file_rot = (360 - plan_rot) % 360
import os, sys, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 22.0, 55.0
NL, TB = chr(10), chr(9)
Q = chr(34)

sys.path.insert(0, HERE)
from sgc_netlist import PARTS, SYMBOL_PINS
from placement_check import P

def det_uuid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "sgc-p1-pcb:" + name))

PAD_ALIASES = {"J1": {"SH": "S1"}, "D1": {"1": "COM", "2": "R", "3": "B", "4": "G"}}   # all 4 shell pads bond to SHELL net (pin S1)

def load_parts():
    by_ref = {p["ref"]: p for p in PARTS}
    missing = set(P) - set(by_ref)
    extra = set(by_ref) - set(P)
    assert not missing, "netlist refs missing from placement plan: %s" % sorted(missing)
    assert not extra, "placement plan refs missing from netlist: %s" % sorted(extra)
    return by_ref

def net_numbering(by_ref):
    nets = set()
    for p in by_ref.values():
        for net in p["nets"].values():
            if net not in ("NC", "no_net"):
                nets.add(net)
    ordered = [""] + sorted(nets)
    return ordered, {n: i for i, n in enumerate(ordered)}

def pad_number(st):
    tok = st[4:].lstrip()
    if tok.startswith(Q):
        return tok[1:tok.index(Q, 1)]
    return tok.split(")")[0].split()[0]

def line_delta(st):
    d = 0
    ins = False
    for ch in st:
        if ins:
            if ch == Q:
                ins = False
        else:
            if ch == Q:
                ins = True
            elif ch == "(":
                d += 1
            elif ch == ")":
                d -= 1
    return d

def emit_footprint(ref, part, spec, net_idx):
    fp = part.get("fp") or spec.get("fp") or ""
    assert fp.startswith("sgc_p1:"), "ref %s: footprint %r is not project-local" % (ref, fp)
    name = fp[7:]
    txt = open(os.path.join(HERE, "sgc_p1.pretty", name + ".kicad_mod"), encoding="utf-8").read()
    x, y, rot = P[ref][0], P[ref][1], P[ref][2]
    fx, fy, fr = x, H - y, (360 - rot) % 360
    body = ['(footprint "%s"' % name,
            TB + '(layer "F.Cu")',
            TB + "(at %g %g %g)" % (fx, fy, fr),
            TB + '(uuid "%s")' % det_uuid("fp:" + ref)]
    body.append(TB + '(property "Reference" "%s"' % ref)
    body.append(TB * 2 + "(at %g %g 0)" % (fx, fy - 3.0))
    body.append(TB * 2 + '(layer "F.SilkS")')
    body.append(TB * 2 + '(uuid "%s")' % det_uuid("fpref:" + ref))
    body.append(TB * 2 + "(effects (font (size 0.8 0.8) (thickness 0.15)))")
    body.append(TB + ")")
    unassigned = []
    skip_close = False
    in_ft_skip = False
    ft_depth = 0
    in_prop_skip = False
    prop_depth = 0
    cur = None
    depth = 0
    for ln in txt.split(NL)[1:]:
        st = ln.strip()
        if not st:
            continue
        if in_ft_skip:
            ft_depth += line_delta(st)
            if ft_depth <= 0:
                in_ft_skip = False
            continue
        if in_prop_skip:
            prop_depth += line_delta(st)
            if prop_depth <= 0:
                in_prop_skip = False
            continue
        if cur is None:
            if in_ft_skip:
                ft_depth += line_delta(st)
                if ft_depth <= 0:
                    in_ft_skip = False
                continue
            if in_prop_skip:
                prop_depth += line_delta(st)
                if prop_depth <= 0:
                    in_prop_skip = False
                continue
            if skip_close and st == ")":
                skip_close = False
                continue
            if st.startswith("(layer ") or st.startswith("(footprint") or st.startswith("(tedit"):
                continue
            if st.startswith("(at ") and len(st) > 4 and (st[4].isdigit() or st[4] == "-"):
                continue
            if st.startswith("(version") or st.startswith("(generator"):
                continue
            if st.startswith("(property"):
                prop_depth = line_delta(st)
                if prop_depth > 0:
                    in_prop_skip = True
                continue
            if st.startswith("(fp_text"):
                ft_depth = line_delta(st)
                if ft_depth > 0:
                    in_ft_skip = True
                continue
            if st.startswith("(pad"):
                cur = pad_number(st)
                depth = line_delta(st)
                body.append(ln)
                if depth == 0:
                    pin = PAD_ALIASES.get(ref, {}).get(cur, cur)
                    net = part["nets"].get(pin)
                    if net is None and pin.isdigit():
                        net = part["nets"].get(int(pin))
                    if net and net not in ("NC", "no_net"):
                        body[-1] = body[-1][:-1] + ' (net %d "%s"))' % (net_idx[net], net)
                    else:
                        unassigned.append(cur)
                    cur = None
            else:
                body.append(TB + st)
        else:
            if st.startswith("(net"):
                continue
            body.append(TB + st)
            depth += line_delta(st)
            if depth <= 0:
                pin = PAD_ALIASES.get(ref, {}).get(cur, cur)
                net = part["nets"].get(pin)
                if net is None and pin.isdigit():
                    net = part["nets"].get(int(pin))
                if net and net not in ("NC", "no_net"):
                    body.insert(-1, TB * 2 + '(net %d "%s")' % (net_idx[net], net))
                else:
                    unassigned.append(cur)
                cur = None
    return NL.join(body), unassigned

HEADER = """(kicad_pcb
(version 20260206)
(generator "pcbnew")
(generator_version "10.0")
(general
(thickness 0.8)
(legacy_teardrops no)
)
(paper "A4")
(title_block
(title "SGC P1 - ANNA-B402 carrier 22x55 4L")
(date "2026-11-27")
(rev "A-WIP")
(company "Ski Gate Chrono")
)
(layers
(0 "F.Cu" signal)
(4 "In1.Cu" power "GND")
(6 "In2.Cu" power "1V8")
(2 "B.Cu" signal)
(9 "F.Adhes" user "F.Adhesive")
(11 "B.Adhes" user "B.Adhesive")
(13 "F.Paste" user)
(15 "B.Paste" user)
(5 "F.SilkS" user "F.Silkscreen")
(7 "B.SilkS" user "B.Silkscreen")
(1 "F.Mask" user)
(3 "B.Mask" user)
(17 "Dwgs.User" user "User.Drawings")
(19 "Cmts.User" user "User.Comments")
(21 "Eco1.User" user "User.Eco1")
(23 "Eco2.User" user "User.Eco2")
(25 "Edge.Cuts" user)
(27 "Margin" user)
(31 "F.CrtYd" user "F.Courtyard")
(29 "B.CrtYd" user "B.Courtyard")
(35 "F.Fab" user)
(33 "B.Fab" user)
(39 "User.1" user)
(41 "User.2" user)
(43 "User.3" user)
(45 "User.4" user)
)
(setup
(stackup
(layer "F.SilkS" (type "Top Silk Screen"))
(layer "F.Paste" (type "Top Solder Paste"))
(layer "F.Mask" (type "Top Solder Mask") (thickness 0.01))
(layer "F.Cu" (type "copper") (thickness 0.035))
(layer "dielectric 1" (type "prepreg") (thickness 0.2104) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
(layer "In1.Cu" (type "copper") (thickness 0.035))
(layer "dielectric 2" (type "core") (thickness 0.2192) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
(layer "In2.Cu" (type "copper") (thickness 0.035))
(layer "dielectric 3" (type "prepreg") (thickness 0.2104) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
(layer "B.Cu" (type "copper") (thickness 0.035))
(layer "B.Mask" (type "Bottom Solder Mask") (thickness 0.01))
(layer "B.Paste" (type "Bottom Solder Paste"))
(layer "B.SilkS" (type "Bottom Silk Screen"))
(copper_finish "ENIG")
)
(pad_to_mask_clearance 0)
(allow_soldermask_bridges_in_footprints no)
(tenting (front yes) (back yes))
)
"""

def main():
    by_ref = load_parts()
    ordered, net_idx = net_numbering(by_ref)
    out = [HEADER]
    for i, n in enumerate(ordered):
        out.append('(net %d "%s")' % (i, n))
    out.append('(gr_rect (start 0 0) (end 22 55) (stroke (width 0.05) (type solid)) (fill no) (layer "Edge.Cuts") (uuid "%s"))' % det_uuid("edge:outline"))
    total_un = {}
    for ref in sorted(P, key=lambda r: (-P[r][1], P[r][0])):
        part = by_ref[ref]
        spec = SYMBOL_PINS[part["sym"]]
        block, un = emit_footprint(ref, part, spec, net_idx)
        out.append(block)
        if un:
            total_un[ref] = un
    out.append("(embedded_fonts no)")
    out.append(")")
    path = os.path.join(HERE, "sgc_p1.kicad_pcb")
    open(path, "w", encoding="utf-8").write(NL.join(out) + NL)
    print("wrote %s: %d footprints, %d nets" % (os.path.basename(path), len(P), len(ordered) - 1))
    for ref, ul in sorted(total_un.items()):
        print("  note: %s pads without netlist net (left unconnected): %s" % (ref, ",".join(ul)))

if __name__ == "__main__":
    main()
