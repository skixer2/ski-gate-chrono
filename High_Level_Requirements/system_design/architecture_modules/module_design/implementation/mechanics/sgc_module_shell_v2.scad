// SGC Module-Shell Interface — Full Parametric Model (v2.1)
// OpenSCAD — F5 preview, F6 render, File→Export→STL
//
// This model shows:
//   1. SHELL with built-in spring-latch mechanism (the "lock")
//   2. MODULE (electronics housing) with detent notch + rails
//   3. Both assembled and disassembled views
//   4. Battery lengthwise-aligned with PCB (series, not side-by-side)
//
// Controls: Set MODE below to switch views

/* [View Mode] */
MODE = "assembled"; // ["assembled", "disassembled", "cutaway_shell", "cutaway_module", "latch_detail"]

/* [Shell Size] */
SH_SIZE = "M"; // ["S", "M", "L"]

// Shell lengths per size (mm)
SHELL_S = 200;
SHELL_M = 250;
SHELL_L = 300;
SH_LEN = (SH_SIZE == "S") ? SHELL_S : ((SH_SIZE == "L") ? SHELL_L : SHELL_M);

/* [Module Dimensions] */
MOD_LEN = 65;       // Module length (mm) — compact, fits all shell sizes
MOD_W   = 22;       // Module width (narrow for comfort)
MOD_H   = 9;        // Module total height
WALL    = 1.5;      // Housing wall thickness
CLEAR   = 0.4;      // Sliding clearance

// Shell
SH_TH   = 2.0;      // Shell wall thickness

// Latch
LATCH_W   = 8;      // Latch width
LATCH_H   = 3;      // Latch engagement depth
LATCH_POS = 30;     // Distance from insertion end to latch center
SPRING_L  = 15;     // Spring length

// Rails
RAIL_H = 2.5;       // Rail protrusion into pocket
RAIL_W = 3;         // Rail width

// Keying tab (anti-flip)
KEY_W = 8;
KEY_H = 3;
KEY_D = 4;          // depth
KEY_OFFS = 6;       // off-center = can't insert upside-down

// O-ring
OR_D = 2.0;         // O-ring cross-section diameter

$fn = 80;

// ============================================================
// HELPERS
// ============================================================
module rbox(s, r) {
  linear_extrude(s.z) offset(r=r) square([s.x-2*r, s.y-2*r]);
}

// ============================================================
// MODULE — Sealed Electronics Housing (transparent PC)
// ============================================================
module module_body() {
  difference() {
    // Outer shape
    rbox([MOD_LEN, MOD_W, MOD_H], 2.5);

    // Inner cavity
    translate([WALL, WALL, WALL])
      rbox([MOD_LEN-2*WALL, MOD_W-2*WALL, MOD_H-2*WALL], 1.5);

    // LATCH DETENT — notch on the top surface
    // This is what the shell's latch bolt engages into
    translate([MOD_LEN - LATCH_POS - LATCH_W/2, MOD_W/2 - 3, MOD_H - LATCH_H])
      cube([LATCH_W, MOD_W/2 + 2, LATCH_H + 0.3]);

    // KEYING SLOT — on top surface, near insertion (back) end
    // Only fits the matching tab on the shell (off-center = anti-flip)
    translate([KEY_OFFS + 3, MOD_W/2 - KEY_W/2, MOD_H - KEY_H])
      cube([KEY_D + 0.2, KEY_W + 0.2, KEY_H + 0.3]);

    // O-ring groove — perimeter channel
    translate([OR_D + 2, -0.1, MOD_H - WALL - OR_D*0.7])
      cube([MOD_LEN - 2*(OR_D + 2), MOD_W + 0.2, WALL + 0.2]);
    translate([-0.1, OR_D + 2, MOD_H - WALL - OR_D*0.7])
      cube([MOD_LEN + 0.2, MOD_W - 2*(OR_D + 2), WALL + 0.2]);
  }

  // KEYING TAB — on bottom surface, OPPOSITE side from the slot
  // This prevents upside-down insertion
  translate([KEY_OFFS + 3, MOD_W/2 + KEY_W/2 - KEY_W + 1, 0])
    cube([KEY_D, KEY_W, KEY_H]);
}

// Internal PCB (green) — occupies front 70% of module length
module pcb_inside() {
  // PCB: spans most of module width but only ~45mm of length (leaving room for battery)
  color("#2d5016")
    translate([WALL + 3, WALL + 3, WALL])
      cube([MOD_LEN - 20 - WALL - 3, MOD_W - 2*WALL - 6, 1.6]);

  // ANNA-B112 module on PCB (center of PCB area)
  color("#333")
    translate([WALL + 12, MOD_W/2 - 6, WALL + 1.6])
      cube([14, 12, 2.8]);
}

// Battery (gold, LENGTHWISE — in series with PCB, not side-by-side)
// Exploits shell length (200-300mm) to keep module narrow for comfort
module battery_inside() {
  // Battery sits at the far end of the module (toward elbow end of shell)
  // PCB occupies the front ~70% of module length
  color("#e8c030")
    translate([MOD_LEN - 18 - WALL - 1, WALL + 3, WALL])
      cube([18, MOD_W - 2*WALL - 6, 6]);
}

// SK6812 LEDs — along the top edge, over the PCB section only
module leds_inside() {
  for (i = [0:4]) {
    color("#0f0")
      translate([WALL + 6 + i*9.5, MOD_W - WALL - 2, MOD_H - WALL - 0.8])
        cylinder(r=1.3, h=0.8);
  }
}

// USB-C port — at wrist-end of module (opposite from battery)
module usb_port() {
  color("#555")
    translate([-2, MOD_W/2 - 4, MOD_H/2 - 2.5])
      cube([4, 8, 5]);
}

// Full module assembly
module electronics_module(opacity) {
  // Housing — transparent PC (alpha = opacity)
  color("LightSteelBlue", opacity) module_body();

  // Internals visible through transparent housing
  pcb_inside();
  battery_inside();
  leds_inside();
  usb_port();
}

// ============================================================
// SHELL — with built-in latch mechanism
// ============================================================
module shell_body() {
  difference() {
    // Shell block — sized to selected SH_SIZE
    rbox([SH_LEN - 20, MOD_W + 20, MOD_H + SH_TH*2 + RAIL_H + 3], 4);

    // POCKET — the cavity the module slides into
    // Pocket length = module length + clearance (module sits at wrist-end)
    translate([SH_TH, SH_TH, SH_TH])
      rbox([MOD_LEN + 2*CLEAR + 2, MOD_W + 2*CLEAR, MOD_H + RAIL_H + 2], 3);
  }
}

// Rails inside the pocket — module slides on these
module guide_rails() {
  color("#95a5a6") {
    // Upper rail
    translate([-1, -1, MOD_H + SH_TH])
      cube([MOD_LEN + 2*CLEAR + 2 + SH_TH*2 + 2, RAIL_W, RAIL_H]);
    // Lower rail
    translate([-1, MOD_W + 2*CLEAR - RAIL_W + 1 + SH_TH*2, MOD_H + SH_TH])
      cube([MOD_LEN + 2*CLEAR + 2 + SH_TH*2 + 2, RAIL_W, RAIL_H]);
  }
}

// SPRING-LOADED LATCH BOLT — the core locking mechanism
// This is BUILT INTO the shell. It engages the detent on the module.
module latch_mechanism(engaged) {
  // The latch assembly lives in the shell wall, above the pocket
  lx = MOD_LEN - LATCH_POS - LATCH_W/2 - SH_TH;
  ly = MOD_W/2 + SH_TH - LATCH_W/2;

  // Spring housing (in shell wall)
  color("#f0a030") {
    if (engaged) {
      // Bolt EXTENDED into pocket (locked position)
      translate([lx, ly, MOD_H + SH_TH])
        cube([LATCH_W, LATCH_W, SPRING_L]);

      // Latch tongue engaging module detent
      translate([lx + 1, ly + 1, MOD_H + SH_TH - LATCH_H])
        cube([LATCH_W - 2, LATCH_W - 2, LATCH_H + SPRING_L + 1]);
    } else {
      // Bolt RETRACTED (released position)
      translate([lx, ly, MOD_H + SH_TH + LATCH_H + 2])
        cube([LATCH_W, LATCH_W, SPRING_L - LATCH_H]);
    }
  }

  // Spring coil (visual)
  color("#f39c12") {
    for (z = [MOD_H + SH_TH + 2 : 3 : MOD_H + SH_TH + SPRING_L - 2]) {
      if (engaged || z < MOD_H + SH_TH + 5)
        translate([lx + LATCH_W/2, ly + LATCH_W/2, z])
          cylinder(r=LATCH_W/2-0.5, h=1.5, center=true, $fn=12);
    }
  }

  // Release button (external, on shell surface)
  color("#e74c3c")
    translate([lx + LATCH_W/2 - 5, ly - 10, MOD_H + SH_TH + SPRING_L + 2])
      cube([10, 14, 5]);
}

// ============================================================
// ASSEMBLED VIEW
// ============================================================
module assembled_view() {
  // Shell (partially transparent to show module inside)
  color("LightSkyBlue", 0.25) shell_body();
  guide_rails();

  // Module inside shell pocket
  translate([SH_TH + CLEAR/2, SH_TH, SH_TH])
    electronics_module(0.7);

  // Latch engaged (locked)
  latch_mechanism(true);
}

// ============================================================
// DISASSEMBLED VIEW
// ============================================================
module disassembled_view() {
  // Shell below
  translate([0, 0, 0]) {
    color("LightSkyBlue", 0.35) shell_body();
    guide_rails();
    latch_mechanism(false);
  }

  // Module above (floating, aligned for insertion)
  translate([SH_TH + CLEAR/2, SH_TH, MOD_H + SH_TH + RAIL_H + 25])
    electronics_module(1.0);
}

// ============================================================
// CUTAWAY: Shell removed to show module + latch interface
// ============================================================
module cutaway_shell_view() {
  difference() {
    assembled_view();
    // Cut away front-right quarter of shell
    translate([MOD_LEN/2, MOD_W/2, -5])
      cube([MOD_LEN, MOD_W, 100]);
  }
}

// ============================================================
// CUTAWAY: Module exposed, showing detent engagement
// ============================================================
module cutaway_module_view() {
  difference() {
    assembled_view();
    // Cut away front half of module to expose detent
    translate([0, MOD_W/2, -5])
      cube([MOD_LEN, MOD_W, 100]);
  }
}

// ============================================================
// LATCH DETAIL — Close-up of just the latching interface
// ============================================================
module latch_detail_view() {
  // Simplified cross-section of the latch area
  // Module wall (cross-section)
  color("LightSteelBlue", 0.5)
    translate([0, -2, 5])
      cube([40, 16, 12]);

  // Detent notch in module
  color("LightSteelBlue", 0.5)
  difference() {
    translate([0, -2, 5])
      cube([40, 16, 12]);
    translate([14, 4, 8])
      cube([8, 12, 6]);
  }

  // Shell wall
  color("LightSkyBlue", 0.3)
    translate([0, -6, 0])
      cube([40, 18, 8]);

  // Latch bolt (extended — locked)
  color("#27ae60")
    translate([15, 5, 3])
      cube([6, 6, 12]);

  // Spring
  color("#f39c12")
    translate([16, 6, 15])
      cylinder(r=2.5, h=12, $fn=20);

  // Release button
  color("#e74c3c")
    translate([13, 2, 28])
      cube([12, 12, 4]);

  // Labels via colored markers
  color("#27ae60")
    translate([30, 8, 15])
      sphere(r=2);
  color("#e74c3c")
    translate([30, 8, 30])
      sphere(r=2);
}

// ============================================================
// MAIN — Select view by MODE
// ============================================================
echo(str("=== SGC MODULE-SHELL INTERFACE v2.1 ==="));
echo(str("Shell size: ", SH_SIZE, " (", SH_LEN, " mm)"));
echo(str("View mode: ", MODE));
echo(str("Module: ", MOD_LEN, "x", MOD_W, "x", MOD_H, "mm (lengthwise batt+PCB)"));
echo(str("Shell adds: +", SH_TH*2 + RAIL_H + 3, "mm total thickness"));
echo(str("Latch position: ", LATCH_POS, "mm from insertion end"));
echo(str("Key tab offset: ", KEY_OFFS, "mm (anti-flip)"));
echo(str(""));
echo(str("BATTERY: Lengthwise-aligned behind PCB (series, not side-by-side)"));
echo(str("  — exploits shell length, keeps width narrow for comfort"));
echo(str(""));
echo(str("LATCHING: Slide module into shell pocket → latch bolt auto-engages detent → CLICK"));
echo(str("RELEASE: Press button → latch retracts → slide module out"));

if (MODE == "assembled") {
  assembled_view();
} else if (MODE == "disassembled") {
  disassembled_view();
} else if (MODE == "cutaway_shell") {
  cutaway_shell_view();
} else if (MODE == "cutaway_module") {
  cutaway_module_view();
} else if (MODE == "latch_detail") {
  latch_detail_view();
}
