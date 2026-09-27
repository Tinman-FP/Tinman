# Tinman

Tinman is a profile-neutral desktop slicer derived from TinManX1 and OrcaSlicer
2.4.2. It is intended for people who want Tinman's application improvements
without William Tinney's lab printer, filament, or process profiles.

## Tinman vs. TinManX1

| Tinman | TinManX1 |
| --- | --- |
| Ships only the standard public vendor catalog | Includes Tinman-FP lab and FibreSeek profiles |
| Starts with a separate user-data directory | Uses the TinManX1 lab data directory |
| Users select or create their own printers | Maintained around William's tested machines |
| Repository: Tinman-FP/Tinman | Repository: Tinman-FP/TinManX1 |

No Codex or TinManX1 profile bundle is packaged in Tinman. The distribution
check fails if either bundle is accidentally reintroduced.

## Add A Printer

1. Open **Prepare**.
2. Open the printer preset menu.
3. Choose **Select/Remove printers (system presets)**.
4. Select a manufacturer and printer model, then finish the assistant.

Choose **Create printer** from the same menu for a machine that is not in the
public catalog. Tinman restores OrcaSlicer's native printer assistant for these
user-invoked commands instead of routing them through the Bambu-only guide.

See [Getting Started](docs/GETTING_STARTED.md) for installation and first-run
details.

## Build

Tinman uses the OrcaSlicer build system. On macOS, after dependencies are
available:

    cmake -S . -B build/arm64 -G "Ninja Multi-Config" \
      -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=11.3 \
      -DCMAKE_PREFIX_PATH=/path/to/OrcaSlicer_dep/usr/local
    cmake --build build/arm64 --config Release --target Tinman -j 8
    python3 scripts/package_tinman_macos.py

Run the distribution guard before publishing:

    python3 checks/verify_tinman_distribution.py

## Data Separation

Tinman uses its own application identity and user-data directory:

- macOS: ~/Library/Application Support/Tinman
- Windows: the Tinman folder under the user's roaming application data
- Linux: the Tinman folder under XDG_CONFIG_HOME, or ~/.config/Tinman

It does not import TinManX1 or OrcaSlicer settings automatically.

## Upstream And License

Tinman is based on [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer), with
work inherited from Bambu Studio, PrusaSlicer, and Slic3r. The project remains
AGPL-3.0-or-later. See [ATTRIBUTION.md](ATTRIBUTION.md) for the full credit
ledger.
