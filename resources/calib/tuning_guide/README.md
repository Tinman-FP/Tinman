# Tinmans Tuning Guide tests

Models for **Calibration > Tuning Guide tests**. Each entry starts a new project with these
models and the selected printer, filament and process left as they are. Only the settings a
test needs are set on its objects. How to run and grade each test is in
[Tinmans Tuning Guide](https://github.com/Tinman-FP/tinmans-tuning-guide).

| Menu entry | Guide chapter | Model | Object settings |
| --- | --- | --- | --- |
| 3DBenchy | 24 | `../../handy_models/3DBenchy.drc` (the stock model, 100 %) | none |
| First layer | 01 | `first_layer_square.stl`, five copies: bed centre and near each corner | none |
| Bond check | 02 | `bond_bars.stl` | none |
| Cooling and bridges | 06 | `cooling_and_bridges.stl` | none |
| Dimensional accuracy | 07 | `dims01_frames_and_holes.stl` | none |
| Seams | 08 | `seam_test.stl` | none |
| Top surface A/B | 08 | `top_surface_a.stl`, `top_surface_b.stl` | Top surface pattern: A Monotonic Line, B Monotonic |
| VAL01 v2 | 09 | `val01_v2.stl`, labelled "VAL01" | support off |

The OpenSCAD sources are in `source/`. The STLs are binary exports of those files from
OpenSCAD 2021.01 (`top_surface_set.scad` with `-D row_y=12.5` for A and `-D row_y=-37.5` for
B). Apart from the neutral VAL01 label, each matches the model printed in the guide's tuning
runs in triangle count, size, volume and area.

The models were made for the guide's tuning runs in November 2026 and are distributed with
Tinman under its licence. 3DBenchy is the Creative Tools model already shipped in
`resources/handy_models`.
