// Tinmans Tuning Guide, chapter 08: top-surface A/B.
// One set = a 60 x 25 mm block, a 25 mm square and a 30 mm triangle, all 4 mm tall. Tinman
// loads two sets: A (back row, rendered with row_y = 12.5) with Top surface pattern Monotonic
// Line, and B (front row, row_y = -37.5) with Monotonic, both as object settings, so the only
// difference between them is the top-surface path.
row_y = 0;   // set by -D on the command line
h = 4;
translate([0, row_y, 0]) {
  translate([-72.5, 0, 0]) cube([60, 25, h]);                      // block
  translate([2.5, 0, 0]) cube([25, 25, h]);                         // square
  translate([57.5, 0, 0]) linear_extrude(h) polygon([[-15, 0], [15, 0], [0, 25.98]]);  // triangle, apex at back
}
