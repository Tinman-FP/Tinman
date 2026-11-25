// Tinmans Tuning Guide, chapter 07: dimensional test (DIMS01).
// Three square frames, 120 / 80 / 40 mm outer, 5 mm walls, 6 mm tall, and an 80 x 14 x 6 mm bar
// with 3, 5, 8 and 10 mm holes. Each piece has a 4 mm 45-degree chamfer on its +X +Y outer
// corner, so X and Y can be told apart after removal. Matched outer and inner spans at three
// sizes separate a proportional scale error from a constant contour offset, in X and in Y:
//   outer span = scale x L + 2 x offset,  inner span = scale x (L - 10) - 2 x offset.
// Measure at the middle of each side, away from corners, after the part has cooled.
// The pieces are spaced so a brim up to about 27 mm wide cannot reach another piece's
// inside or holes: frame to frame gap + wall >= 30 mm, bar to frame gap >= 30 mm, and the
// 40 mm frame sits centred inside the 120 mm frame, 35 mm from its inside walls.
// Footprint 227 x 126 mm.
h = 6;      // height (Z check)
w = 5;      // frame wall width
c = 4;      // orientation chamfer leg

module corner_cut(x, y) {
    translate([x, y, -1]) linear_extrude(h + 2) polygon([[0.01, 0.01], [-c, 0.01], [0.01, -c]]);
}

module frame(L) {
    difference() {
        translate([-L/2, -L/2, 0]) cube([L, L, h]);
        translate([-L/2 + w, -L/2 + w, -1]) cube([L - 2*w, L - 2*w, h + 2]);
        corner_cut(L/2, L/2);
    }
}

module hole_bar() {
    bar_l = 80; bar_w = 14;
    holes = [[-28, 3], [-15, 5], [1, 8], [21, 10]];   // [x centre, diameter]
    difference() {
        translate([-bar_l/2, -bar_w/2, 0]) cube([bar_l, bar_w, h]);
        for (hd = holes) translate([hd[0], 0, -1]) cylinder(d = hd[1], h = h + 2, $fn = 256);
        corner_cut(bar_l/2, bar_w/2);
    }
}

translate([60, 60, 0])    frame(120);   // X 0..120,   Y 0..120
translate([60, 60, 0])    frame(40);    // X 40..80,   Y 40..80 (inside the 120, 35 mm clear)
translate([187, 80, 0])   frame(80);    // X 147..227, Y 40..120 (27 mm right of the 120)
translate([187, 1, 0])    hole_bar();   // X 147..227, Y -6..8   (32 mm below the 80)
