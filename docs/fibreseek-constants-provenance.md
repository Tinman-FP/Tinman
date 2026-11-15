# FibreSeek fibre constants: provenance

This document records where every number in Tinman's continuous-fibre output
comes from. It is the basis for removing values that have no documented
source. It covers the C++ fibre writer, the G-code preview, the bundled Python
planner and the `fiber_*` option defaults in `PrintConfig.cpp`.

## Why

Some fibre values in Tinman date from a period when numbers were taken from
Rocket Slicer presets and output, without an independent source (see #10, #12
and #14). The rule for shipped code:

- **(a)** A value with a derivation that can be written down (geometry, a unit
  conversion, an identity, an index, an epsilon or a clamp) stays, with the
  derivation in a comment.
- **(b)** Material or process tuning with no documented source must not ship
  as a hidden value. It becomes an explicit profile setting, ideally with no
  default, so a missing value stops the slice with a message naming the
  setting.

Provenance wins over compatibility: older projects lose values that Tinman
used to supply implicitly. Explicit values stored in a project or profile are
kept.

## Method

- **What was read.** Every numeric constant in:
  - `GCode::extrude_fiberseek_composite_routes_for_layer` and the fibre
    helpers it calls (`GCode.cpp`), counting each `tinman_positive_or_default`
    and `tinman_float_at_or_default` fallback;
  - the fibre preview and mass code (`GCodeProcessor.cpp`/`.hpp`);
  - the `PlannerConfig` defaults and the G-code-emitting functions of
    `resources/orcaslicer_codex/fiber_planner/orcaslicer_codex_native_fiber_planner.py`;
  - the default of every `fiber_*`, `continuous_fiber_*`,
    `plastic_nozzle_diameter` and `composite_nozzle_diameter` option.

  Route-search geometry inside the planners (clearances, tolerances, angle
  steps) is listed by function only.
- **Snapshot.** The values were read at commit `9fd0d3793f`, the first rework
  commit of #21. Line numbers refer to that commit. Rows marked
  **[fixed in 9fd0d379]** record what that commit already changed.
- **History.** The material-dependent (b1) values and the items named after
  Rocket were traced with `git log -S'<value>'`; their introducing commits are
  in §6 and §7. For every other row the default is "no source found": nothing
  in the repository history or documentation gives a derivation or reference.
- **Classes.** a, b1 (material or strand), b2 (process or machine), b2p
  (preview only), `prot` (the printer's command set, counted as a). See the
  Legend below.

## Status after #21

**Removed or made explicit in #21:**

- The 0.835 feed coefficient is now the composite setting **Fiber feed
  factor** (`fiber_feed_factor`), with no default.
- The preview's 0.25 mm / 102 g/km fallbacks and its 1.245 g/cm³ density
  fallback are removed. Without strand data the preview shows no fibre mass.
- The `continuous_fiber_*` defaults are now 0 ("not set").
- The planner's 0.041 P/V defaults are now 0, since they are always
  recomputed.
- The planner's 0.72 after-cut default is replaced by the existing
  `fiber_after_cut_plastic_extrusion_multiplier` setting, whose default 1 is
  the identity.
- Fibre linear density is in g/km throughout. A value below 1 is read as
  g/m, the setting's earlier label, and converted. This assumes no
  continuous-fibre tow is lighter than 1 g/km; printable strands are tens to
  hundreds of g/km.
- Both writers read per-material values (diameter, linear density, feed
  factor) only at the composite's slot, the first material whose
  filament_type is CFC. Another material's value is never used.

**Named after Rocket, not converted in #21:**

- `rocket_style_transition_limit_mm` (6 × line width). This is the longest
  connector treated as a transition rather than as fibre (§1c). It belongs to
  the C++ composite route planner, which a separate clean-room planner is
  replacing; it should go with that replacement. **[removed with that planner
  in the planner swap; the writer now prints the spiral step length `T`]**
- The `max_arc_segment_length` special case for the developer-only
  `ROCKET_COMPARE_*` comparison lanes (§3a). Those lanes are kept for
  comparison only, and keeping them is the user's decision.

**Still shipped, unsourced, pending a follow-up decided with the user:** the
b1 items in §6 and the b2 items in §8. In normal use they are live through the
`fiber_*` option defaults or the planner's defaults. The most important ones:

- standby temperatures 150/180 °C;
- the Python planner's fan speeds and its 12 mm minimum bend radius;
- the cut and restart lengths and the speed fallbacks;
- the prime-line and retract moves.

§7 lists divergences between the C++ and Python writers that the follow-up
should resolve. For example, the C++ writer cuts at L − 58 mm, while the
Python planner reads its cut distance (54.8 mm) back from the `;CUT DISTANCE`
comment.

## Legend

- **a**: the value has a derivation that can be written down: geometry, a unit conversion, identity, an index, an epsilon or a clamp. Keep it, with the derivation in a comment.
- **b1**: material or strand tuning. It changes with the fibre or the matrix: diameter, density, feed or flow factors, temperatures, cooling, after-cut multipliers, bend radius.
- **b2**: process or machine tuning: speeds, lengths, distances, cut geometry, fan or tool mapping, motion limits.
- **b2p**: b2 that only affects the preview display, not the G-code.
- `prot`: a firmware protocol token (M2800, M1001, T0/T1, macro names). These are counted as **a**: they are the machine's command set, not tuning.

Python default status:
- **dead**: always replaced. Either Tinman writes the config key with a value the parser accepts, or the field is recomputed.
- **live-if-0**: Tinman writes the key, but its PrintConfig default is 0. The parser treats 0 as unset, so the Python default is used unless the profile sets the key.
- **live**: no config key reaches the field. Only the optional `fiber_reinforcement_payload` JSON can change it.
- **mode**: overwritten unconditionally by `apply_mode_defaults` (§3c). The table value then becomes the live-if-0 default.

Which writer runs, and when (Print.cpp:207-222, GCode.cpp:149-160):
- With `fiber_manufacturing_mode = CompositeOnly`, the C++ writer `extrude_fiberseek_composite_routes_for_layer` writes the fibre G-code.
- With the default mode `PlasticPlusFiberOverlay` and a FibreSeek profile, the Python planner post-processes the G-code file.
- `append_full_config` (GCode.cpp) writes every config key except the host keys, so every `fiber_*` key reaches the Python config block.

---

## 1. C++ fibre writer: `src/libslic3r/GCode.cpp`

### 1a. `GCode::extrude_fiberseek_composite_routes_for_layer` (6693-7116)

`tinman_positive_or_default(v, fb)` returns `fb` when `v <= EPSILON` (1e-4), so a 0 in the profile selects the fallback.

| Loc | Value | Controls | Cls | Derivation / note |
|---|---|---|---|---|
| 6718 | 0.001 | floor for `;HEIGHT` | a | defensive floor (no zero height) |
| 6719 | 0.8 | `fiber_line_width` fallback | b2 | dead (PrintConfig default is 0.8, itself b2). Not the nozzle: `composite_nozzle_diameter` = 0.4 |
| 6720 | 58.0 | `fiber_cut_distance` fallback (cutter to tip) | b2 | **LIVE**: the PrintConfig default is 0. The route planner instead gets 0 (PrintObject.cpp:1299), see §7 |
| 6721 | 40.0 | inner fallback of the restart fallback | b2 | dead: `cut_distance_mm` is already > 0 after 6720 |
| 6721 | +2.0 | restart = cut distance + 2 | b2 | **LIVE** when `fiber_restart_length` = 0 (default), giving 60 mm. Python uses 55 |
| 6723 | 0.0 | `fiber_start_length` fallback | a | neutral (no prime V). Python uses 15 |
| 6726 | 30.0 | print speed, used after `fiber_normal_max_speed` and then `fiber_print_speed` | b2 | **LIVE** when both are 0 (defaults) |
| 6726 | x60 | mm/s to mm/min | a | unit |
| 6727 | 5.0 | `fiber_start_speed` fallback (slow zone) | b2 | **LIVE**. Ignores `fiber_start_max_speed` |
| 6727, 6730, 6732 | x60 | mm/s to mm/min | a | unit |
| 6728 | 3.0 | `fiber_finish_max_speed` fallback, used for the post-cut tail | b2 | **LIVE**. Python's finish default is 15 mm/s |
| 6729 | = finish max | `fiber_finish_min_speed` fallback | a | neutral: equal to max |
| 6731 | 0.5 | ironing feed = mean of finish max and min | a | midpoint of the configured range |
| 6732 | 2.0 | `fiber_correction_move_speed` fallback (non-feeding G0 inside a route) | b2 | **LIVE**. Ignores `fiber_override_correction_speed`, which Python honours |
| 6733 | 1500.0 | feed for the restart `U` move (mm/min) | b2 | hidden, no setting. Python has the same value |
| 6734 | 600.0 | feed for the priming `V` move and the end `V-1` retract | b2 | hidden, no setting. Python has the same value |
| 6740-6743 | (none) | `fiber_feed_factor` from the composite slot | b1 | **[fixed in 9fd0d379]**: was a hidden 0.835. Now a composite setting with no default; slicing stops when it is missing |
| 6745 | pi(d/2)^2 | strand cross-section | a | geometry |
| 6745 | 100000.0 | rounds P to 5 decimals | a | matches the 5-decimal output |
| 6746 | 100.0, /100 | `fiber_feedrate_percent` fallback | a | identity, percent |
| 6747 | 1.0 | `fiber_after_cut_plastic_extrusion_multiplier` fallback | a | identity. The setting itself is b1 (§9) |
| 6748 | max 0 | `fiber_slow_length` clamp | a | clamp. No fallback; Python uses 10 |
| 6749 | via 6.0 | connector/transition threshold | b2 | `rocket_style_transition_limit_mm`, see 1c |
| 6750-6751 | max 0 | fan index clamp | a | clamp |
| 6752-6753 | 1 or 0 | composite filament index | a | index |
| 6754 | 0.0 | `fiber_restart_pause` fallback | a | neutral, clamped at 0 |
| 6755 | 0.0 | `fiber_finish_ironing_distance` fallback | a | neutral |
| 6756 | 250 | plastic nozzle temperature fallback | b1 | dead: filament temperatures are > 0 |
| 6757-6760 | 270 | composite nozzle temperature fallback, after `fiber_nozzle_temperature_preheat` and then `nozzle_temperature_initial_layer` | b1 | dead normally. Introduced in 7ad1c8e08a (2026-07-23) |
| 6761 | 150 | plastic standby temperature (`fiber_nozzle_temperature_standby[0]`) | b1 | **LIVE**: the default is {0}. Introduced in 7ad1c8e08a |
| 6762 | 180 | composite standby temperature (`fiber_nozzle_temperature_standby[1]`) | b1 | **LIVE** |
| 6763 | 90.0 % | transition fan: plastic filament's `fan_max_speed` | b1 | dead: the vector is never empty. Repurposes an Orca filament key |
| 6764 | 25.0 % | laydown fan: composite filament's `fan_min_speed` | b1 | dead. Repurposes an Orca filament key |
| 6805 | 5; 1.0 | V/U/P decimals; V multiplier while feeding | a | precision; identity |
| 6819 | clamp 0..1 | `point_at_fraction` | a | clamp |
| 6829-6830 | M2800, M400 | default cut commands | a | prot |
| 6831 | 3.2 | `;CUT DISTANCE` = cut − 3.2 | b2 | **LIVE** with an empty `fiber_cut_gcode` (default). Comment only in C++; in Python the same number moves the cut (§7.1) |
| 6852, 7107 | `T0 R`, T1 | tool switch | a | prot (T0 = fibre, T1 = plastic) |
| 6846 | F1800 E-15 | plastic retract before the fibre tool | b2 | hidden. Python has the same value |
| 6909 | ceil(L + cut) | `M1001 L` load estimate | a | fibre needed = route + cutter offset |
| 6936 | start x V/mm | priming V | a | derivation |
| 6938 | x1000 | s to ms for `G4 P` | a | unit |
| 6940 | G4 P0 | zero dwell | a | no-op |
| 6946 | L − cut | cut position | a | the cutter sits `cut_distance` upstream of the tip |
| 7069 | 0.05 | tail continuity tolerance (mm) | a | defensive epsilon |
| 7097 | V-1 | matrix retract at the route end | b2 | hidden. Python has the same value |
| 7105 | F600 V-14 | composite retract before the plastic tool | b2 | hidden. Python has the same value |
| 7108 | S255 | plastic fan full during the change back | b2 | 255 = full PWM, but running at 100 % is a process choice |
| 6855, 7113 | S0 | fan off | a | off |

### 1b. Other fibre helpers in GCode.cpp

| Loc | Value | Controls | Cls | Derivation / note |
|---|---|---|---|---|
| 212, 215 | 3 | XY decimals | a | 1 µm |
| 222 | 0 | F decimals | a | integer mm/min |
| 257 | clamp 0..100, x255/100 | percent to PWM | a | unit |
| 329 | 0.0 | linear density when unset | a | sentinel: mass 0, no guess |
| 332 | /1000000.0 | mm x g/km to g | a | unit (1 km = 1e6 mm) |
| 361 | ADVANCE=0 | pressure advance off for the composite job | b2 | hidden. Python has the same value |
| 362 | 0.8 | `MINIMUM_CRUISE_RATIO` | b2 | hidden. Python has the same value |
| 363 | 1 | `SQUARE_CORNER_VELOCITY` | b2 | hidden. Python has the same value |
| 364 | T=0 SCV=1 | tool corner velocity | b2 | hidden. Python has the same value |
| 392-393 | M104 S0 | heaters off at shutdown | a | off |
| 394 | M106 **P2** S0 | composite fan off at shutdown | b2 | hard-coded index; ignores `fiber_composite_extruder_fan_index` (default 0) |
| 3074-3075 | 1 or 0 | composite filament index | a | index |
| 3079 | 270 | composite temperature in the start contract | b1 | dead; duplicates 6760 |
| 3531 | 250 | initial plastic temperature | b1 | dead; duplicates 6756 |
| 3085, 3537 | max of `chamber_temperature` | chamber setpoint | a | aggregation |

### 1c. Called helper: `src/libslic3r/FiberseekCompositePlanner.cpp`

| Loc | Value | Controls | Cls | Derivation / note |
|---|---|---|---|---|
| 3649 | 6.0 | `rocket_style_transition_limit_mm` = 6 x line width: the longest connector treated as a transition and not as fibre | b2 | named after Rocket; no source. From 7ad1c8e08a. Also used by PrintObject.cpp:1312 |

---

## 2. Preview and mass: `src/libslic3r/GCode/GCodeProcessor.cpp` / `.hpp`

| Loc | Value | Controls | Cls | Derivation / note |
|---|---|---|---|---|
| cpp 53 (upstream) | 1.245 g/cm³ | `DEFAULT_FILAMENT_DENSITY`, returned as the **fibre density** when the diameter or linear density is 0 (101, 105) | b1 | **LIVE**: with a diameter but no linear density, the preview reports fibre grams at PLA density while the header says 0 g (§7.14) |
| cpp 101, 105, 184 | 1e-6 | zero guards | a | epsilon |
| cpp 104 | 0.25·pi·d² | strand area | a | geometry |
| cpp 105 | /(A·1000) | g/km to g/cm³ | a | g/km / mm² = 1e-6 g/mm³ = 1e-3 g/cm³ |
| cpp 187 | 0.5 | radius = d/2 | a | geometry |
| cpp 2488 | 0.0f | `fiber_diameter` fallback | a | **[fixed in 9fd0d379]**: was 0.25. Never reached anyway: the `fiber_diameter` key is coFloats, so `option<ConfigOptionFloat>` returns null |
| cpp 2489, 2972 | 0.001 | diameter floor | a | clamp |
| cpp 2493 | 0.0f | `fiber_linear_density` fallback | a | **[fixed in 9fd0d379]**: was 102 |
| hpp 947-948 | 0.0f, 0.0f | defaults of the no-argument `ensure_..._preview_filament()` | a | **[fixed in 9fd0d379]**: were 0.25 / 102 |
| cpp 2495, 2497 | d x 2.0 | preview width when `fiber_line_width` is missing or 0 | b2p | dead: the key always exists, default 0.8 |
| cpp 2498-2499 | clamp 0.25..0.65 | preview width | b2p | **LIVE**: clamps the default 0.8, and the generator's 0.68-0.80, to 0.65 |
| hpp 848, cpp 2718 | 0.45 | initial or reset preview width | b2p | live only for G-code that has fibre markers but no fibre flags |
| cpp 4115 | U x P | V when the line has no V | a | derivation |
| cpp 4180 | 0.16 | minimum preview height for fibre moves | b2p | hidden |
| cpp 6049 | 0.18 | fibre moves drawn 0.18 mm higher | b2p | hidden display offset |
| cpp 6434-6435 | x0.001 | mm to m; mm³ x g/cm³ to g | a | unit |

---

## 3. Python planner: `resources/orcaslicer_codex/fiber_planner/orcaslicer_codex_native_fiber_planner.py`

### 3a. `PlannerConfig` defaults (122-210), mapped through `planner_config()` (1188-1438)

The parsers work like this:
- `pos` (`parse_first_positive_float`, `speed_mm_s_to_feedrate`) treats a value of 0 or less as unset.
- `any` (`parse_first_float`) accepts 0.
- An `A or B` chain on config strings never reaches B when A is present, because the string "0" is truthy (§7.11).

| Loc | Field = default | Overridden by | Status | Cls | Note |
|---|---|---|---|---|---|
| 123 | cut_distance = 58.0 | `fiber_cut_distance` pos; then **replaced** by the `CUT DISTANCE` number in the cut G-code (1322-1324) | live-if-0 | b2 | effective value is cut − 3.2 = 54.8 (§7.1) |
| 124 | restart_length = 55.0 | `fiber_restart_length` pos | live-if-0 | b2 | C++ fallback is cut + 2 |
| 125 | start_length = 15.0 | `fiber_start_length` pos; payload | live-if-0 | b2 | C++ fallback is 0 |
| 126 | slow_length = 10.0 | `fiber_slow_length` pos; payload | live-if-0 | b2 | C++ uses 0 |
| 128 | fiber_diameter = 0.0 | `continuous_fiber_diameter`, then `fiber_diameter` (`first_set_float`) | sentinel | a | exits when 0 |
| 129 | fiber_linear_density = 0.0 | `continuous_fiber_linear_density`, then `fiber_linear_density`; a value < 1 is read as g/m (x1000, 577) | sentinel | a | mass 0 when unset |
| 130 | fiber_feed_factor = 0.0 | `fiber_feed_factor` (composite) | sentinel | a | **[fixed in 9fd0d379]**: replaces 0.835. Exits when 0 |
| 131-132 | fiber_p_value / fiber_v_per_mm = 0.0 | recomputed (1224-1225, 1279) | dead | a | **[fixed in 9fd0d379]**: were 0.041 (= pi·0.125²·0.835) |
| 133 | fiber_width = 0.80 | `fiber_line_width` pos (default 0.8); `--line-width` | dead | b2 | |
| 134 | min_radius = 12.0 | `max(fiber_min_radius, 12.0)` (1227) | **floor, LIVE** | b1 | a profile cannot go below 12 mm; the PrintConfig default 8 becomes 12. From 89f08ebacc |
| 135 | max_arc_segment_length = 3.0 | `fiber_max_arc_segment_length` pos (default 1.0) | dead; **live** for "rocket compare" or speedy/reinforced/fortify profiles whose value is 1.0 (1233) | b2 | Rocket-compat special case |
| 136 | mechanical_min_route_length = 55.0 | `fiber_mechanical_min_route_length` or `fiber_hardware_min_route_length` pos (default 55) | dead | b2 | |
| 137 | min_route_length = 10.0 | `fiber_min_route_length` pos; `--min-route-length` | live-if-0 | b2 | |
| 138 | perimeter_min_route_length = 55.0 | `fiber_minimum_perimeter_length` or `fiber_perimeter_min_route_length` (default 55) | dead | b2 | |
| 139 | thin_feature_min_route_lengths = [5.0, 2.0] | none | live | b2 | retry lengths when a layer gets no routes (3155) |
| 140 | infill_spacing = 4.0 | mode table | dead | b2 | |
| 141 | infill_density_percent = 0.0 | `fiber_infill_density` | sentinel | a | |
| 142 | perimeter_inset = 0.85 | `fiber_perimeter_inset` pos; CLI | live-if-0 | b2 | |
| 143 | infill_inset = 1.20 | `fiber_infill_inset` pos; CLI | live-if-0 | b2 | |
| 144 | travel_feedrate = 30000.0 | none | live | b2 | G0 to the route start (3206) and prime travel; C++ uses Orca travel |
| 145 | z_feedrate = 600.0 | none | live | b2 | |
| 146 | fiber_feedrate = 1800 (30 mm/s) | `fiber_normal_max_speed` pos; `--fiber-print-speed`; payload | live-if-0 | b2 | same as the C++ fallback |
| 147 | fiber_slow_feedrate = 300 (5 mm/s) | `fiber_start_max_speed` pos; `--fiber-start-speed` | live-if-0 | b2 | same as C++ |
| 148 | fiber_finish_feedrate = 900 (15 mm/s) | `fiber_finish_max_speed` pos | live-if-0 | b2 | metadata and summary only; never on a move |
| 149 | fiber_cut_tail_feedrate = 180 (3 mm/s) | `fiber_start_min_speed` pos (1266) | live-if-0 | b2 | post-cut tail speed; C++ uses finish max instead |
| 150-155 | start/normal/finish min and min-limit speeds = 3, 3, 5, 3, 5, 3 | the matching keys, pos | live-if-0 | b2 | only rewritten into the config block (3551); no motion |
| 156 | restart_feedrate = 1500.0 | none | live | b2 | = C++ |
| 157 | priming_feedrate = 600.0 | none | live | b2 | = C++ |
| 158-160 | tension_* = 0.0 | any | dead | a | 0 = off |
| 161 | feedrate_percent = 100.0 | `fiber_feedrate_percent` pos (default 100) | dead | a | identity |
| 162-163 | correction_move_feedrate(_percent) = 0.0 | `fiber_correction_move_speed` if override is on; percent any | dead | a | 0 = use travel |
| 164 | after_cut_plastic_extrusion_multiplier = 1.0 | `fiber_after_cut_plastic_extrusion_multiplier` pos (1211, 1288-1293); payload | dead | a | **[fixed in 9fd0d379]**: was a live 0.72 (from 89f08ebacc). The legacy guard at 1292 is now a no-op |
| 172-174 | layer_step 1, macro_layer_height 0.2, infill_trace_stride 1 | mode table | dead | b2 | |
| 176 | fiber_seam_angle = 0.0 | any | dead | a | |
| 177-178 | perimeter_routes_per_layer 4, hole_reinforcement_routes_per_layer 0 | mode table | dead | b2 | |
| 179 | hole_reinforcement_max_laps = 3 | none | live | b2 | |
| 180 | hole_reinforcement_max_radius_factor = 2.3 | none | live | b2 | |
| 182-183 | infill_routes_per_layer 8, max_routes_per_layer 80 | mode table | dead | b2 | |
| 184-186 | routes_per_cut 1, fiber_start_layer 0, fiber_path_phase 0 | keys or CLI | dead | a | index, count |
| 191-192 | "T0 …FIBER" / "T1 …PLASTIC" | none | live | a | prot |
| 193 | cut_gcode [M2800, M400, ";CUT DISTANCE 54.8"] | rebuilt from `fiber_cut_gcode` (default empty) and cut − 3.2 | dead | b2 | 54.8 = 58 − 3.2 |
| 194 | plastic_nozzle_temperature = 260.0 | `nozzle_temperature[0]` or `nozzle_temperature_initial_layer[0]` pos | dead | b1 | |
| 195 | fiber_nozzle_temperature = 275.0 | `nozzle_temperature[1]` or `_initial_layer[1]`, only with 2 or more values | dead with 2 filaments; **live** with 1 | b1 | ignores `fiber_nozzle_temperature_preheat`, which C++ reads. From 89f08ebacc |
| 196 | plastic_standby_temperature = 150.0 | `fiber_nozzle_temperature_standby[0]`, only with 2 or more values | live-if-0 | b1 | = C++ |
| 197 | fiber_standby_temperature = 180.0 | `fiber_nozzle_temperature_standby[1]` (or [0] when there is one value) | live-if-0 | b1 | = C++ |
| 198 | bed_temperature = 80.0 | `hot_plate_temp_initial_layer`, `hot_plate_temp` or `first_layer_bed_temperature`, last positive | dead | b1 | |
| 199 | chamber_temperature = 0.0 | `chamber_temperature` | dead | a | 0 = off |
| 200 | toolchange_z_lift = 5.0 | none | live | b2 | the C++ change blocks have no Z lift |
| 201 | fiber_transition_fan_speed = 38 (PWM, about 15 %) | none | live | b1 | C++ uses the plastic filament's `fan_max_speed` |
| 202 | fiber_laydown_fan_speed = 63 (PWM, about 25 %) | none | live | b1 | C++ uses the composite filament's `fan_min_speed`. From 89f08ebacc |
| 203 | active_tool_fan_speed = 255 | none | live | b2 | full PWM; the choice is process |
| 205-206 | prime line centre X 152.5, Y 119.0 | payload `fiber_prime` | live | b2 | bed coordinates, not derived from `printable_area` |
| 207 | prime line length = 90.0 | payload; floored at cut + 1 | live | b2 | |
| 208 | prime line height = 0.2 | `fiber_priming_line_height` pos (default {0}) | live-if-0 | b2 | |
| 209 | prime travel Z = 1.2 | payload | live | b2 | |
| 210 | prime dwell = 1000 ms | payload | live | b2 | |

### 3b. Literals in functions that compute feeds, speeds, P/V, lengths or temperatures, or that write G-code

| Loc | Value | Controls | Cls | Derivation / note |
|---|---|---|---|---|
| 577 | x1000 if 0 < v < 1 | linear density in g/m read as g/km | a | unit conversion with a plausibility threshold (no real strand weighs under 1 g/km) |
| 620-621 | /60, x60 | mm/s and mm/min | a | unit |
| 626, 629 | 3.2 | `;CUT DISTANCE` = cut − 3.2, **then parsed back as the cut distance** | b2 | **LIVE**; moves the cut and the M1001 estimate (§7.1) |
| 626, 629 | M2800, M400 | default cut | a | prot |
| 685 | x100/density, floor = width | spacing from density | a | spacing = width / (density / 100) |
| 1224 | pi(d/2)^2, round 5 | P | a | geometry, precision |
| 1279 | /100 | feed percent | a | percent |
| 1360, 1405, 1033 | clamp 0..100 | density | a | clamp |
| 1400, 1420 | 0.01, 0.1 | CLI width and minimum-length floors | a | clamp |
| 1426, 1428 | x60 | CLI speeds | a | unit |
| 3203 | /100, floor 1.0 | approach feed as a percentage of print feed | a | percent, clamp |
| 3224-3229 | 1.0; 5 | V multiplier while feeding; decimals | a | identity, precision |
| 3269 | ceil(L + cut) | M1001 L | a | as in C++ (but uses the 54.8 cut) |
| 3276 | `;HEIGHT:0.2` | route preview height | b2p | fixed; C++ writes the real layer height |
| 3284-3285 | V = start x V/mm | priming V | a | derivation |
| 3286 | G4 P0 | dwell | a | no-op |
| 3295, 3436 | V-1 at 600 | end retract | b2 | = C++ |
| 3328-3331 | ADVANCE=0, 0.8, 1, SCV=1 | motion contract | b2 | = C++ (counted once in 1b; not counted here) |
| 3351 | M106 **P1** S0 | plastic fan off at start | b2 | hard-coded fan index |
| 3368, 3446 | max(z + lift, lift) | change-Z formula | a | formula. The lift is in 3a |
| 3373 | F1800 E-15 | plastic retract | b2 | = C++ |
| 3376, 3381, 3383, 3385, 3454, 3460, 3471 | **P1/P2** | fan index per tool | b2 | hard-coded. C++ uses the `fiber_*_extruder_fan_index` settings (both default 0) |
| 3390 | +1.0 | prime line at least cut + 1 mm long | b2 | margin |
| 3393 | −1.0 | prime approach lead-in | b2 | |
| 3396 | +0.2 | prime travel clearance | b2 | |
| 3408 | ceil(length) | prime M1001 L | a | load = line length |
| 3409 | min(restart, **55.0**) | restart U capped for the prime line | b2 | **LIVE**, hidden cap |
| 3411 | **F720** V{start_length} | prime-line priming | b2 | hidden feed. V = start_length mm, not start x V/mm as on routes (§7.13) |
| 3437 | −1.2 | prime back-off X | b2 | |
| 3451 | F600 V-14 | composite retract | b2 | = C++ |
| 3469-3470 | M104 S0 | heaters off | a | off |
| 3686-3690 | P3/P5 S0 at layer ≤ 1, S255 at layer ≥ 5 | auxiliary fan schedule | b2 | **LIVE**, hidden. Not in the C++ writer |
| 3552-3554 | /60 | metadata | a | unit |
| 3755 | /1_000_000 | mm x g/km to g | a | unit |
| 46-54 | 0/90, 45/135, 0/60/120, … | pattern angle sets | a | pattern geometry |
| 75-89 | 0…20, 99 (3124) | route sort priority | a | ordering |

### 3c. Mode table `apply_mode_defaults` (963-995): light / medium / heavy, all b2

| Loc (L/M/H) | Field | L / M / H | Status |
|---|---|---|---|
| 965/976/988 | infill_spacing | 7.5 / 4.5 / 2.8 | live-if-0 (`fiber_infill_spacing`, `fiber_infill_density`, `--spacing`, `--density`) |
| 966/977/989 | layer_step | 3 / 2 / 1 | live-if-0 (`fiber_layer_step`) |
| 967/978/990 | macro_layer_height | 0.6 / 0.4 / 0.2 | live-if-0 (`fiber_macro_layer_height`); set to 0 when the layer step is set |
| 968/979/991 | infill_trace_stride | 3 / 2 / 1 | live (payload only) |
| 969/980/992 | perimeter_routes_per_layer | 1 / 2 / 4 | live (payload only) |
| 970/981/993 | hole_reinforcement_routes_per_layer | 1 / 3 / 6 | live (payload only) |
| 971/982/994 | infill_routes_per_layer | 2 / 4 / 8 | live (payload only) |
| 972/983/995 | max_routes_per_layer | 32 / 80 / 180 | live-if-0 (`fiber_max_routes_per_layer`) |

The C++ route planner has its own mode defaults in PrintObject.cpp:240-290 (layer step, spacing). These were not audited.

### 3d. Route-search geometry heuristics, grouped by function (w = fibre width, R = min_radius)

These are b2 (geometry heuristics) unless marked a. Values that are pure epsilons, point counts, pi, or the "/2" of a centre are a.

- `parse_gcode` (354): 0.05 path gap, 0.01 minimum XY for an extrusion. a (epsilons).
- `closed_outer_polygons` (1443): an outer wall counts as closed if its ends are within **1.2 mm**. b2.
- `normalized_route_points` / `path_is_closed` (1573, 1579): 0.05 closure. a.
- `circumradius` (1595): abc/(2·area2). a.
- `select_spatially_phased_routes` (1693): peripheral threshold max(w, 0.5); core distance max(0.75R, 0.35·median, 6w); core extent max(4R, 1.6·median extent); 8 sectors of 45°. b2.
- `deterministic_closed_route_start_index` (1865): 2654435761, 40503, 97, 100.0. a (hash constants for a deterministic seam).
- `fiber_seam_rotated_route` (1901): 90° for "rear". a (direction).
- `smooth_cluster_halo` / `smooth_circular_trace_route` / `smooth_circular_orbit_route` (1989-2045): segment count from 2πr / max(arc_seg, 0.5), at least 8 or 24 points. a (discretisation), with b2 floors 0.5 / 8 / 24.
- `connector_is_printable`, `route_is_printable`, `internal_void_shell_groups` (2078, 2090, 2316): 0.05 buffer. a.
- `grouped_hole_loop_infos` (2098-2099): centre tolerance max(0.45, 0.6w); radius tolerance max(3w, 3.0). b2.
- `route_avoids_hole_voids` (2143): clearance max(0.35w, 0.25). b2.
- `expanded_hole_orbit_route` (2163-2200): radius R + 0.25, L_hw/(2π) + 0.25, hole r + max(w, 0.5); maximum radius min(R·2.3, R + max(6w, 4)); step max(w, 0.5). b2 (the 2π is a).
- `local_hole_cluster_reinforcement_routes` (2222-2282): cluster distance max(2.35R, 12w); centre span max(3.5R, 24); span + 2(R + max(6w, 4)); base R + 0.25; step max(w, 0.5). b2.
- `stitched_internal_void_group_routes` (2404-2492): core offset max(0.65R, 5w); hole offset max(4w, 2.0); shell buffer max(0.45w, 0.25); connector clearance max(0.2w, 0.08); laps ≤ max(max_laps + 2, 5); 8 x 45° sectors. b2.
- `stitched_short_perimeter_routes` (2517): connector gap min(max(4w, 2.5), max(0.5R, 2.5)). b2.
- `traced_hole_reinforcement_routes` (2706-2766): equivalent radius L/(2π) (a); separation max(R, 8w). b2.
- `clipped_infill_routes` (2896-2897): hatch extent = diagonal + 4·spacing (b2 margin); centre /2 (a).
- `selected_layer_indexes` (2968): 0.5 rounding of the macro layer. a.
- Count: about 134 literals (excluding 0 and 1) in 32 functions. About 53 are b2 heuristic coefficients or floors; about 81 are a (point counts, pi factors, epsilons, sector geometry, hash constants). The 9 functions not named above hold only counts and indices (a).

---

## 4. `src/libslic3r/PrintConfig.cpp` defaults

Consumers: W = C++ writer (GCode.cpp), CP = C++ route planner (PrintObject.cpp / FiberseekCompositePlanner), P = Print.cpp, which passes the value as a CLI argument only when it is > 0, Py = Python field, GP = GCodeProcessor, M = FibreSeekMaterials.cpp. "none" = no consumer outside the GUI and presets.

| Line | Key | Default | Cls | Python field / consumers | Note |
|---|---|---|---|---|---|
| 3041 | fiber_diameter | {0.0} | a | Py fiber_diameter (2nd key); M copies it to `continuous_fiber_diameter` | composite strand, unset |
| 3049 | fiber_linear_density | {0.0} g/km | a | Py fiber_linear_density (2nd key); M | **[fixed in 9fd0d379]**: label was g/m |
| 3059 | fiber_feed_factor | {0.0} | a | Py fiber_feed_factor; W via the composite slot | **[new in 9fd0d379]**: replaces 0.835; no default |
| 3131 | fiber_nozzle_temperature_preheat | {0} | a | W only (Py ignores it) | 0 = use `nozzle_temperature_initial_layer` |
| 3139 | fiber_nozzle_temperature_standby | {0} | a | Py plastic/fiber_standby_temperature; W | 0 lets the 150/180 fallbacks through |
| 3171 | fiber_restart_pause | {0.0} | a | W | off |
| 3187 | fiber_finish_ironing_distance | {0.0} | a | W | off |
| 3195 | fiber_priming_line_height | {0.0} | a | Py fiber_prime_line_height | 0 lets 0.2 through |
| 3440 | fiber_start_layer | 0 | a | Py fiber_start_layer; CP; P | index |
| 3456 | fiber_min_radius | **8.0** | b1 | Py min_radius (floored at 12) | process-profile key; bend radius depends on the strand; CP does not read it |
| 3465 | fiber_max_arc_segment_length | **1.0** | b2 | Py max_arc_segment_length | special-cased for Rocket profiles |
| 3474 | fiber_start_length | 0.0 | a | Py start_length; W | 0 lets Py 15 through (C++ 0) |
| 3483 | fiber_slow_length | 0.0 | a | Py slow_length; W | 0 lets Py 10 through (C++ 0) |
| 3492, 3501, 3510 | fiber_tension_length / _feedrate / _release_fraction | 0.0 | a | Py tension_*; CP (fraction) | off |
| 3519 | fiber_feedrate_percent | **100.0** | a | Py feedrate_percent; W | identity; overlaps `fiber_feed_factor` (§5) |
| 3528 | fiber_start_max_speed | 0.0 | a | Py fiber_slow_feedrate (1st key) | W ignores it |
| 3537 | fiber_start_min_speed | 0.0 | a | Py fiber_start_min_speed and fiber_cut_tail_feedrate | 0 lets 3 / 180 through |
| 3546 | fiber_start_min_limit_speed | 0.0 | a | Py fiber_start_min_limit_speed | metadata only |
| 3555 | fiber_normal_max_speed | 0.0 | a | Py fiber_feedrate (1st key); W (1st) | 0 lets 30 through |
| 3564, 3573 | fiber_normal_min_speed / _min_limit_speed | 0.0 | a | Py fields of the same name | metadata only |
| 3582 | fiber_finish_max_speed | 0.0 | a | Py fiber_finish_feedrate; W | 0 lets W 3 / Py 15 through |
| 3591 | fiber_finish_min_speed | 0.0 | a | Py fiber_finish_min_speed; W | |
| 3600 | fiber_finish_min_limit_speed | 0.0 | a | Py fiber_finish_min_limit_speed | metadata only |
| 3616 | fiber_correction_move_speed | 0.0 | a | Py correction_move_feedrate (only with the override); W always | 0 lets W 2 through |
| 3625 | fiber_correction_move_feedrate_percent | 0.0 | a | Py correction_move_feedrate_percent | off |
| 3633 | fiber_after_cut_plastic_extrusion_multiplier | **1.0** | a | Py after_cut_plastic_extrusion_multiplier; W | identity; the quantity is b1 (§5) |
| 3648, 3657, 3666, 3674 | fiber_first_layer_flow_ratio / _line_width / _height / _speed_ratio | 0.0 | a | none | unused settings |
| 3702 | fiber_infill_density | 0 % | a | Py (via spacing); CP; P | unset |
| 3751 | fiber_seam_angle | 0.0 | a | Py fiber_seam_angle; CP; P | |
| 3760 | fiber_line_width | **0.8** | b2 | Py fiber_width; W; CP; GP; P | not the nozzle diameter (0.4); generator uses 0.68 / 0.80 / 0.70 |
| 3769 | fiber_infill_spacing | 0.0 | a | Py infill_spacing; CP; P | 0 lets the mode table through |
| 3778 | fiber_macro_layer_height | 0.0 | a | Py macro_layer_height | 0 lets the mode table through |
| 3787 | fiber_layer_step | 0 | a | Py layer_step; CP; P | 0 lets the mode table through |
| 3796 | fiber_min_route_length | 0.0 | a | Py min_route_length; CP; P | 0 lets Py 10 / CP 10 through |
| 3805 | fiber_perimeter_min_route_length | **55.0** | b2 | Py perimeter_min_route_length; CP | |
| 3814 | fiber_mechanical_min_route_length | **55.0** | b2 | Py mechanical_min_route_length; CP | machine minimum, tied to the cutter and restart geometry |
| 3823 | fiber_perimeter_inset | 0.0 | a | Py perimeter_inset; CP; P | 0 lets Py 0.85 through |
| 3832 | fiber_infill_inset | 0.0 | a | Py infill_inset; P | 0 lets Py 1.20 through |
| 3841 | fiber_print_speed | 0.0 | a | Py fiber_feedrate (CLI, wins); W (2nd) | |
| 3850 | fiber_start_speed | 0.0 | a | Py fiber_slow_feedrate (CLI, wins); W | |
| 3858 | fiber_max_routes_per_layer | 0 | a | Py max_routes_per_layer; CP; P | 0 lets the mode table through |
| 3866 | fiber_routes_per_cut | 1 | a | Py routes_per_cut; P | one route per cut |
| 3874, 3882 | fiber_outer_ / fiber_inner_perimeter_loops | **1**, **1** | b2 | CP | |
| 3890, 3898 | fiber_plastic_outer_ / _inner_loops_with_fiber | **2**, 0 | b2 | none | unused; the 2 is meaningless today |
| 3447 | fiber_print_order_code | 0 | a | none | unused |
| 5699, 5707 | plastic_ / composite_nozzle_diameter | **0.4**, **0.4** | b2 | M (profile matching) | hardware default; not used by the writers |
| 5714-5749 | fiber_{plastic,composite}_extruder_offset_{x,y,z} | 0.0 | a | none | unused |
| 5757, 5765, 5799, 5807 | heat-up speeds (plastic, composite, bed, chamber) | 0.0 | a | none | unused |
| 5784, 5791 | fiber_plastic_ / fiber_composite_extruder_fan_index | **0**, **0** | b2 | W | both default to fan P0; Python and the C++ shutdown use P1/P2 (§7.9) |
| 5814 | fiber_motion_blocks_buffer_size | 0 | a | none | unused |
| 5822 | fiber_cut_distance | 0.0 | a | Py cut_distance; W; CP | 0 lets W/Py 58 through; CP keeps 0 |
| 5830 | fiber_restart_length | 0.0 | a | Py restart_length; W | 0 lets W cut + 2 / Py 55 through |
| 5845, 5853 | fiber_nozzle_contact_radius(_extended) | 0.0 | a | none | unused |
| 5905 | continuous_fiber_diameter | 0. | a | Py (1st key); W; GP; M | |
| 5913 | continuous_fiber_linear_density | 0. g/km | a | Py (1st key); W; GP; M | |
| 5919 | fiber_postprocessor_type | 0 | a | P | |
| 3067-3123 | fiber_spool_length_km, fiber_cost, fiber_plastic_diameter, _density, _cost, _spool_weight | 0.0 | a | none | unused |
| 3147, 3155, 3163 | fiber_first_layers_height, fiber_plastic_extrusion_speed, fiber_extrusion_speed | 0.0 | a | none | unused |
| 5879, 5885, 5897 | continuous_fiber_name / _type / _source_material_id | "Carbon fiber 0.25 mm", "carbon_fiber", "tinmanx1-carbon-025" | b1 | Py (material-tuning lookup); GP; M | identity defaults that name one strand while the diameter default is 0 (§7.15) |
| 5891 | continuous_fiber_material_kind | "continuous_fiber" | a | M | role tag |
| (rest) | strings, bools, enums: fiber_name/type/manufacturer, fiber_plastic_*, fiber_material_kind, fiber_source_material_id, fiber_cut_gcode, payloads, toolchange G-code, slot roles = ""; fiber_enabled, generate_*, shared_nozzle, override_correction_speed, z_hop_after_cut, has_fan = false; manufacturing mode PlasticPlusFiberOverlay, reinforcement Light, pattern Solid, infill source PlasticTraces, seam Source | n/a | a | various | non-numeric: off or unset. An empty `fiber_cut_gcode` turns on the 3.2 path |

---

## 5. Summary counts

Counted by table row, from the class column. A grouped row counts once. The §3d geometry literals are counted separately. Rows marked [fixed in 9fd0d379] are counted by what the code holds now. A 0 sentinel or an identity counts as a. The feed-factor read in 1a counts as b1: it is an exposed setting with no default.

| Section | a | b1 | b2 | b2p | total |
|---|---|---|---|---|---|
| 1a C++ writer | 26 | 7 | 16 | 0 | 49 |
| 1b other GCode.cpp helpers | 8 | 2 | 5 | 0 | 15 |
| 1c FiberseekCompositePlanner | 0 | 0 | 1 | 0 | 1 |
| 2 GCodeProcessor | 10 | 1 | 0 | 5 | 16 |
| 3a PlannerConfig | 13 | 8 | 35 | 0 | 56 |
| 3b planner literals | 21 | 0 | 13 | 1 | 35 |
| 3c mode table | 0 | 0 | 8 | 0 | 8 |
| 4 PrintConfig | 51 | 2 | 8 | 0 | 61 |
| **Total** | **129** | **20** | **86** | **6** | **241** |
| 3d geometry literals (excluding 0 and 1) | ≈81 | 0 | ≈53 | 0 | ≈134 |

---

## 6. b1 items, and the fewest composite-filament settings that cover them

**Already resolved in 9fd0d379.** These no longer ship as hidden values:
- the 0.835 feed coefficient, now `fiber_feed_factor` with no default;
- the preview fallbacks 0.25 mm and 102 g/km;
- the planner's P/V defaults of 0.041;
- the planner's live after-cut default of 0.72;
- the g/m vs g/km unit label.

The b1 items still in the code are below. Items 1-15 are the b1 rows counted in §5. Items 16-19 are b1 quantities whose current default is an identity or a setting; §5 counts them as a, b2 or b1.

| # | Loc | Value | Live? |
|---|---|---|---|
| 1 | GCode.cpp:6761; py:196 | plastic standby 150 °C | **live** (the standby key defaults to {0}) |
| 2 | GCode.cpp:6762; py:197 | composite standby 180 °C | **live** |
| 3 | GCode.cpp:6760, 3079 | composite nozzle 270 °C | dead |
| 4 | py:195 | composite nozzle 275 °C; ignores `fiber_nozzle_temperature_preheat` | **live** with a single-filament config |
| 5 | GCode.cpp:6756, 3531 | plastic nozzle 250 °C | dead |
| 6 | py:194 | plastic nozzle 260 °C | dead |
| 7 | py:198 | bed 80 °C | dead |
| 8 | GCode.cpp:6763 | transition fan = plastic `fan_max_speed`, fallback 90 % | dead (borrowed Orca key) |
| 9 | py:201 | transition fan 38 PWM | **live** |
| 10 | GCode.cpp:6764 | laydown fan = composite `fan_min_speed`, fallback 25 % | dead (borrowed Orca key) |
| 11 | py:202 | laydown fan 63 PWM (89f08ebacc) | **live** |
| 12 | py:134, 1227 | bend-radius floor 12 mm (89f08ebacc) | **live** |
| 13 | PrintConfig:3456 | `fiber_min_radius` default 8.0, in the process profile | live (then floored to 12) |
| 14 | GCodeProcessor.cpp:53, 105 | fibre density falls back to 1.245 g/cm³ (PLA) | **live** when the linear density is unset |
| 15 | PrintConfig:5879/5885/5897 | default strand identity "Carbon fiber 0.25 mm" / "tinmanx1-carbon-025" | live (identity) |
| 16 | PrintConfig:3633 and py:164 | after-cut matrix multiplier: the default 1.0 is identity, but the quantity is matrix and fibre dependent and sits in the *process* profile (the generator sets 0.58 / 0.72 per mode) | exposed |
| 17 | PrintConfig:3519 | `fiber_feedrate_percent` 100: a second feed calibration that multiplies `fiber_feed_factor` | exposed |
| 18 | py:3411 | prime line primes `V = start_length` (15 mm of matrix), not start x V/mm | **live**; a flow quantity with no derivation (counted b2 in §3b with its F720 feed) |
| 19 | GCode.cpp:6740 | `fiber_feed_factor` | resolved; listed for completeness |

Proposal: four groups, at most two new keys. All of them live in the composite filament profile.

1. **Fibre strand.** Keep `fiber_diameter`, `fiber_linear_density` and `fiber_feed_factor`. Add **`fiber_min_bend_radius`** (mm, no default). It replaces the 12 mm floor and the 8.0 process default (items 12-13). Alternatively, move `fiber_min_radius` into the composite profile with no default. Drop the `DEFAULT_FILAMENT_DENSITY` fallback for the fibre: with no linear density, show no mass (item 14). Default the three `continuous_fiber_*` identity strings to "" (item 15).
2. **Composite temperatures.** No new key. Make the existing per-filament `fiber_nozzle_temperature_preheat` and `fiber_nozzle_temperature_standby` required for both slots, and fail like the diameter does when one is 0. Make the planner read `fiber_nozzle_temperature_preheat` as the C++ writer does. Delete the 150/180/250/260/270/275/80 fallbacks (items 1-7). The dead ones duplicate Orca's own nozzle and bed temperatures.
3. **Composite cooling.** No new key if the C++ convention is kept: plastic `fan_max_speed` for the transition and composite `fan_min_speed` for laydown. The planner must read the same keys and the `fiber_*_extruder_fan_index` settings, and drop 38/63 (items 8-11). If borrowing Orca's min/max fan keys is judged too implicit, add one key, **`fiber_laydown_fan_speed`** (%), and use plastic `fan_max_speed` for the transition.
4. **Matrix flow around the fibre.** Move `fiber_after_cut_plastic_extrusion_multiplier` into the composite profile; it can be renamed `fiber_after_cut_matrix_flow`. Fold `fiber_feedrate_percent` into `fiber_feed_factor`, or keep it only as a documented process trim (items 16-17). Give the prime-line priming the same derivation as routes, start x V/mm (item 18).

---

## 7. Cross-writer divergences and other findings

These are not classifications, but the owner should see them.

1. **[fixed in the planner swap: both writers cut at L − `fiber_cut_distance` and print that value]** **Cut position differs by 3.2 mm.**
   - C++ cuts at L − `fiber_cut_distance` (58) and only *prints* `;CUT DISTANCE 54.8`.
   - Python parses that number back into `cut_distance` (py:1322-1324), so it cuts at L − 54.8 and its M1001 estimate is 3.2 mm shorter. The generator's profiles set `fiber_cut_gcode` to `;CUT DISTANCE 54.8` literally.
2. **[fixed in the planner swap: both use `fiber_restart_length`, required; Python's prime line still caps it at 55]** **Restart length.** C++ uses cut + 2 (60). Python uses 55, and the prime line caps it at 55.
3. **[fixed in the planner swap: both use `fiber_start_length` and `fiber_slow_length`, 0 = off. C++ showed 0/0 because the writer never received the process's fibre settings]** **Start and slow lengths.** C++ uses 0 and 0. Python uses 15 and 10.
4. **Post-cut tail speed.**
   - C++ uses `fiber_finish_max_speed` (fallback 3).
   - Python uses `fiber_start_min_speed` (fallback 3). Its 15 mm/s finish feed never reaches a move.
5. **Correction or approach moves.**
   - C++ always uses `fiber_correction_move_speed` (fallback 2 mm/s) and ignores `fiber_override_correction_speed`.
   - Python uses travel 30000 unless the override is on.
6. **Start speed.** C++ reads only `fiber_start_speed`. Python reads `fiber_start_max_speed`, then the CLI `fiber_start_speed` wins.
7. **Print speed precedence.** C++ prefers `fiber_normal_max_speed`. Python's CLI `fiber_print_speed` overrides it.
8. **Composite nozzle temperature.** C++ reads `fiber_nozzle_temperature_preheat`. Python ignores it.
9. **Fans.**
   - C++ uses the fan-index settings, which default to P0 for both tools. Its shutdown hard-codes P2.
   - Python hard-codes P1 (plastic) and P2 (composite), adds an auxiliary P3/P5 schedule at layers 1 and 5, and uses PWM 38/63/255.
10. **Toolchange Z lift.** Python lifts 5 mm on each change. The C++ change blocks emit no Z move of their own; the brush macros may move Z.
11. **`or` chains on config strings** (py:1243, 1247, 1267-1269, 1229). A present key holding "0" is truthy. The fallbacks `fiber_print_speed`, `fiber_start_speed`, `fiber_start_min_limit_speed`, `fiber_finish_min_limit_speed` and `fiber_hardware_min_route_length` are therefore never read from the config block. The CLI covers the two speeds.
12. **[fixed in the planner swap: the old planner and its 5 mm margin are gone; the new planner and the writer read the same `fiber_cut_distance`]** **Route planner vs writer cut distance.** PrintObject.cpp:1299 passes the raw `fiber_cut_distance`, which is 0 by default, plus a hidden 5.0 mm safety margin (1300). The writer substitutes 58, so the planner's cut-safety check and the emitted cut disagree on default profiles. PrintObject.cpp was not audited further.
13. **Prime line.** Only Python emits one: X 152.5, Y 119, 90 mm long, Z 0.2, travel Z 1.2, 1000 ms dwell, F720. It primes `V = start_length` (15 mm of matrix) where routes prime `start_length x V/mm` (≈ 0.6 for a 0.25 mm strand).
14. **[fixed in #21]** **Preview mass vs header.** The commit message of 9fd0d379 says the preview shows no fibre mass without strand data. That holds only when the diameter is also missing. With a diameter set and no linear density, `orcaslicer_codex_continuous_fiber_density` returns 1.245 and the preview shows grams, while the G-code header says 0 g.
15. **Default strand identity.** The planner selects `material_tuning` payload entries by `continuous_fiber_source_material_id`. Its default `tinmanx1-carbon-025` can match tuning for a strand the user never chose.
16. **Unused settings.** These have no consumer:
    - first-layer fibre overrides;
    - extruder offsets and heat-up speeds;
    - nozzle contact radii;
    - `fiber_plastic_*_loops_with_fiber`, whose default is 2;
    - `fiber_print_order_code` and `fiber_motion_blocks_buffer_size`;
    - spool and cost data.

---

## 8. b2 items grouped by theme

- **Cut geometry and fibre lengths:**
  - cut distance 58: GCode 6720, py 123, PrintObject 1299 raw;
  - CUT DISTANCE offset 3.2: GCode 6831, py 626/629, py 193 (54.8);
  - restart length: cut + 2 (GCode 6721), 55 (py 124), cap 55 (py 3409);
  - start length 15 (py 125) and slow length 10 (py 126);
  - transition limit 6 x width (FCP 3649);
  - cut safety margin 5.0 (PrintObject 1300).
- **Route acceptance:** min route lengths 10 / 55 / 55 / [5, 2] (py 137, 136, 138, 139; PrintConfig 3805, 3814); `fiber_line_width` 0.8 (GCode 6719, PrintConfig 3760, py 133); max arc segment 3.0 with the Rocket special case (py 135, 1233); insets 0.85 / 1.20 (py 142, 143).
- **Fibre speeds:**
  - print 30 (GCode 6726, py 146), start 5 (6727, py 147);
  - finish or tail: 3 (6728), 15 (py 148), 3 (py 149);
  - correction 2 (6732);
  - min and limit speeds, metadata only (py 150-155).
- **Fixed feeds:** restart U 1500 (GCode 6733, py 156); priming and retract 600 (GCode 6734, py 157); prime F720 (py 3411); travel 30000 (py 144); Z 600 (py 145).
- **Retracts around toolchanges:** F1800 E-15 (GCode 6846, py 3373); F600 V-14 (GCode 7105, py 3451); route end V-1 (GCode 7097, py 3295/3436).
- **Machine motion contract:** PA 0, MINIMUM_CRUISE_RATIO 0.8, square-corner velocity 1, tool SCV 1 (GCode 361-364, py 3328-3331).
- **Fan and tool mapping:**
  - fan indices: P0 defaults (PrintConfig 5784/5791), hard-coded P1/P2 (py), P2 at C++ shutdown (GCode 394);
  - S255 full fan on change (GCode 7108, py 203);
  - auxiliary P3/P5 schedule at layer ≤ 1 / ≥ 5 (py 3686-3690);
  - toolchange Z lift 5 (py 200).
- **Prime line:** centre 152.5 / 119, length 90, height 0.2, travel Z 1.2, dwell 1000 ms (py 205-210); margins +1.0, −1.0, +0.2, −1.2 (py 3390-3437).
- **Route density and selection:**
  - mode table (§3c: spacing 7.5 / 4.5 / 2.8, layer step 3 / 2 / 1, macro height 0.6 / 0.4 / 0.2, routes per layer, 32 / 80 / 180);
  - hole laps 3 and radius factor 2.3 (py 179-180);
  - fibre loops 1 / 1 and the unused 2 (PrintConfig 3874-3898);
  - nozzle diameters 0.4 / 0.4 (PrintConfig 5699/5707);
  - §3d geometry heuristics.
- **Preview only (b2p):** width clamp 0.25-0.65 and default 0.45, lift 0.18, minimum height 0.16 (GCodeProcessor); `;HEIGHT:0.2` (py 3276).
