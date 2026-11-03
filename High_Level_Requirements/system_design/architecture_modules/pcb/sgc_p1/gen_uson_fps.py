#!/usr/bin/env python3
"""gen_uson_fps.py — generate the two project-local USON-8 footprints (KiCad 10 dialect).

Sources (datasheet package outlines, verified 2026-11-03):
- MX25R1635F v1.6 p.81  8-land USON 2x3mm, pitch 0.5: D=2.0 E=3.0 D1=1.6 b=0.25 L=0.45.
  Pads 0.30x0.55, rows x=+-0.8, y=+-0.25/+-0.75 (W25X-class standard land).
- MX25R6435F v1.6 p.82  8-land USON 4x3mm, pitch 0.8: D=4.0 E=3.0 b=0.30 L=0.60.
  Pads 0.45x0.75, 4 per long edge: x=+-1.2/+-0.4, y=+-1.1 (industry USON8-4x3 land).
Both datasheets: exposed pad underneath may be LEFT FLOATING -> no EP pad modeled.
Pin map (top view, pin1 = top-left): first row 1..4, opposite row 8..5.
Output: sgc_p1.pretty/*.kicad_mod (project-local fp library, auto-loaded by KiCad)."""
import os

def fmt(v):
    return ('%g' % v)

def fpline(st, en, w, layer):
    return ('\t(fp_line\n\t\t(start %s %s)\n\t\t(end %s %s)\n\t\t(stroke\n\t\t\t(width %s)\n\t\t\t(type solid)\n\t\t)\n\t\t(layer "%s")\n\t)\n'
            % (fmt(st[0]), fmt(st[1]), fmt(en[0]), fmt(en[1]), fmt(w), layer))

def fppoly_pin1(x, y, layer):
    """pin-1 indicator triangle at top-left corner."""
    return ('\t(fp_poly\n\t\t(pts\n\t\t\t(xy %s %s) (xy %s %s) (xy %s %s) (xy %s %s)\n\t\t)\n\t\t(stroke\n\t\t\t(width 0.12)\n\t\t\t(type solid)\n\t\t)\n\t\t(fill yes)\n\t\t(layer "%s")\n\t)\n'
            % (fmt(x[0]), fmt(y[0]), fmt(x[1]), fmt(y[1]), fmt(x[1]), fmt(y[0]), fmt(x[0]), fmt(y[0]), layer))

def pad(num, x, y, w, h):
    return ('\t(pad "%s" smd roundrect\n\t\t(at %s %s)\n\t\t(size %s %s)\n\t\t(layers "F.Cu" "F.Mask" "F.Paste")\n\t\t(roundrect_rratio 0.25)\n\t)\n'
            % (num, fmt(x), fmt(y), fmt(w), fmt(h)))

def prop(name, val, y, layer, size=1.0, hide=False):
    h = '\n\t\t(hide yes)' if hide else ''
    return ('\t(property "%s" "%s"\n\t\t(at 0 %s 0)\n\t\t(layer "%s")%s\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size %s %s)\n\t\t\t\t(thickness 0.15)\n\t\t\t)\n\t\t)\n\t)\n'
            % (name, val, fmt(y), layer, h, size, size))

def fptext_user(ref_xy):
    return ('\t(fp_text user "${REFERENCE}"\n\t\t(at %s %s 0)\n\t\t(layer "F.Fab")\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 0.6 0.6)\n\t\t\t\t(thickness 0.1)\n\t\t\t)\n\t\t)\n\t)\n' % (fmt(ref_xy[0]), fmt(ref_xy[1])))

def build(name, descr, pads, body_x, body_y, ct_x, ct_y, sk_x, sk_y):
    s = '(footprint "%s"\n\t(version 20260206)\n\t(generator "gen_uson_fps")\n\t(generator_version "10.0")\n\t(layer "F.Cu")\n\t(descr "%s")\n\t(tags "USON 8 Macronix")\n' % (name, descr)
    s += prop('Reference', 'REF**', -(sk_y + 0.55), 'F.SilkS', 0.8)
    s += prop('Value', name, sk_y + 0.55, 'F.Fab', 0.8)
    s += prop('Datasheet', '', 0, 'F.Fab', hide=True)
    s += prop('Description', 'EP underneath left floating per datasheet', 0, 'F.Fab', 1.0, hide=True)
    s += '\t(attr smd)\n\t(duplicate_pad_numbers_are_jumpers no)\n'
    # F.Fab: body rectangle with pin1 chamfer at top-left
    ch = min(0.3, body_x / 3, body_y / 3)
    s += fpline((-body_x, -(body_y - ch)), (-body_x + ch, -body_y), 0.1, 'F.Fab')
    s += fpline((-body_x + ch, -body_y), (body_x, -body_y), 0.1, 'F.Fab')
    s += fpline((body_x, -body_y), (body_x, body_y), 0.1, 'F.Fab')
    s += fpline((body_x, body_y), (-body_x, body_y), 0.1, 'F.Fab')
    s += fpline((-body_x, body_y), (-body_x, -(body_y - ch)), 0.1, 'F.Fab')
    # F.SilkS: outline (gap to pads) + pin1 dot
    s += fpline((-sk_x, -sk_y), (sk_x, -sk_y), 0.12, 'F.SilkS')
    s += fpline((sk_x, -sk_y), (sk_x, sk_y), 0.12, 'F.SilkS')
    s += fpline((sk_x, sk_y), (-sk_x, sk_y), 0.12, 'F.SilkS')
    s += fpline((-sk_x, sk_y), (-sk_x, -sk_y), 0.12, 'F.SilkS')
    s += fppoly_pin1((-sk_x, -sk_y + 0.5), (-sk_x + 0.5, -sk_y), 'F.SilkS')
    # F.CrtYd rectangle
    s += fpline((-ct_x, -ct_y), (ct_x, -ct_y), 0.05, 'F.CrtYd')
    s += fpline((ct_x, -ct_y), (ct_x, ct_y), 0.05, 'F.CrtYd')
    s += fpline((ct_x, ct_y), (-ct_x, ct_y), 0.05, 'F.CrtYd')
    s += fpline((-ct_x, ct_y), (-ct_x, -ct_y), 0.05, 'F.CrtYd')
    s += fptext_user((0, 0))
    for p in pads:
        s += pad(*p)
    s += '\t(embedded_fonts no)\n)\n'
    return s

# USON-8 2x3mm P0.5mm — rows at x=+-0.8; y=+-0.25,+-0.75; pins 1-4 left col top->bottom, 8-5 right col top->bottom
pads_2x3 = [
    ('1', -0.8, -0.75), ('2', -0.8, -0.25), ('3', -0.8, 0.25), ('4', -0.8, 0.75),
    ('5', 0.8, 0.75), ('6', 0.8, 0.25), ('7', 0.8, -0.25), ('8', 0.8, -0.75),
]
pads_2x3 = [(n, x, y, 0.30, 0.55) for n, x, y in pads_2x3]

# USON-8 4x3mm P0.8mm — 4 pads per long edge: x=+-1.2/+-0.4, y=+-1.1; top row 1-4 left->right, bottom 5-8 right->left
pads_4x3 = [
    ('1', -1.2, -1.1), ('2', -0.4, -1.1), ('3', 0.4, -1.1), ('4', 1.2, -1.1),
    ('5', 1.2, 1.1), ('6', 0.4, 1.1), ('7', -0.4, 1.1), ('8', -1.2, 1.1),
]
pads_4x3 = [(n, x, y, 0.45, 0.75) for n, x, y in pads_4x3]

os.makedirs('sgc_p1.pretty', exist_ok=True)
open('sgc_p1.pretty/USON-8_2x3mm_P0.5mm.kicad_mod', 'w').write(
    build('USON-8_2x3mm_P0.5mm',
          'USON-8 2x3mm P0.5mm, Macronix MX25R1635FZUIH0 (datasheet PM2161 v1.6 p.81). EP left floating per datasheet - no EP pad.',
          pads_2x3, 1.0, 1.5, 1.25, 1.75, 1.15, 1.65))
open('sgc_p1.pretty/USON-8_4x3mm_P0.8mm.kicad_mod', 'w').write(
    build('USON-8_4x3mm_P0.8mm',
          'USON-8 4x3mm P0.8mm, Macronix MX25R6435FZBIH3 (datasheet PM2138 v1.6 p.82). EP left floating per datasheet - no EP pad.',
          pads_4x3, 2.0, 1.5, 2.25, 1.75, 2.1, 1.6))
print('written: sgc_p1.pretty/USON-8_2x3mm_P0.5mm.kicad_mod, sgc_p1.pretty/USON-8_4x3mm_P0.8mm.kicad_mod')
