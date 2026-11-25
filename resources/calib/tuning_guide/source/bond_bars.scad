// Tinmans Tuning Guide, chapter 02: bond check.
// Three identical vertical bars, 10 mm wide (X) x 5 mm thick (Y) x 45 mm tall (Z), with a 3 mm
// hole through the 5 mm thickness at Z = 40 for a string or a luggage-scale hook, and a number
// (1-3) embossed near the base. Print one temperature (or one fan setting) per print. Clamp the
// bottom 10 mm with the jaws on the 10 mm faces, then pull or push at the hole until it breaks.
// It ranks candidates; it does not measure strength.
$fn = 48;
font = "Liberation Sans:style=Bold";
module bar(n) {
  difference() {
    cube([10, 5, 45]);
    translate([5, -1, 40]) rotate([-90, 0, 0]) cylinder(d = 3, h = 7);
  }
  translate([3.2, 0.01, 2]) rotate([90, 0, 0]) linear_extrude(0.4) text(str(n), size = 4, font = font);
}
for (i = [0:2]) translate([i * 20, 0, 0]) bar(i + 1);
