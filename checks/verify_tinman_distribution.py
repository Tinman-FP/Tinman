#!/usr/bin/env python3
"""Verify the profile-neutral Tinman distribution contract."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROFILES = ROOT / "resources" / "profiles"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"Tinman distribution check failed: {message}")


def main() -> int:
    version = (ROOT / "version.inc").read_text(encoding="utf-8")
    require('set(SLIC3R_APP_NAME "Tinman")' in version, "application name is not Tinman")

    forbidden_profiles = [
        PROFILES / "Codex",
        PROFILES / "Codex.json",
        PROFILES / "TinManX1",
        PROFILES / "TinManX1.json",
    ]
    require(
        not any(path.exists() for path in forbidden_profiles),
        "private Codex or TinManX1 profile bundles are present",
    )

    private_profile_pattern = re.compile(r"tinman|codex", re.IGNORECASE)
    for path in PROFILES.rglob("*"):
        relative_path = path.relative_to(PROFILES).as_posix()
        require(
            private_profile_pattern.search(relative_path) is None,
            f"private profile name is present: {relative_path}",
        )
        if path.is_file() and path.suffix.lower() in {".json", ".info"}:
            require(
                private_profile_pattern.search(path.read_text(encoding="utf-8", errors="ignore")) is None,
                f"private profile content is present: {relative_path}",
            )

    for vendor in ("BBL", "Creality", "Custom", "Prusa", "Qidi"):
        require((PROFILES / vendor).is_dir(), f"standard vendor directory is missing: {vendor}")
        require((PROFILES / f"{vendor}.json").is_file(), f"standard vendor index is missing: {vendor}")

    plist_template = (ROOT / "src/dev-utils/platform/osx/Info.plist.in").read_text(encoding="utf-8")
    for expected in (
        "<string>Tinman</string>",
        "<string>com.tinmanfp.Tinman</string>",
        "<string>images/Tinman.icns</string>",
    ):
        require(expected in plist_template, f"macOS identity is missing {expected}")

    gui_app = (ROOT / "src/slic3r/GUI/GUI_App.cpp").read_text(encoding="utf-8")
    run_wizard_body = re.search(
        r"bool GUI_App::run_wizard\([\s\S]+?\n}\n\nvoid GUI_App::show_desktop_integration_dialog",
        gui_app,
    )
    require(run_wizard_body is not None, "could not locate GUI_App::run_wizard")
    require(
        "GuideFrame wizard(this, pStyle);" in run_wizard_body.group(0)
        and "wizard.SetStartPage(page);" in run_wizard_body.group(0)
        and "wizard.run()" in run_wizard_body.group(0),
        "setup and Add Printer do not route to the visual Orca guide",
    )
    require(
        "ConfigWizard::SP_WELCOME ? GuideFrame::BBL_WELCOME" in run_wizard_body.group(0)
        and "ConfigWizard::SP_FILAMENTS ? GuideFrame::BBL_FILAMENT_ONLY" in run_wizard_body.group(0)
        and "ConfigWizard::SP_PRINTERS ? GuideFrame::BBL_MODELS_ONLY" in run_wizard_body.group(0),
        "visual setup guide does not preserve the requested start page",
    )
    require("SetAppName(SLIC3R_APP_NAME);" in gui_app, "Tinman does not use an independent data directory")

    web_guide = (ROOT / "src/slic3r/GUI/WebGuideDialog.cpp").read_text(encoding="utf-8")
    for required in (
        'strCmd == "save_userguide_models"',
        'm_appconfig_new.set_variant(vendor_name, model_name, nozzle, "true")',
        "preset_bundle->apply_vendor_config(",
        "preferred_model",
        "preferred_variant",
    ):
        require(required in web_guide, f"visual printer setup is missing persistence behavior: {required}")

    main_frame = (ROOT / "src/slic3r/GUI/MainFrame.cpp").read_text(encoding="utf-8")
    require(
        main_frame.count("if (wxGetApp().is_closing() || m_printer_view == nullptr)") >= 2
        and re.search(
            r"void MainFrame::load_printer_url\(wxString url, wxString apikey\)\s*\{\s*"
            r"if \(wxGetApp\(\)\.is_closing\(\)\)",
            main_frame,
        ) is not None,
        "queued printer web-view events are not guarded during shutdown",
    )

    app_config = (ROOT / "src/libslic3r/AppConfig.cpp").read_text(encoding="utf-8")
    preset_bundle = (ROOT / "src/libslic3r/PresetBundle.cpp").read_text(encoding="utf-8")
    require(
        "tinmanx_apply_machine_catalog(*this)" not in app_config
        and "tinmanx_apply_machine_catalog(config)" not in preset_bundle,
        "fresh configurations are still seeded with TinManX1's private machine catalog",
    )

    guide_text = (ROOT / "resources/web/data/text.js").read_text(encoding="utf-8")
    require("Welcome to Tinman" in guide_text, "first-run guide is not branded Tinman")
    require("TinManX1" not in guide_text, "first-run guide still contains TinManX1 branding")

    required_assets = (
        "Tinman.icns",
        "Tinman.ico",
        "Tinman_1024.png",
        "Tinman_192px.png",
        "Tinman_192px_transparent.png",
        "TinmanTitle.ico",
    )
    for asset in required_assets:
        require((ROOT / "resources/images" / asset).is_file(), f"branding asset is missing: {asset}")

    print("Tinman distribution check passed: identity, profile boundary, printer wizard, and assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
