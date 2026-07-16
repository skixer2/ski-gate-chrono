// SGC Module-Shell Interface — Parametric 3D Model
// OpenSCAD script — render with F6, export STL/STEP
//
// Usage: Open in OpenSCAD (free: openscad.org)
//   F5 = preview, F6 = full render
//   File → Export → STL/STEP/PNG

/* [Module Dimensions] */
MODULE_LENGTH = 80;     // mm — total module length
MODULE_WIDTH  = 35;     // mm — total module width
MODULE_HEIGHT = 9;      // mm — total module thickness
WALL_THICK    = 1.5;    // mm — PC housing wall thickness
CORNER_R      = 2.5;    // mm — corner radius

/* [Shell Pocket Dimensions] */
POCKET_CLEARANCE = 0.3; // mm — sliding clearance
SHELL_THICKNESS  = 2.0; // mm — PC shell wall
RAIL_HEIGHT      = 2.0; // mm — guide rail protrusion
RAIL_WIDTH       = 3.0; // mm — guide rail width

/* [Latch Mechanism] */
LATCH_WIDTH  = 6;       // mm
LATCH_HEIGHT = 3;       // mm — depth of latch bolt into detent
LATCH_DIST   = 25;      // mm — distance from insertion end
SPRING_DIAM  = 5;       // mm
SPRING_LENGTH = 12;     // mm — compressed

/* [Keying Tab] */
KEY_WIDTH  = 8;         // mm
KEY_HEIGHT = 2;         // mm
KEY_DEPTH  = 3;         // mm
KEY_OFFSET = 5;         // mm — distance from edge (off-center = anti-flip)

/* [O-Ring] */
ORING_GROOVE_DEPTH = 1.5; // mm
ORING_GROOVE_WIDTH = 2.5; // mm

$fn = 60; // resolution

// ============================================================
// MODULE HOUSING
// ============================================================
module module_housing() {
  difference() {
    // Outer shell
    translate([0, 0, 0])
      rounded_rect([MODULE_LENGTH, MODULE_WIDTH, MODULE_HEIGHT], CORNER_R);
    
    // Inner cavity (hollow)
    translate([WALL_THICK, WALL_THICK, WALL_THICK])
      rounded_rect([
        MODULE_LENGTH - 2*WALL_THICK,
        MODULE_WIDTH - 2*WALL_THICK,
        MODULE_HEIGHT - 2*WALL_THICK
      ], max(1, CORNER_R - WALL_THICK));
    
    // Detent notch for latch bolt (on top surface)
    translate([LATCH_DIST + MODULE_LENGTH/4, MODULE_WIDTH/2, MODULE_HEIGHT - LATCH_HEIGHT])
      cube([LATCH_WIDTH, MODULE_WIDTH/2 + 0.1, LATCH_HEIGHT + 0.1], center=false);
    
    // Keying slot (on top surface, near insertion end)
    translate([MODULE_LENGTH - KEY_DEPTH - KEY_OFFSET - 5, MODULE_WIDTH/2 - KEY_WIDTH/2, MODULE_HEIGHT - KEY_HEIGHT])
      cube([KEY_DEPTH + 0.2, KEY_WIDTH + 0.2, KEY_HEIGHT + 0.1]);

    // O-ring groove (around perimeter, near face)
    difference() {
      translate([ORING_GROOVE_WIDTH, -1, ORING_GROOVE_DEPTH])
        cube([MODULE_LENGTH - 2*ORING_GROOVE_WIDTH, MODULE_WIDTH + 2, MODULE_HEIGHT - 2*ORING_GROOVE_DEPTH]);
      translate([ORING_GROOVE_WIDTH + ORING_GROOVE_WIDTH, -2, ORING_GROOVE_DEPTH + ORING_GROOVE_WIDTH])
        cube([MODULE_LENGTH - 4*ORING_GROOVE_WIDTH, MODULE_WIDTH + 4, MODULE_HEIGHT - 2*ORING_GROOVE_DEPTH - 2*ORING_GROOVE_WIDTH]);
    }
  }
  
  // Keying tab (on opposite side from slot — only fits one way)
  translate([5 + KEY_OFFSET, MODULE_WIDTH/2 - KEY_WIDTH - 2, MODULE_HEIGHT])
    cube([KEY_DEPTH, KEY_WIDTH, KEY_HEIGHT]);
}

// ============================================================
// SHELL POCKET (hollow in shell)
// ============================================================
module shell_pocket() {
  difference() {
    // Shell block (representative section)
    translate([-5, -5, -SHELL_THICKNESS])
      rounded_rect([
        MODULE_LENGTH + 10,
        MODULE_WIDTH + 10,
        MODULE_HEIGHT + SHELL_THICKNESS + RAIL_HEIGHT + 3
      ], CORNER_R);

    // Pocket cavity
    translate([0, 0, 0])
      rounded_rect([
        MODULE_LENGTH + 2*POCKET_CLEARANCE,
        MODULE_WIDTH + 2*POCKET_CLEARANCE,
        MODULE_HEIGHT + RAIL_HEIGHT + 1
      ], CORNER_R);

    // Upper rail groove
    translate([0, 0, MODULE_HEIGHT + POCKET_CLEARANCE])
      cube([MODULE_LENGTH + 2*POCKET_CLEARANCE, RAIL_WIDTH, RAIL_HEIGHT + 1]);

    // Lower rail groove
    translate([0, MODULE_WIDTH + 2*POCKET_CLEARANCE - RAIL_WIDTH, MODULE_HEIGHT + POCKET_CLEARANCE])
      cube([MODULE_LENGTH + 2*POCKET_CLEARANCE, RAIL_WIDTH, RAIL_HEIGHT + 1]);
  }
  
  // Rails (protruding into pocket)
  translate([0.1, RAIL_WIDTH, MODULE_HEIGHT])
    cube([MODULE_LENGTH + 2*POCKET_CLEARANCE - 0.2, RAIL_WIDTH/2, RAIL_HEIGHT]);
  translate([0.1, MODULE_WIDTH + 2*POCKET_CLEARANCE - RAIL_WIDTH*1.5, MODULE_HEIGHT])
    cube([MODULE_LENGTH + 2*POCKET_CLEARANCE - 0.2, RAIL_WIDTH/2, RAIL_HEIGHT]);
}

// ============================================================
// LATCH BOLT + SPRING (in shell wall)
// ============================================================
module latch_mechanism(shell_x=0, shell_y=0) {
  color("SpringGreen") {
    // Latch bolt body
    translate([MODULE_LENGTH/4 + LATCH_DIST - 1, -SHELL_THICKNESS - 4, MODULE_HEIGHT/2 + 2])
      cube([LATCH_WIDTH - 2, 8, LATCH_HEIGHT + 1]);
    
    // Latch tongue (engages module detent)
    translate([MODULE_LENGTH/4 + LATCH_DIST - 2, -SHELL_THICKNESS - 1, MODULE_HEIGHT + 0.3])
      cube([LATCH_WIDTH, 3, LATCH_HEIGHT]);
  }
  
  color("Orange") {
    // Spring (compressed)
    translate([MODULE_LENGTH/4 + LATCH_DIST - 1, -SHELL_THICKNESS - 4 - SPRING_LENGTH, MODULE_HEIGHT/2 + 2])
      cube([LATCH_WIDTH - 2, SPRING_LENGTH, LATCH_HEIGHT + 1]);
  }
}

// ============================================================
// PCB (internal, for visualization)
// ============================================================
module pcb() {
  color("DarkGreen")
    translate([WALL_THICK + 2, WALL_THICK + 3, WALL_THICK + 1])
      cube([
        MODULE_LENGTH - 2*WALL_THICK - 4,
        MODULE_WIDTH - 2*WALL_THICK - 6,
        1.6
      ]);
}

// ============================================================
// Battery (side-by-side with PCB, for visualization)
// ============================================================
module battery() {
  color("Gold")
    translate([WALL_THICK + 4, WALL_THICK + 2, WALL_THICK + 3])
      cube([20, 14, 6]);
}

// ============================================================ 
// SK6812 LEDs (for visualization)
// ============================================================
module leds() {
  for (i = [0:4]) {
    color("Lime")
      translate([WALL_THICK + 6 + i*10, WALL_THICK + 5, MODULE_HEIGHT - WALL_THICK - 1])
        cylinder(r=1.2, h=1.5);
  }
}

// ============================================================
// O-RING (visual representation)
// ============================================================
module oring() {
  color("Red") {
    // Top
    translate([ORING_GROOVE_WIDTH, MODULE_WIDTH, MODULE_HEIGHT - ORING_GROOVE_DEPTH])
      cube([MODULE_LENGTH - 2*ORING_GROOVE_WIDTH, ORING_GROOVE_WIDTH, ORING_GROOVE_WIDTH]);
    // Bottom
    translate([ORING_GROOVE_WIDTH, -ORING_GROOVE_WIDTH, MODULE_HEIGHT - ORING_GROOVE_DEPTH])
      cube([MODULE_LENGTH - 2*ORING_GROOVE_WIDTH, ORING_GROOVE_WIDTH, ORING_GROOVE_WIDTH]);
    // Left
    translate([-ORING_GROOVE_WIDTH, 0, MODULE_HEIGHT - ORING_GROOVE_DEPTH])
      cube([ORING_GROOVE_WIDTH, MODULE_WIDTH, ORING_GROOVE_WIDTH]);
    // Right
    translate([MODULE_LENGTH, 0, MODULE_HEIGHT - ORING_GROOVE_DEPTH])
      cube([ORING_GROOVE_WIDTH, MODULE_WIDTH, ORING_GROOVE_WIDTH]);
  }
}

// ============================================================
// Helper: rounded_rect
// ============================================================
module rounded_rect(size, r) {
  linear_extrude(size.z)
    offset(r=r)
      square([size.x - 2*r, size.y - 2*r], center=false);
}

// ============================================================
// ASSEMBLY (exploded for clarity)
// ============================================================
EXPLODE = 25; // set to 0 for assembled view

// Shell pocket (translucent)
color("LightBlue", 0.3)
  translate([0, 0, -EXPLODE])
    shell_pocket();

// Module housing (transparent)
color("LightBlue", 0.5)
  module_housing();

// Internal components (visible through transparent housing)
pcb();
battery();
leds();

// O-ring
oring();

// Latch mechanism
latch_mechanism();

// ============================================================
// ANNOTATIONS (rendered as colored markers)
// ============================================================
// Alignment key marker
color("Purple")
  translate([5 + KEY_OFFSET + KEY_DEPTH/2, MODULE_WIDTH/2 - KEY_WIDTH - 3, MODULE_HEIGHT + KEY_HEIGHT])
    sphere(r=1.5);

// USB-C port marker
color("Gray")
  translate([MODULE_LENGTH, MODULE_WIDTH/2, MODULE_HEIGHT/2])
    cube([3, 8, 4], center=true);

echo(str("=== SGC Module Dimensions ==="));
echo(str("Module: ", MODULE_LENGTH, "×", MODULE_WIDTH, "×", MODULE_HEIGHT, " mm"));
echo(str("Wall thickness: ", WALL_THICK, " mm"));
echo(str("Module + shell total: ~", MODULE_HEIGHT + SHELL_THICKNESS*2 + RAIL_HEIGHT + 4, " mm"));
echo(str("Latch position: ", LATCH_DIST + MODULE_LENGTH/4, " mm from insertion end"));
echo(str("Key tab offset: ", KEY_OFFSET, " mm (anti-flip)"));
echo(str(""));
echo(str("Render with F6 → Export STL for 3D printing prototype"));
echo(str("Set EXPLODE=0 for assembled view, EXPLODE=25 for exploded"));
