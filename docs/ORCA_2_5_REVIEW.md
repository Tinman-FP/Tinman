# OrcaSlicer 2.5.0 Alpha Review

Reviewed October 17, 2026. Upstream release:
[OrcaSlicer v2.5.0-alpha](https://github.com/OrcaSlicer/OrcaSlicer/releases/tag/v2.5.0-alpha),
published October 16. This is a prerelease, not a stable 2.5 release.

The official macOS universal DMG was downloaded and its SHA-256 checked against
the GitHub release asset digest:
`db253104d2dff8d284f3bfbfeb8878f09b78cb480d8e6037439e2f796bd3667f`.

## Selected Fixes

These patches are tested first on an isolated TinManX1 branch, then promoted to
Tinman. Cherry-pick trailers preserve upstream authorship and commit IDs.

| Upstream change | Why it belongs in Tinman | Scope |
| --- | --- | --- |
| [#14473](https://github.com/OrcaSlicer/OrcaSlicer/pull/14473) | PA-pattern calibration could leave a large absolute extrusion coordinate and cause excessive subsequent retraction. | Reset E after the final pattern layer; retain the regression test. |
| [#15543](https://github.com/OrcaSlicer/OrcaSlicer/pull/15543) | Scarf seams can interfere with a retraction calibration. | Disable scarf seams on the calibration object, not in the user's saved profile. |
| [#14546](https://github.com/OrcaSlicer/OrcaSlicer/pull/14546) | Calibration generators could access a missing model after import cancellation or failure. | Return model-load success and stop the affected tower generators when loading fails. |
| [#16191](https://github.com/OrcaSlicer/OrcaSlicer/pull/16191) | Bambu LAN connection help could crash before the printer type was known. | Use a bundled fallback help image and tolerate empty icon names. |
| [#15832](https://github.com/OrcaSlicer/OrcaSlicer/pull/15832) | Very short scarf-seam segments can provoke motion-planner slowdowns. | Remove tiny seam-adjacent segments and extend the ramp slightly when necessary; retain geometry regression tests. |

The scarf patch only affects scarf-seam toolpaths. It does not automatically
enable scarf seams or change normal-seam defaults. It is relevant to seam-quality
investigation, but is not proof that a particular photographed print defect is
resolved. A physical comparison print remains necessary.

No printer or filament preset catalog changes, project-schema migrations, user
credentials, or machine firmware changes are included in this backport.

## Deferred

- The Python plugin runtime/cloud hub, Design tools, belt printing and parallel
  printing are larger experimental additions requiring their own compatibility
  review.
- New Bambu networking architecture and mixed-nozzle support need device-level
  validation; the existing Tinman H2D/X1C onboarding fixes are retained.
- Precise-seam modifiers require a separate project-format and calibration
  matrix. The selected resizing/interpolation changes are covered by the
  October 19 stage-1 work below, not the original maintenance batch.
- Catalog-wide filament ID changes are not imported into an existing profile
  ecosystem as part of a small maintenance release.

Tinman retains its 2.4.2 upstream base identity. This is a selective maintenance
update, not a claim of complete feature parity with OrcaSlicer 2.5.0 Alpha.

## Verification

Build with `BUILD_TESTS=ON`. Run the calibration and extrusion-entity tests plus
the broader libslic3r/FFF suites. Compile the modified GUI units. Before public
distribution, build the public application, run the distribution/build-identity
checks, and smoke-test the packaged application with disposable user data.

The public release notes record the actual completed checks and any limitations.
No printer operation is required or performed by these software tests.

## Approved Follow-up: October 19, 2026

The user approved the staged follow-up recommendations. Work is isolated on
`codex/orca-250-calibration-stage1` in private TinManX1. This is a development
candidate, not an installed update or a public release. The earlier sections
describe the original maintenance batch; they do not imply that this new batch
has already shipped. No live printer, filament, or process presets are changed.

### Stage 1: Calibration and Isolated Reliability Fixes

Imported with upstream attribution:

| Change | Upstream | Local compatibility work |
| --- | --- | --- |
| Adaptive PA measurement validation | [#14198](https://github.com/OrcaSlicer/OrcaSlicer/pull/14198) | Parse complete finite numbers; validate each filament separately; reject malformed enabled models during print validation. Do not compare PA, flow, and acceleration numerically across different units. |
| Prusa Adaptive PA warning and clearer measurement labels | [#14271](https://github.com/OrcaSlicer/OrcaSlicer/pull/14271), [#14411](https://github.com/OrcaSlicer/OrcaSlicer/pull/14411) | Retain existing configuration types and preset storage. |
| PA-line footprint and preview | [#14440](https://github.com/OrcaSlicer/OrcaSlicer/pull/14440) | Account for optional labels and extrusion width; do not clip long-pattern bounds to a 25 mm inset; preview height tags describe thickness, not absolute Z. |
| Nozzle-aware VFA, flow-limit adjustment, PA-tower interpolation, optional temperature-tower scaling | [#14759](https://github.com/OrcaSlicer/OrcaSlicer/pull/14759) | Keep Tinman's scalar speed/temperature config types. Bound VFA speed steps, reject impossible flow targets, retain selected filament volumetric limits, and keep nozzle/layer limits. |
| Human-readable Moonraker upload errors | [#14841](https://github.com/OrcaSlicer/OrcaSlicer/pull/14841) | Port only error formatting and its tests, not plugin test dependencies or authentication changes. |

The VFA adjustment selects a compatible process layer height, then proposes a
lower end speed if necessary. A reduced endpoint stays on the requested speed
grid and must not exceed the calculated flow limit. If even the first two blocks
cannot be reached, generation stops. Users can still explicitly opt into a
warned, flow-clamped test when automatic adjustment is disabled; such a test
must not be treated as evidence that its requested speeds were reached.

Physical calibration results remain machine/nozzle/material-specific. Software
regression tests do not promote a filament or process preset to TUNED.

### Next Batches and Release Gates

| Track | Decision | Required before promotion |
| --- | --- | --- |
| Precise Seam | Evaluate separately; not imported in stage 1. Initial [#12974](https://github.com/OrcaSlicer/OrcaSlicer/pull/12974) spans 47 files, including model/project-format code; follow-up [#16072](https://github.com/OrcaSlicer/OrcaSlicer/pull/16072) and [#16170](https://github.com/OrcaSlicer/OrcaSlicer/pull/16170) also need review. | 3MF round trips, unchanged toolpaths without modifiers, Classic/Arachne and seam-paint/scarf combinations, representative physical comparison. |
| Speed Dial | Evaluate a dependency-separated implementation. [#14697](https://github.com/OrcaSlicer/OrcaSlicer/pull/14697) initially dispatches plugin actions; [#15562](https://github.com/OrcaSlicer/OrcaSlicer/pull/15562) is not a small standalone port. Do not import the Python runtime just for this UI. | Built-in action/settings registry, focus and keyboard behavior, localization, no plugin/cloud dependency, desktop smoke tests. |
| H2D mixed-nozzle support | Separate compatibility track, not a blind catalog/network upgrade. | Equal and mixed diameters, left/right selection, standard/high-flow variants, AMS/manual mapping, LAN/developer mode, old profile/project round trips, generated G-code audit and device validation. Retain Tinman's H2D/X1C onboarding fixes. |
| macOS shutdown/startup fixes | Review separately against Tinman's camera and shutdown work. The upstream Dock-quit patch depends on the newer plugin manager. | Reproduce the relevant fault; repeated launch/quit and print-host UI smoke tests with disposable data. |
| Plugin/cloud runtime, Design, texture/color mixing, belt/parallel printing, broad catalog migrations | Deferred. | Dedicated design, compatibility, security and migration review; stable upstream evidence. |

Stage 1 also needs packaged-application smoke tests (VFA resize on/off, reachable
and impossible flow ranges, PA line with/without numbers, PA/temperature towers),
representative exported-G-code checks, profile/project round trips, and supported
platform builds before public promotion. H2D mixed-nozzle qualification is not
covered by this single-nozzle calibration batch. No printer commands are issued
by this work.

### Private Stage 1 Verification Results (Historical)

On this Apple Silicon Mac, the Release build completed for `libslic3r_tests`,
`fff_print_tests`, and `slic3rutils_tests`, including the GUI library. Existing
compiler/dependency warnings remain; this was not a warning-free build.

| Suite | Result |
| --- | --- |
| Focused calibration | 9 cases, 1,993 assertions passed |
| Full libslic3r (includes the focused calibration cases) | 304 cases, 264,400 assertions passed |
| Full FFF printing | 50 cases, 903 assertions passed |
| Full slic3r utilities, including upload-error parsing | 34 cases, 288 assertions passed |
| Whitespace/error-marker check | `git diff --check` passed |

Compact test outputs are retained in [audit/orca250-stage1](audit/orca250-stage1).
New regression coverage includes 25-layer VFA bands, nonzero-origin speed grids,
impossible flow targets, nozzle sizes 0.2-1.0 mm, malformed/nonfinite PA input,
print validation with and without a warning sink, optional label footprints,
and PA preview thickness with nonzero Z offset. The existing absolute-extrusion
reset regression is retained.

These results describe the private development build, not the public package.
Do not distribute that private build. Public promotion preserves intervening
Tinman fixes and uses the Preview 5 identity; its independent checks follow.

### Public Preview 5 Validation (In Progress)

The selected commits were applied onto public Tinman `90e25afac2f7` on
`codex/calibration-preview5-release`. The candidate is **not yet promoted** to
the public download. The local Release build completed with existing compiler
warnings. Standard CI exclusions (`~[NotWorking]`) were used for the suites.

| Check | Observed result |
| --- | --- |
| libslic3r | 305 cases, 264,405 assertions passed |
| FFF printing | 48 cases, 889 assertions passed |
| Utilities | 31 cases, 285 assertions passed |
| Actual tower G-code export (included in FFF) | 1 case, 40 assertions; PA, temperature and VFA at 0.4/0.6 mm |
| Branding/build identity Python tests | 15 passed |
| Windows build contract tests | 3 passed |
| Public distribution checks | Passed: branding, wizard assets and private-profile boundary |
| Fresh isolated macOS setup | Qidi Q2 0.4 mm added and available for calibration |
| VFA resizing on/off | Generated, sliced and exported; resizing off retained selected 0.16 mm layer height |
| VFA impossible range | Rejected 10,000-11,000 mm/s range |
| VFA bounded adjustment | Proposed 0.12 mm layers and 490 mm/s grid endpoint; filament MVS remained 24.5 mm3/s |
| PA line with numbers | Generated and sliced preview; final native export check remains pending |
| Reopen/project check | Model and presets retained; calibration schedule was not restored (see below) |

Cross-platform build/test run:
[39310400517](https://github.com/Tinman-FP/Tinman/actions/runs/39310400517).
This run supersedes the canceled first run: Windows ARM required native CMake
4.3.4 for the runner's Visual Studio 2026 generator. Build scripts now stop on
failed commands rather than continuing to package stale output.

**Calibration project limitation:** an ordinary saved VFA project reopened
successfully, but its re-sliced G-code lost the runtime speed sweep. Preserve
audited final G-code and generator inputs; regenerate calibrations when changing
or repeating them. A model/preset round trip is not a calibration-schedule round
trip. This work does not introduce a calibration project-schema migration.

Remaining gates: complete the packaged PA-line numbers-off and PA/temperature
tower interactive matrix, final exported-output audit, clean release packaging,
and platform CI results. Native UI automation became unavailable during the
macOS save-panel test; that check is not counted as passed. No interactive
Windows/Linux or physical printer qualification is claimed. No live user
profiles or installed application were changed.
