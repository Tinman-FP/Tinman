// Tinmans Tuning Guide, chapter 09: VAL01 v2, functional bracket and fit coupon.
// The last test before a filament profile is called TUNED. Print it from the saved profiles with
// no supports: the two 40 mm bridges are part of the test (Tinman turns support off on the object).
// Bracket: base 70 x 40 x 8 with a cable channel underneath (40 mm bridge, 4 mm deck), an upright
//   wall 70 x 6, 38 mm tall overall, with a 40 x 12 window (40 mm lintel bridge), two 45-degree
//   gussets, a 14 mm boss with a blind 5 mm bore that starts on the deck top (v1 ran the bore
//   through the bridged deck), two 5.5 mm (M5 clearance) and two 3.4 mm (M3 clearance) holes,
//   and a debossed label.
// Fit coupon: four 8.0 mm holes and four pins at 7.9 / 7.8 / 7.7 / 7.6 mm (0.1 / 0.2 / 0.3 /
//   0.4 mm diametral clearance), pins marked with 1-4 dimples on top.
// To label your own copy, change only `label` (about 8 characters) and export a new STL.
// Tinman spreads the pieces along Y (spread = true): in the guide's layout the pins stand 8 mm
// behind the coupon plate and 15 mm in front of the bracket, so a brim wider than about 8 mm fills
// the coupon's 8 mm holes and the bracket's M5 holes. Spread, the pins move 20 mm and the plate
// 47 mm toward the front, about 35 mm apart, and no part changes (CORE One L report, 2026-11-25).
label = "VAL01";
spread = true;
$fn = 128;

module gusset(x) {
  translate([x, 0, 0]) rotate([90, 0, 90]) linear_extrude(4)
    polygon([[14, 8], [34, 8], [34, 28]]);
}

module bracket() {
  difference() {
    union() {
      cube([70, 40, 8]);                          // base
      translate([0, 34, 0]) cube([70, 6, 38]);    // upright wall
      gusset(10); gusset(56);                     // 45-degree gussets
      translate([35, 17, 0]) cylinder(d = 14, h = 18);   // boss on the deck
    }
    translate([15, -1, -1]) cube([40, 36, 5]);    // cable channel: 40 wide, 4 tall, deck z 4..8
    translate([15, 33, 20]) cube([40, 8, 12]);    // window 40 x 12 in the wall, lintel z 32..38
    for (p = [[6, 6], [64, 6]]) translate([p[0], p[1], -1]) cylinder(d = 5.5, h = 10);   // M5 clearance
    for (p = [[5, 24], [65, 24]]) translate([p[0], p[1], -1]) cylinder(d = 3.4, h = 10); // M3 clearance
    translate([35, 17, 8]) cylinder(d = 5, h = 11);                                      // boss bore, blind: starts on the deck top (Z 8), never cuts the bridge
    translate([35, 5, 7.4]) linear_extrude(1) text(label, size = 4.5, halign = "center", valign = "center");
  }
}

module fit_coupon(pin_y) {
  difference() {
    cube([60, 14, 5]);
    for (i = [0:3]) translate([9 + i * 14, 7, -1]) cylinder(d = 8.0, h = 7);
  }
  for (i = [0:3]) translate([9 + i * 14, pin_y, 0]) difference() {
    cylinder(d = 7.9 - i * 0.1, h = 12);
    for (k = [0:i]) translate([-1.5 + k * 1.0, 0, 11.4]) cylinder(d = 0.8, h = 1, $fn = 24);  // 1-4 dimples
  }
}

bracket();
if (spread) translate([0, -92, 0]) fit_coupon(53);
else translate([0, -45, 0]) fit_coupon(26);
