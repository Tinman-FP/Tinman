// Tinmans Tuning Guide, chapter 06: cooling and bridges.
// A 100 x 90 mm plate with bridges of 10, 20, 30 and 40 mm under 2 mm decks, overhang fins at
// 30, 45, 55, 65 and 70 degrees, and 3 mm round, 5 mm round and 5 mm square pins, 20 mm tall.
// Decks are 20 mm wide so the walls leave real bridge infill in the middle, and 2 mm thick so
// the top shows whether the bridge spoiled the surface above it. Change one fan setting per print.
$fn = 48;
base_t = 0.8;
deck_t = 2.0;
font = "Liberation Sans:style=Bold";
cube([100, 90, base_t]);

// Bridges, row 1: pillars 4 x 20 x 8 mm, spans 10 / 20 / 30, deck deck_t.
for (x = [4, 18, 42, 76]) translate([x, 4, base_t]) cube([4, 20, 8]);
translate([4, 4, base_t + 8]) cube([76, 20, deck_t]);
for (l = [[11,"10"],[28,"20"],[57,"30"]]) translate([l[0], 0.6, base_t]) linear_extrude(0.4) text(l[1], size = 3, font = font);

// Bridge row 2: 40 mm span.
for (x = [4, 48]) translate([x, 28, base_t]) cube([4, 20, 8]);
translate([4, 28, base_t + 8]) cube([48, 20, deck_t]);
translate([26, 24.6, base_t]) linear_extrude(0.4) text("40", size = 3, font = font);

// Small pins: 3 and 5 mm round, 5 mm square, 20 mm tall.
translate([62, 38, base_t]) cylinder(d = 3, h = 20);
translate([72, 38, base_t]) cylinder(d = 5, h = 20);
translate([80, 35.5, base_t]) cube([5, 5, 20]);

// Overhang fins: 3 mm slabs leaning back (+Y) from vertical by angle a, 10 mm tall; labels in front.
angles = [30, 45, 55, 65, 70];
for (i = [0:len(angles)-1]) {
  a = angles[i];
  translate([4 + i*14, 54, base_t])
    multmatrix([[1,0,0,0],[0,1,tan(a),0],[0,0,1,0],[0,0,0,1]]) cube([10, 3, 10]);
  translate([5 + i*14, 50.8, base_t]) linear_extrude(0.4) text(str(a), size = 2.6, font = font);
}
