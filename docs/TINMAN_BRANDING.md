# Tinman Product Identity

Tinman has its own calendar-based release version. The upstream engine's version
is a compatibility identifier, not the version of the Tinman application.

## Version Sources

`version.inc` supplies the product display version, four-component package version,
release revision, and macOS bundle versions. `TinManBuildInfo.hpp` exposes the
product identity to UI, diagnostics, CLI help, HTTP clients and exported comments.
Artwork must not contain frozen release numbers; native UI renders the version.

macOS uses three-component bundle versions. Windows file resources and installers
use the four-component package version. Flatpak AppStream metadata is generated
from the same settings. The Mac packager rejects stale bundle metadata before
copying or signing an application.

## Intentional Upstream Names

- `SoftFever_VERSION` remains 2.4.2: the actual compatibility baseline, not a claim
  that Tinman is an unmodified Orca release or a complete newer upstream rebase.
- 3MF metadata, profile schema versions, G-code placeholders and upstream update
  queries retain that compatibility version.
- G-code has a real Tinman generator/version line plus a separate OrcaSlicer
  compatibility line for older printer metadata parsers. Do not put Tinman's
  calendar version on that compatibility line.
- OrcaFilamentLibrary vendor IDs, registered OAuth client IDs, credential-store
  names, translation catalogs, legacy migration paths and protocol keys retain
  their established spelling. Renaming these requires a separate migration or
  service registration, not a branding replacement.
- Source target names, dependency paths, upstream issue references, historic
  documentation, license notices and contributor credits remain truthful.
- Upstream release alerts explicitly identify themselves as upstream alerts;
  they do not imply that an Orca download is a Tinman update.

## Distribution Boundary

Public packaging uses Tinman app names, icons, launchers and repository links.
It must not invoke the private TinManX1 installation helper or include personal
printer profiles. Local personal resource overrides belong only in the owner's
installed app and backup, never in release artifacts or source control.

## Verification

Run `python3 checks/test_tinman_branding.py`,
`python3 checks/test_tinman_build_info.py`, and
`python3 checks/verify_tinman_distribution.py`.
The FFF export regression test also checks the real Tinman generator and the
unchanged Orca compatibility generator for both BBL and non-BBL output.

Verify splash, About, CLI help and installed bundle metadata after packaging.
Windows and Linux runtime validation still requires their respective platforms;
passing the source-contract checks is not a substitute for those builds.
