// Tinmans Tuning Guide, chapter 08: seam and surface test.
// Five separate pieces, so every layer has long travels between islands and short travels
// between walls inside each island. Find each seam in the preview or G-code before judging it.
h = 15;        // wall pieces
hb = 4;        // flat top-surface block
// A: round, seam on a curve
translate([-45, 25, 0]) cylinder(d = 30, h = h, $fn = 256);
// B: square, four sharp 90-degree convex corners
translate([-12.5, 12.5, 0]) cube([25, 25, h]);
// C: equilateral triangle, three 60-degree corners (side 30)
translate([45, 20, 0]) linear_extrude(h) polygon([[-15, -8.66], [15, -8.66], [0, 17.32]]);
// D: L shape, five convex corners and one concave corner
translate([-55, -35, 0]) linear_extrude(h) polygon([[0,0],[30,0],[30,12],[12,12],[12,30],[0,30]]);
// E: flat block for the top surface (60 x 25, 4 mm)
translate([-5, -35, 0]) cube([60, 25, hb]);
