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
- Broader calibration resizing/interpolation changes and precise-seam modifiers
  affect more of the slicing pipeline and need a separate calibration matrix.
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
