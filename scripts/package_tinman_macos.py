#!/usr/bin/env python3
"""Create a self-contained, ad-hoc-signed Tinman macOS preview archive."""

from __future__ import annotations

import argparse
import plistlib
import re
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CANDIDATES = (
    ROOT / "build/arm64/src/Tinman.app",
    ROOT / "build/arm64/src/Release/Tinman.app",
    ROOT / "build/arm64/src/Release/OrcaSlicer.app",
    ROOT / "build/arm64/OrcaSlicer/Tinman.app",
    ROOT / "build/arm64/OrcaSlicer/OrcaSlicer.app",
)


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def first_built_app() -> Path:
    for candidate in DEFAULT_CANDIDATES:
        if candidate.exists():
            return candidate
    joined = "\n".join(f"  {path}" for path in DEFAULT_CANDIDATES)
    raise SystemExit(f"Tinman build was not found. Checked:\n{joined}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-app", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--no-archive", action="store_true", help="Stage an app for DMG packaging")
    parser.add_argument("--skip-codesign", action="store_true", help="The caller will sign the staged app")
    args = parser.parse_args()

    source = args.source_app or first_built_app()
    settings = dict(re.findall(r'^set\((\w+) "([^"\n]+)"\)', (ROOT / "version.inc").read_text(), re.M))
    with (source / "Contents/Info.plist").open("rb") as stream:
        source_info = plistlib.load(stream)
    for field, setting in (("CFBundleShortVersionString", "TINMAN_MACOS_VERSION"),
                           ("CFBundleVersion", "TINMAN_MACOS_BUILD")):
        if source_info.get(field) != settings[setting]:
            raise SystemExit(f"Stale build: {field} is {source_info.get(field)!r}; rebuild for {settings[setting]}")
    target = args.output_dir / "Tinman.app"
    archive = args.output_dir / "Tinman-macOS-arm64.zip"

    if target.exists():
        shutil.rmtree(target)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, target, symlinks=False)

    macos = target / "Contents/MacOS"
    executable = macos / "Tinman"
    if not executable.exists():
        upstream = macos / "OrcaSlicer"
        if not upstream.exists():
            raise SystemExit(f"application executable not found under {macos}")
        upstream.rename(executable)

    info_path = target / "Contents/Info.plist"
    with info_path.open("rb") as stream:
        info = plistlib.load(stream)
    info.update(
        {
            "CFBundleName": "Tinman",
            "CFBundleDisplayName": "Tinman",
            "CFBundleExecutable": "Tinman",
            "CFBundleIdentifier": "com.tinmanfp.Tinman",
        }
    )
    with info_path.open("wb") as stream:
        plistlib.dump(info, stream, sort_keys=False)

    run("xattr", "-cr", str(target))
    if not args.skip_codesign:
        run("codesign", "--force", "--deep", "--sign", "-", str(target))
        run("codesign", "--verify", "--deep", "--strict", str(target))
    if args.no_archive:
        print(target)
        return 0
    if archive.exists():
        archive.unlink()
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(target), str(archive))
    print(archive)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
