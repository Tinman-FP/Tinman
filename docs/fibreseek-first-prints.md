# FibreSeek Seeker 3: first prints from Tinman

This is the plan for the first real prints sent to a FibreSeek Seeker 3 from
Tinman. You run these prints yourself. Development sessions never contact the
printer. Stay with the printer for every print, and stop it if fibre tangles,
drags, or the cutter misfires.

Do the prints in order. Go on to the next one only when the previous one
passed.

## Before you start

**Tinman.** Use a build that has the FibreSeek print host ("FibreSeek Seeker 3"
under Host Type) and the continuous-fibre panel. The panel appears under the
filament list when a Seeker 3 profile is selected.

**Printer profile.** Tinman does not ship a Seeker 3 profile yet, so use your
own. Check that it has:

- Printer model `FibreSeek Seeker 3`.
- Two nozzles. Nozzle 1 is the 0.4 mm polymer nozzle (T1 on the printer) and
  takes material 1. Nozzle 2 is the 0.7 mm matrix and fibre nozzle (T0) and
  takes material 2.
- Single Extruder Multi Material turned off, since each material has its own
  nozzle. With it on, a new project starts with only one material.
- Plastic nozzle diameter 0.4 and composite nozzle diameter 0.7.
- Fiber enabled turned on, with your fibre cut G-code and cut distance.
- Material 1: an ordinary PCTG profile. Material 2: a composite profile whose
  filament type is `CFC`, for example CFC PCTG + X-CCF.
- In that composite profile's fibre settings, fill in three values. Tinman
  ships no fibre material data, so a fibre slice stops and names any of these
  that are missing:
  - **Fiber diameter** (mm), from the fibre's datasheet.
  - **Fiber linear density** (g/km), from the datasheet. This is only used for
    the fibre weight estimate.
  - **Composite plastic multiplier** (the `fiber_feed_factor` key, formerly
    "Fiber feed factor"): FibreSeek's ExtrusionMultiplierP, 0.8 for CFC PETG +
    X-CCF. The plastic the composite nozzle extrudes per mm of fibre (P) is
    this times the bead's cross-section (Fiber line width × layer height) less
    the fibre's, over the composite filament's cross-section. Tinman has no
    default.

**Fibre planner.** In the default fibre mode, Tinman plans the fibre routes
with a Python helper run by `/usr/bin/python3`. That helper needs `shapely`.
Install it once, then check it:

```bash
/usr/bin/python3 -m pip install --user shapely
```

```bash
/usr/bin/python3 -c "import shapely; print(shapely.__version__)"
```

Without it, slicing a fibre job fails with "The native FibreSeek planner
needs shapely".

**Connection.** In the printer's connection settings, set Host Type to
**FibreSeek Seeker 3** and enter the printer's address (and its API key, if
you set one). Press Test. Tinman keeps a Seeker 3 on this host type even if
it was saved as Moonraker before. If the address is `https://`, the printer's
certificate must be valid, or set its CA file in the connection settings.

**Dry run, no printer.** From a Tinman source checkout, start the mock printer:

```bash
python3 tests/slic3rutils/fibreseek_mock_moonraker.py --port 7125
```

Add a second connection with address `127.0.0.1:7125`, send coupon 1 to it
with **Upload and print**, and check that Tinman reports no error. Then stop
the mock (Ctrl+C) and switch back to the real printer.

## What Tinman checks when you send

Tinman stops the send and tells you why if any of these fail:

- The file is plain G-code (not a 3MF) sliced with a Seeker 3 profile.
- Klipper is ready, nothing is printing or paused, and no error from the
  last job is still showing. A finished or cancelled job is fine. A file
  that is selected but not started blocks the send: clear it on the printer
  first.
- T1 has the polymer nozzle and T0 the matrix and fibre nozzle the job was
  sliced for.
- The copy stored on the printer matches the file byte for byte.
- It is still the same printer (by its host name) just before the start.

Each send gets its own file name on the printer: your file name plus two
short codes and the time, for example
`coupon_01-1a2b3c4d-20261116T140000-9f3c01aa.gcode`.

The send dialog says which materials the job needs on T1 and T0. Tinman
cannot see which materials are loaded, so check them on the printer before
you press **Upload and print**.

**Upload** only stores the file. **Upload and print** also starts it, once,
and then waits up to 10 minutes for the printer to report that it is
printing that file. If the start fails, or the printer does not confirm in
time, Tinman reports it and does not retry: check the printer before you
try again. Cancel in the upload queue works until the start is sent. After
that, Cancel only stops Tinman watching, and the printer may still be
printing. Tinman's start request does not pass the printer's own auto-levelling
or AI-detection options, so the printer uses its defaults. If the printer
does not prepare the way it does when you start a job from its screen, use
**Upload**, start the job from the printer's screen, and report what you saw.

## Make the coupons

```bash
python3 -m venv ~/fs-coupons-venv
~/fs-coupons-venv/bin/pip install cadquery
~/fs-coupons-venv/bin/python scripts/generate_fiberseek_test_coupons.py --output-dir ~/Documents/fibreseek-coupons
```

Use the `.stl` files. These are the coupons used below:

| Coupon | File | What it tests |
|---|---|---|
| 1 | `coupon_01_rect_plate_100x40x6` | Straight perimeters and ribs |
| 4 | `coupon_04_min_length_bars_40_55_65_90` | The 55 mm minimum route length and cuts |
| 2 | `coupon_02_single_hole_plate_100x60x6_h20` | A fibre loop around a hole |

Coupons 3, 5, 6 and 7 (more holes, bend radius, gear, separate islands) come
later, after these pass.

## Settings for every print

- Layer height 0.20 mm. One coupon per plate, in the middle of the bed.
- Fiber manufacturing mode: **Plastic + fiber overlay**. It is Tinman's
  default, but the generated Seeker 3 process profiles ("0.20mm Plastic +
  Continuous Fiber Light/Medium/Heavy") select "Fiber-owned composite roads",
  so check it after choosing one. Leave composite roads for later: that mode
  now uses the new route planner, which has never been printed.
- PCTG temperatures, bed and chamber as in your earlier plastic prints.
- In the preview, check before you send: the fibre summary (number of routes
  and fibre length), the first and last layer with fibre, and that no fibre
  path crosses a hole or leaves the part.

## The prints

### Print 1: matrix only, no fibre

- Coupon 1. Put the whole part on material 1 (PCTG).
- Turn off Fiber perimeters and Fiber infill.
- Send with **Upload and print**.

Check that the file arrives and the printer starts on its own without errors.
Check that it heats, homes and prints the first layer as it does for a job
started from its screen. Check adhesion, warping and the part's size. The
Tinman log line for this send must say `continuous fibre: no`.

### Print 2: the simplest fibre coupon

- Coupon 1 again.
- Material 1 PCTG. Pick the fibre in the continuous-fibre panel, which puts
  the matching composite profile on material 2. The panel matches profiles by
  their material data (type `CFC`, matrix type, fibre name and fibre type).
  If the choice jumps back, its tooltip says why. You can also pick the
  composite profile directly as material 2.
- Fiber reinforcement mode: Light. Fiber perimeters on, Fiber infill off.
- Fiber start layer: 4. Fibre starts on layer 4, and the last 4 layers are
  plastic only.

### Print 3: fibre ribs

- As print 2, with Fiber infill also on.

### Print 4: minimum route length and cuts

- Coupon 4, with settings as print 2.
- The 40 mm bar should get no fibre (below the 55 mm minimum). The 55, 65
  and 90 mm bars should get fibre. Check this in the preview first.

### Print 5: a hole

- Coupon 2, with settings as print 2.

## What to check on each fibre print

- **Fibre placement.** The fibre lies where the preview shows it. It is
  covered by plastic, not exposed on the top or bottom. It follows the walls,
  does not cross the hole, and is not pulled across travel moves.
- **Cuts.** Every route ends in one clean cut. Count the cuts and compare
  them with the route count in the preview. No fibre is dragged after a cut,
  and no cut is missed.
- **Adhesion.** The fibre is bonded to the layer below: try to lift it with a
  fingernail. The plastic printed over the fibre does not lift, and the layers
  above the fibre bond normally.
- **End of route.** After each cut, the short tail left in the nozzle is laid
  down without a blob or a string. The next route starts cleanly where the
  preview shows it, and the fibre feeds again without slipping.

## Known risks to watch

A review of the fibre output found these. Watch for them, and stop the print
if one causes trouble:

- **Prime line.** Both modes lay one 90 mm fibre prime line at the start of
  the first fibre layer, 2 mm in front of the first layer (behind it when the
  bed has no room in front). Rocket lays its line before the first layer
  instead, so watch the idle nozzle's clearance over the part while the line
  is laid at 0.2 mm.
- **Cut position.** Both modes cut when Fiber cut distance less Extended
  fiber contact radius is left of a route (58 − 1.8 = 56.2 mm in the Seeker 3
  test profiles). Check where the fibre ends against the preview.
- **Fixed values.** Fan speeds in the default mode are fixed values with no
  documented source. Note what the printer does at each tool change.

The full list is in `docs/fibreseek-constants-provenance.md`.

### What changes in the G-code after the planner swap

The planner swap (PR #25) changes what the printer receives. Old → new:

**Default mode** (Plastic + fiber overlay, the Python planner; the first prints use this):

- **Cut position:** L − 54.8 → L − 58. The cutter fires 3.2 mm earlier along each route and the prime line.
- **`;CUT DISTANCE` line:** 54.8 → 58.
- **`M1001 L`:** ceil(L + 54.8) → ceil(L + 58), about 3 mm more fibre loaded per route.
- **Shortest standalone route:** 64.8 → 68 mm (cut distance + slow length). Routes from 64.8 to 68 mm long are now left out.
- **Restart, start and slow lengths:** taken from the profile, with no stand-ins. The Seeker 3 test profiles set 55 / 15 / 10, so these numbers don't change. A profile that leaves Fiber cut distance or Fiber restart length at 0 now stops the slice; before, 58 / 55 were used silently.

**Fibre-owned composite roads** (composite-only, the C++ writer):

- **Routes:** the new planner lays wall loops and spirals plus linked ribs. Route count, lengths and positions differ (coupon table in PR #25).
- **Plastic under the fibre:** plastic infill is no longer printed under the fibre bead. Before, they overlapped. A route the writer skips (one that would leave the part, logged as `TINMANX1_SKIPPED_UNSAFE_FIBER_ROUTE`) keeps its plastic.
- **`;CUT DISTANCE` line:** 54.8 → 58. The cut position (L − 58) and `M1001 L` are unchanged.
- **Prime at each route start:** V0 → Fiber start length × P (15 mm × P, V0.589 for the test strand); replaced again by the Rocket-parity changes below.
- **Slow start:** none → the first Fiber slow length (10 mm) of each route at Fiber start speed (5 mm/s), split mid-move where needed so it is exactly that length. Before, the whole route ran at print speed (30 mm/s).
- **After the cut:** the tail runs at Fiber finish max speed, 3 → 15 mm/s. Its matrix V is multiplied by the after-cut multiplier, 1.0 → 0.72.
- **Finish ironing:** skipped on every route → 8 mm of V-only ironing at 10 mm/s (the mean of finish max and min speed). It runs back along the route's end, or over a loop's start.
- **Restart `U`:** unchanged (55, from the profile). A restart length of 0 now stops the slice; before, 60 was used.
- **Comments only:** route comments name the real warnings instead of `native_route_warnings`, and `connector_threshold` is gone.

**Both modes, Rocket parity.** Later changes bring both writers to Rocket's machine protocol, measured on Rocket's own G-code:

- **Cut G-code:** written exactly as the printer profile gives it at every cut, after `; Start to cut`. FibreSeek's own cut code ends in `;CUT DISTANCE 54.8`, a literal of that code, so a profile with it prints that line; Tinman adds no marker of its own and no longer warns.
- **Cut position:** L − 58 → L − (58 − Extended fiber contact radius), L − 56.2 with the Seeker 3 profile's 1.8 mm. The cut strand ends that radius short of the route's end, under the nozzle, as Rocket's does. `M1001 L` is unchanged: ceil(L + Fiber cut distance).
- **Standby temperatures:** each idle nozzle waits at its own material's Composite standby temperature (Rocket: 120 °C for PETG, 150 °C for CFC PETG). A material that leaves it at 0 keeps 150 °C plastic and 180 °C composite. The generated Seeker 3 test profiles set 150 and 180, so enter 120 and 150 to match Rocket.
- **Tool changes:** both lift Z 5 mm above the layer before the brush station (FibreSeek's Z lift on tool change). Composite-only mode had no lift.
- **Prime at each route start:** the composite material's retraction plus Extra length on restart, at its retraction speed, then `G4 P0`: the tool-change values after a tool change, the travel values between routes. FibreSeek's preset gives 5 − 1 = V4 and 1 − 1 = V0 at 10 mm/s. The generated test profiles set no tool-change retraction, so Tinman's default of 10 mm applies (V10, then `V-9` below) until the composite profile sets 5 and −1. After each cut the travel retraction is taken back (`V-1`) and topped up to the tool-change retraction before the change to plastic (`V-4`, was `V-14`). The default mode's prime line used V15 at F720 and `G4 P1000`, composite-only mode Fiber start length × P.
- **Composite-only retract lines:** they now put F last (`G1 V-1 F600`). With F first, the G-code export dropped `G1 F600` from a line whose feedrate repeated the previous one, leaving ` V-1 ; Retract` or ` V-14 ; Retract`, which the printer skips. That hit most cuts and every change to plastic.
- **P (plastic per mm of fibre):** π(d/2)² × feed factor → Composite plastic multiplier × (Fiber line width × layer height − π(d/2)²) / (π(D/2)²), as Rocket computes it, with D the composite's Composite plastic diameter (Filament diameter when 0). With 0.8 on 0.2 mm layers: 0.03927 → 0.03689; on Rocket's 0.24 mm layers it is 0.0475 (Rocket writes `P0.048`). The prime line keeps the old rule for now.
- **Prime line:** composite-only mode now lays the same prime line as the default mode. In both, the line moves from Y 119 to 2 mm in front of the first layer, and is reached at the tool-change height before coming down.

## What to report back

- The three fibre values you entered (diameter, linear density, composite plastic multiplier).

For each print:

- Pass or fail for each check above, with notes. Include the layer number of
  anything that went wrong.
- Photos: top, bottom, one side, and close-ups of route starts, route ends and
  cuts.
- The project (`.3mf`) and the G-code file you sent, or its name on the
  printer.
- The Tinman log lines starting with `FibreSeek:` for the send. They give the
  file's SHA-256, whether it had fibre, and why a send stopped. The logs are
  in the `log` folder of Tinman's configuration folder (Help > Show
  Configuration Folder).
- Anything shown on the printer's screen. Say whether the printer did
  anything the preview did not show, or started a job without being asked.
- The part's measured size and weight.
