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
- Plastic nozzle diameter 0.4 and composite nozzle diameter 0.7.
- Fiber enabled turned on, with your fibre cut G-code and cut distance.
- Material 1: an ordinary PCTG profile. Material 2: a composite profile whose
  filament type is `CFC`, for example CFC PCTG + X-CCF.
- In that composite profile, Fiber diameter and Fiber linear density must be
  filled in from your fibre's datasheet. Tinman has no built-in values for
  them: a fibre slice without a diameter stops with a message saying so.

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
- Fiber manufacturing mode: **Plastic + fiber overlay** (the default). Leave
  "Fiber-owned composite roads" alone for now: its route planner is being
  replaced.
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

## What to report back

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
