#!/usr/bin/env python3
"""Generate FibreSeek Seeker 3 system presets.

Two outputs share one set of values:
- the TinManX1 pack (the default), for the private TinManX1 distribution, whose
  curated "TinMan Codex" machines live in nested folders the generator keeps;
- with --vendor fibreseek, the neutral FibreSeek vendor bundle Tinman ships, which
  names nothing after Tinman and is written the way orca_profile_tool.py normalizes.

Values cited "FibreSeek preset" come from the preset tables shipped with Rocket
Slicer 1.4.0 (PresetVersion 16); the others are Tinman estimates from generic
Orca material baselines. The Rocket Compare process lanes (--rocket-compare)
exist only to compare emitted G-code against Rocket Slicer output. Plastic
profiles are written separately from plastic-plus-continuous-fiber profiles so
the UI can group the process lanes cleanly.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import functools
import importlib.util
import json
from pathlib import Path
import sys


def find_repo_root(start: Path) -> Path:
    for candidate in (start.parent, *start.parents):
        if (candidate / "README.md").is_file() and (candidate / "scripts").is_dir():
            return candidate
    return start.parents[1]


ROOT = find_repo_root(Path(__file__).resolve())
HELPER_DIR = Path(__file__).resolve().parent
PROFILE_ROOT = ROOT / "resources" / "profiles"
PACK_VERSION = "02.04.00.18"
MACHINE_MODEL = "FibreSeek Seeker 3"


@dataclass(frozen=True)
class Bundle:
    vendor: str  # index and directory name, filament_vendor, default manufacturer
    prefix: str  # plastic filament and filament base names
    common: str  # machine and process common names
    source_id: str  # prefix of the fiber_source_material_id values
    # The TinManX1 distribution's conventions: its own setting and filament ids, filaments for
    # its curated "- TinMan Codex" machines, legacy machine names and the machine contract key.
    # The neutral bundle uses Orca's minted ids and only machines it contains.
    private: bool


TINMANX1 = Bundle("TinManX1", "TinManX1", "TinManX1 FibreSeek", "tinmanx1", private=True)
FIBRESEEK = Bundle("FibreSeek", "FibreSeek", "FibreSeek", "fibreseek", private=False)
BUNDLES = {"tinmanx1": TINMANX1, "fibreseek": FIBRESEEK}
# The bundle being written; build_pack sets it.
BUNDLE = TINMANX1
LEGACY_COMPOSITE_MACHINE_NAMES = ["FibreSeek Seeker 3 - Codex"]


# Keys cited "FibreSeek preset" below come from Rocket Slicer 1.4.0's preset tables
# (PresetVersion 16): temp_wait, retraction_* and retract_* from Plastics.json; fiber_temp,
# fiber_temp_first, fiber_temp_preheat, fiber_temp_standby (TemperatureWait) and fiber_fan
# (RegularFanSpeed) from the matching CFC material in Composites.json. CFC PA is mapped to
# the PA-CF entry, Tinman's only PA matrix. Every other value is a Tinman estimate.
MATERIALS = {
    "PLA": {
        "color": "#F2F2F2",
        "fiber_color": "#F67C01",
        "manufacturer": "FIBRESEEK",
        "fiber_plastic_name": "CFC PLA",
        "fiber_plastic_density": 1.25,
        "density": 1.25,
        "cost": 20,
        "temp": 220,
        "temp_first": 225,
        "bed": 55,
        "bed_first": 60,
        "chamber": 0,
        "temp_wait": 100,  # FibreSeek preset
        "fan": 80,
        "fan_max": 100,
        "flow": 1.00,
        "mvs": 16,
        "retraction_length": 1.0,  # FibreSeek preset, through retract_restart_extra_toolchange
        "retraction_speed": 25,
        "retract_length_toolchange": 5,
        "retract_restart_extra_toolchange": -1,
        "fiber_temp": 230,  # FibreSeek preset, through fiber_temp_standby and fiber_fan
        "fiber_temp_first": 230,
        "fiber_temp_preheat": 220,
        "fiber_temp_standby": 100,
        "fiber_fan": 100,
        "fiber_flow": 0.82,
        "fiber_mvs": 12,
        "fiber_plastic_extrusion_speed": 10,
        "fiber_extrusion_speed": 25,
    },
    "ABS": {
        "color": "#8A8A8A",
        "fiber_color": "#2F2F2F",
        "density": 1.04,
        "cost": 22,
        "temp": 250,
        "temp_first": 255,
        "bed": 100,
        "bed_first": 105,
        "chamber": 50,
        "fan": 10,
        "fan_max": 20,
        "flow": 1.00,
        "mvs": 15,
        "fiber_temp": 260,
        "fiber_temp_first": 265,
        "fiber_fan": 15,
        "fiber_flow": 0.85,
        "fiber_mvs": 12,
    },
    "ASA": {
        "color": "#E5A04C",
        "fiber_color": "#3B3B3B",
        "density": 1.07,
        "cost": 27,
        "temp": 260,
        "temp_first": 265,
        "bed": 105,
        "bed_first": 110,
        "chamber": 55,
        "fan": 10,
        "fan_max": 25,
        "flow": 1.00,
        "mvs": 15,
        "fiber_temp": 270,
        "fiber_temp_first": 275,
        "fiber_fan": 15,
        "fiber_flow": 0.85,
        "fiber_mvs": 12,
    },
    "PPA-CF": {
        "color": "#2E2E2E",
        "fiber_color": "#1F1F1F",
        "density": 1.25,
        "cost": 145,
        "temp": 295,
        "temp_first": 300,
        "bed": 110,
        "bed_first": 115,
        "chamber": 60,
        "fan": 10,
        "fan_max": 25,
        "flow": 0.96,
        "mvs": 8,
        "fiber_temp": 300,
        "fiber_temp_first": 305,
        "fiber_fan": 15,
        "fiber_flow": 0.82,
        "fiber_mvs": 8,
    },
    "PPS-CF": {
        "color": "#262626",
        "fiber_color": "#151515",
        "density": 1.26,
        "cost": 140,
        "temp": 330,
        "temp_first": 335,
        "bed": 110,
        "bed_first": 115,
        "chamber": 60,
        "temp_wait": 150,  # FibreSeek preset
        "fan": 0,
        "fan_max": 15,
        "flow": 0.96,
        "mvs": 4,
        "fiber_temp": 335,
        "fiber_temp_first": 340,
        "fiber_fan": 10,
        "fiber_flow": 0.82,
        "fiber_mvs": 6,
    },
    "ABS-CF": {
        "color": "#363636",
        "fiber_color": "#181818",
        "density": 1.12,
        "cost": 42,
        "price_source": "Generic 1 kg retail average",
        "temp": 255,
        "temp_first": 260,
        "bed": 100,
        "bed_first": 105,
        "chamber": 50,
        "fan": 5,
        "fan_max": 15,
        "flow": 0.98,
        "mvs": 10,
        "fiber_temp": 265,
        "fiber_temp_first": 270,
        "fiber_fan": 10,
        "fiber_flow": 0.82,
        "fiber_mvs": 8,
    },
    "ABS-GF": {
        "color": "#777777",
        "fiber_color": "#3A3A3A",
        "density": 1.08,
        "cost": 45,
        "price_source": "Generic 1 kg retail average",
        "temp": 260,
        "temp_first": 265,
        "bed": 105,
        "bed_first": 110,
        "chamber": 50,
        "fan": 10,
        "fan_max": 20,
        "flow": 0.95,
        "mvs": 8,
        "fiber_temp": 270,
        "fiber_temp_first": 275,
        "fiber_fan": 15,
        "fiber_flow": 0.82,
        "fiber_mvs": 8,
    },
    "ASA-GF": {
        "color": "#9B9B8B",
        "fiber_color": "#444438",
        "density": 1.15,
        "cost": 50,
        "price_source": "Generic 1 kg retail average",
        "temp": 260,
        "temp_first": 265,
        "bed": 105,
        "bed_first": 110,
        "chamber": 65,
        "fan": 10,
        "fan_max": 20,
        "flow": 0.95,
        "mvs": 8,
        "fiber_temp": 270,
        "fiber_temp_first": 275,
        "fiber_fan": 15,
        "fiber_flow": 0.82,
        "fiber_mvs": 8,
    },
    "PP": {
        "color": "#F2F2F2",
        "fiber_color": "#555555",
        "density": 0.90,
        "cost": 35,
        "temp": 225,
        "temp_first": 230,
        "bed": 70,
        "bed_first": 70,
        "chamber": 35,
        "fan": 0,
        "fan_max": 10,
        "flow": 1.00,
        "mvs": 10,
        "fiber_temp": 235,
        "fiber_temp_first": 240,
        "fiber_fan": 5,
        "fiber_flow": 0.85,
        "fiber_mvs": 10,
    },
    "PETG": {
        "color": "#FCD02E",
        "fiber_color": "#F67C01",
        "manufacturer": "FIBRESEEK",
        "fiber_plastic_name": "CFC PETG",
        "fiber_plastic_density": 1.27,
        "density": 1.27,
        "cost": 20,
        "temp": 250,
        "temp_first": 250,
        "bed": 75,
        "bed_first": 75,
        "chamber": 0,
        "temp_wait": 120,  # FibreSeek preset
        "fan": 50,
        "fan_max": 90,
        "flow": 1.00,
        "mvs": 15,
        "retraction_length": 1.0,  # FibreSeek preset, through retract_restart_extra_toolchange
        "retraction_speed": 20,
        "retract_length_toolchange": 5,
        "retract_restart_extra_toolchange": -1,
        "retraction_minimum_travel": 5,
        "z_hop": 0.4,
        "fiber_temp": 270,  # FibreSeek preset, through fiber_temp_standby and fiber_fan
        "fiber_temp_first": 270,
        "fiber_temp_standby": 150,
        "fiber_fan": 80,
        "fiber_flow": 0.80,
        "fiber_mvs": 10,
        "fiber_z_hop": 1.0,
        "fiber_plastic_extrusion_speed": 10,
        "fiber_extrusion_speed": 25,
        "fiber_overrides": {
            "CGF": {
                "fiber_fan": 100,  # FibreSeek preset (CFC PETG + CGF)
                "fiber_flow": 0.68,
            },
        },
    },
    "PET GF": {
        "color": "#FFF06292",
        "fiber_color": "#F67C01",
        "density": 1.43,
        "cost": 30,
        "price_source": "Generic 1 kg retail average",
        "temp": 300,
        "temp_first": 300,
        "bed": 75,
        "bed_first": 75,
        "chamber": 25,
        "temp_wait": 150,  # FibreSeek preset
        "fan": 40,
        "fan_max": 60,
        "flow": 1.00,
        "mvs": 15,
        "retraction_length": 0.5,
        "retraction_speed": 20,
        "retraction_minimum_travel": 0,
        "z_hop": 0.4,
        "fiber_temp": 270,
        "fiber_temp_first": 270,
        "fiber_fan": 80,
        "fiber_flow": 0.80,
        "fiber_mvs": 10,
        "fiber_z_hop": 1.0,
        "fiber_overrides": {
            "CGF": {
                "fiber_fan": 100,
                "fiber_flow": 0.68,
            },
        },
    },
    "PCTG": {
        "color": "#64B5F6",
        "fiber_color": "#203040",
        "density": 1.27,
        "cost": 30,
        "price_source": "Generic 1 kg retail average",
        "temp": 260,
        "temp_first": 265,
        "bed": 80,
        "bed_first": 80,
        "chamber": 45,
        "fan": 20,
        "fan_max": 35,
        "flow": 0.97,
        "mvs": 12,
        "fiber_temp": 270,
        "fiber_temp_first": 275,
        "fiber_fan": 25,
        "fiber_flow": 0.85,
        "fiber_mvs": 12,
    },
    "PCTG-CF": {
        "color": "#2B3A42",
        "fiber_color": "#101820",
        "density": 1.18,
        "cost": 54.95,
        "price_source": "Generic 1 kg retail average",
        "temp": 260,
        "temp_first": 265,
        "bed": 80,
        "bed_first": 80,
        "chamber": 45,
        "fan": 5,
        "fan_max": 15,
        "flow": 0.98,
        "mvs": 8,
        "fiber_temp": 270,
        "fiber_temp_first": 275,
        "fiber_fan": 10,
        "fiber_flow": 0.82,
        "fiber_mvs": 8,
    },
    "Push Plastic PC-PBT": {
        "color": "#1F1F1F",
        "fiber_color": "#111111",
        "manufacturer": "Push Plastic",
        "fiber_plastic_name": "Push Plastic PC-PBT",
        "density": 1.20,
        "cost": 39.99,
        "temp": 255,
        "temp_first": 255,
        "temp_range_low": 245,
        "temp_range_high": 260,
        "bed": 110,
        "bed_first": 110,
        "chamber": 55,
        "fan": 10,
        "fan_max": 25,
        "flow": 1.00,
        "mvs": 6,
        "fiber_temp": 260,
        "fiber_temp_first": 260,
        "fiber_fan": 15,
        "fiber_flow": 0.82,
        "fiber_mvs": 6,
    },
    "Push Plastic PC-PBT-CF": {
        "color": "#161616",
        "fiber_color": "#0D0D0D",
        "manufacturer": "Push Plastic",
        "fiber_plastic_name": "Push Plastic PC-PBT-CF",
        "density": 1.20,
        "cost": 99.98,
        "price_source": "Push Plastic Carbon Fiber PC+PBT 500 g spool, normalized to 1 kg",
        "price_source_url": "https://www.pushplastic.com/products/carbon-fiber-pc-pbt-filament-1-75mm-500g",
        "temp": 255,
        "temp_first": 260,
        "temp_range_low": 250,
        "temp_range_high": 260,
        "bed": 100,
        "bed_first": 100,
        "chamber": 55,
        "fan": 10,
        "fan_max": 25,
        "flow": 0.98,
        "mvs": 6,
        "fiber_temp": 260,
        "fiber_temp_first": 260,
        "fiber_fan": 15,
        "fiber_flow": 0.82,
        "fiber_mvs": 6,
        "required_nozzle_hrc": 40,
    },
    "PA-CF": {
        "color": "#2D2D2D",
        "fiber_color": "#111111",
        "density": 1.17,
        "cost": 80,
        "temp": 300,
        "temp_first": 305,
        "bed": 80,
        "bed_first": 85,
        "chamber": 50,
        "temp_wait": 150,  # FibreSeek preset
        "fan": 20,
        "fan_max": 35,
        "flow": 0.95,
        "mvs": 8,
        "fiber_temp": 270,  # FibreSeek preset (CFC PA), through fiber_fan
        "fiber_temp_first": 270,
        "fiber_temp_standby": 200,
        "fiber_fan": 50,
        "fiber_flow": 0.82,
        "fiber_mvs": 8,
    },
}


PLASTIC_NOZZLES = {
    "0.4": {"layer": "0.20", "line_width": "0.42", "max_layer": "0.30", "min_layer": "0.08"},
    "0.6": {"layer": "0.20", "line_width": "0.62", "max_layer": "0.42", "min_layer": "0.12"},
    "0.8": {"layer": "0.32", "line_width": "0.82", "max_layer": "0.56", "min_layer": "0.16"},
}
COMPOSITE_NOZZLE = "0.7"
COMPOSITE_LINE_WIDTH = "0.80"
CANONICAL_NOZZLES = ("0.4", "0.6", "0.8", "1.0")
FIBRESEEK_BED_VOLUME = {
    "printable_area": ["0x0", "305x0", "305x305", "0x305"],
    "printable_height": "245",
}
ROCKET_COMPARE_SOLID_PAYLOAD = json.dumps(
    {
        "angle_list_raw": "0/90/0",
        "extrusion_width_mm": 0.7,
        "extend_into_perimeters_mm": 0.0,
        "min_segment_length_mm": 10.0,
        "coalesce_transition_length_mm": 12.0,
        "route_stitch_transition_length_mm": 16.0,
        "source": "Rocket Slicer 1.3.1.480 Fortified PETG + X-CCF session",
    },
    separators=(",", ":"),
)


# Fibre route physics, the same in all three modes: FibreSeek preset (Rocket Slicer 1.4.0,
# PresetVersion 16), Reinforced profile "CFC PETG+X-CCF/PETG" at 0.24 mm macro layers.
FIBER_ROUTE_PHYSICS = {
    "fiber_start_length": "6",
    "fiber_slow_length": "5",
    "fiber_start_max_speed": "4",
    "fiber_start_min_speed": "3",
    "fiber_start_min_limit_speed": "2",
    "fiber_normal_max_speed": "30",
    "fiber_normal_min_speed": "5",
    "fiber_normal_min_limit_speed": "2",
    "fiber_finish_max_speed": "10",
    "fiber_finish_min_speed": "3",
    "fiber_finish_min_limit_speed": "2",
    "fiber_max_arc_segment_length": "7",
    "fiber_min_radius": "12",
    "fiber_tension_length": "2",
    "fiber_tension_feedrate": "50",
    "fiber_after_cut_plastic_extrusion_multiplier": "0.70",
}

# Each mode's structure: spacing, layer step, route caps, line width and loops. These are
# Tinman estimates; how FibreSeek's profiles map onto them is not known yet.
FIBER_MODES = {
    "Light": {
        "mode": "light",
        "spacing": "2.40",
        "layer_step": "3",
        "max_routes": "2",
        "routes_per_cut": "1",
        "infill_source": "plastic_traces",
        "generate_perimeters": "1",
        "generate_infill": "1",
        "fiber_line_width": "0.68",
        "fiber_outer_perimeter_loops": "1",
        "fiber_inner_perimeter_loops": "1",
        "fiber_plastic_outer_loops_with_fiber": "2",
        "fiber_plastic_inner_loops_with_fiber": "0",
    },
    "Medium": {
        "mode": "medium",
        "spacing": "1.60",
        "layer_step": "2",
        "max_routes": "6",
        "routes_per_cut": "1",
        "infill_source": "plastic_traces",
        "generate_perimeters": "1",
        "generate_infill": "1",
        "fiber_line_width": "0.80",
        "fiber_outer_perimeter_loops": "2",
        "fiber_inner_perimeter_loops": "2",
        "fiber_plastic_outer_loops_with_fiber": "0",
        "fiber_plastic_inner_loops_with_fiber": "0",
    },
    "Heavy": {
        "mode": "heavy",
        "spacing": "0.80",
        "layer_step": "1",
        "max_routes": "10",
        "routes_per_cut": "1",
        "infill_source": "generated_ribs",
        "generate_perimeters": "1",
        "generate_infill": "1",
        "fiber_line_width": "0.70",
        "fiber_outer_perimeter_loops": "2",
        "fiber_inner_perimeter_loops": "2",
        "fiber_plastic_outer_loops_with_fiber": "0",
        "fiber_plastic_inner_loops_with_fiber": "0",
    },
}

ROCKET_COMPARE_MODE_OVERRIDES = {
    "Light": {
        # Rocket Speedy emits plastic only for this part/profile family.
        "fiber_generate_perimeters": "0",
        "fiber_generate_infill": "0",
        "fiber_infill_pattern": "isogrid",
        "fiber_max_routes_per_layer": "32",
    },
    "Medium": {
        "fiber_infill_pattern": "isogrid",
        "fiber_infill_density": "20",
        "fiber_infill_angles": "0,60,120",
        "fiber_infill_spacing": "0",
        "fiber_max_routes_per_layer": "80",
    },
    "Heavy": {
        "fiber_infill_pattern": "solid",
        "fiber_infill_density": "0",
        "fiber_infill_angles": "0,90,0",
        "fiber_max_routes_per_layer": "180",
    },
}


# plastic_multiplier is fiber_composite_plastic_multiplier (FibreSeek's ExtrusionMultiplierP).
# A manufacturer of None is the bundle's vendor.
CONTINUOUS_FIBERS = [
    {
        "label": "X-CCF Carbon fiber 0.25 mm",
        "profile_suffix": "X-CCF",
        "type": "carbon_fiber",
        "manufacturer": "fibreseek",
        "source_id": "x-ccf-025",
        "color": "#111111",
        # FibreSeek preset (Rocket Slicer 1.4.0, PresetVersion 16): CFC PETG + X-CCF.
        "diameter": 0.25,
        "linear_density": 102.0,
        "plastic_multiplier": 0.8,
        "cost": 180,
        "confidence": "Fibre data from FibreSeek's CFC PETG + X-CCF preset",
    },
    {
        "label": "CGF Glass fiber 0.35 mm",
        "profile_suffix": "CGF",
        "type": "glass_fiber",
        "manufacturer": "fibreseek",
        "source_id": "cgf-035",
        "color": "#E8F4FF",
        # FibreSeek preset (Rocket Slicer 1.4.0, PresetVersion 16): CFC PETG + CGF.
        "diameter": 0.35,
        "linear_density": 170.0,
        "plastic_multiplier": 0.68,
        "cost": 45,
        "confidence": "Fibre data from FibreSeek's CFC PETG + CGF preset",
    },
    # No FibreSeek data for CKF and CBF: estimates, with X-CCF's plastic multiplier.
    {
        "label": "CKF Kevlar fiber 0.25 mm",
        "profile_suffix": "CKF",
        "type": "aramid_fiber",
        "manufacturer": None,
        "source_id": "ckf-025",
        "color": "#D8A300",
        "diameter": 0.25,
        "linear_density": 72.0,
        "plastic_multiplier": 0.8,
        "cost": 160,
        "confidence": "Estimated fibre data",
    },
    {
        "label": "CBF Basalt fiber 0.25 mm",
        "profile_suffix": "CBF",
        "type": "basalt_fiber",
        "manufacturer": None,
        "source_id": "cbf-025",
        "color": "#3F3530",
        "diameter": 0.25,
        "linear_density": 95.0,
        "plastic_multiplier": 0.8,
        "cost": 150,
        "confidence": "Estimated fibre data",
    },
]

# FibreSeek's Reinforced profile start G-code, which Rocket writes at the head of every print
# between these two comments (FibreSeek preset, Rocket Slicer 1.4.0, PresetVersion 16).
# extruder1 is the plastic (T1) extruder. Its smoothing time and pressure advance depend on the
# plastic (0.02 / 0.07 for PETG), so only a PETG plastic in material 1 gets them. Orca's own
# filament pressure advance cannot be used: with this Marlin flavor it writes M900 for the
# active tool, and it has no smoothing time.
FIBRESEEK_PROFILE_START_GCODE = (
    "; Start profile start gcode\n"
    '{if filament_type[0] == "PETG"}SET_PRESSURE_ADVANCE EXTRUDER=extruder1 SMOOTH_TIME=0.02\n'
    "SET_PRESSURE_ADVANCE EXTRUDER=extruder1 ADVANCE=0.07\n"
    "{endif}SET_VELOCITY_LIMIT MINIMUM_CRUISE_RATIO=0.8\n"
    "SET_TOOL_CORNER_VELOCITY T=0 SCV=1\n"
    "SET_TOOL_CORNER_VELOCITY T=1 SCV=5\n"
    "; End profile start gcode\n"
)


def arr(value):
    return [str(value)]


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Unable to load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


@functools.lru_cache(maxsize=None)
def profile_tool():
    return load_module(ROOT / "scripts" / "orca_profile_tool.py", "orca_profile_tool")


def write_json(path: Path, data: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    if BUNDLE.private:
        path.write_text(json.dumps(data, indent=2, ensure_ascii=True) + "\n")
    else:
        # The neutral bundle is written as orca_profile_tool.py normalize writes it.
        tool = profile_tool()
        tool.write_profile_json(path, tool.create_ordered_profile(data, tool.NORMALIZE_FIELD_ORDER))


def setting_id(sub: str, name: str, private_id: str) -> str:
    return private_id if BUNDLE.private else profile_tool().generate_preset_setting_id(BUNDLE.vendor, sub, name)


def filament_id(name: str, filament_type: str, private_id: str) -> str:
    if BUNDLE.private:
        return private_id
    tool = profile_tool()
    return tool.generate_filament_id(BUNDLE.vendor, filament_type, tool.base_name(name))


def load_layup_payload_helper():
    return load_module(HELPER_DIR / "build_tinmanx1_fiber_layup_payload.py", "tinmanx1_layup_payload_helper")


def layup_payload_for_template(template: str) -> str:
    # No per-material route limits are embedded here; the planner falls back to
    # the process and filament settings. Material-specific limits belong in
    # values measured on FibreSeek hardware, not in another slicer's presets.
    payload: dict[str, object] = {}
    if template == "none":
        return json.dumps(payload, separators=(",", ":"), sort_keys=True)
    helper = load_layup_payload_helper()
    payload["layup_bands"] = [helper.normalize_band(dict(item)) for item in helper.TEMPLATES[template]]
    helper.assert_planner_accepts(payload)
    return helper.compact_json(payload)


def material_slug(material: str) -> str:
    return material.replace("/", "-").replace(" ", "-")


def plastic_machine_name(nozzle: str) -> str:
    return f"{MACHINE_MODEL} {nozzle} nozzle"


def composite_machine_name(plastic_nozzle: str) -> str:
    return f"{MACHINE_MODEL} {plastic_nozzle}+{COMPOSITE_NOZZLE} composite nozzle"


def plastic_filament_name(material: str) -> str:
    return f"{BUNDLE.prefix} {material} @FibreSeek Seeker 3"


def filament_base_name(material: str) -> str:
    return f"{BUNDLE.prefix} {material} base"


def machine_common_name() -> str:
    return f"{BUNDLE.common} machine common"


def process_common_name() -> str:
    return f"{BUNDLE.common} process common"


def composite_filament_name(material: str, fiber: dict | None = None) -> str:
    fiber = fiber or CONTINUOUS_FIBERS[0]
    return f"CFC {material} + {fiber['profile_suffix']} @FibreSeek Seeker 3"


def compatible_filament_printers(composite: bool) -> list[str]:
    if BUNDLE.private:
        # The TinManX1 distribution's curated four-nozzle machines.
        return [f"{MACHINE_MODEL} {nozzle} nozzle - TinMan Codex" for nozzle in CANONICAL_NOZZLES]
    plastic = [] if composite else [plastic_machine_name(n) for n in PLASTIC_NOZZLES]
    return plastic + [composite_machine_name(n) for n in PLASTIC_NOZZLES]


def compatible_process_printers(plastic_nozzle: str, fiber: bool) -> list[str]:
    names = [composite_machine_name(plastic_nozzle)] if fiber else [
        plastic_machine_name(plastic_nozzle),
        composite_machine_name(plastic_nozzle),
    ]
    if plastic_nozzle == "0.4" and BUNDLE.private:
        names.extend(LEGACY_COMPOSITE_MACHINE_NAMES)
    return names


def fiber_value(values: dict, fiber: dict, key: str):
    overrides = values.get("fiber_overrides", {}).get(fiber["profile_suffix"], {})
    return overrides.get(key, values[key])


def with_optional_filament_retraction(profile: dict, values: dict, prefix: str = "") -> dict:
    fields = {
        "retraction_length": "filament_retraction_length",
        "retraction_speed": "filament_retraction_speed",
        "retract_length_toolchange": "filament_retract_length_toolchange",
        "retract_restart_extra_toolchange": "filament_retract_restart_extra_toolchange",
        "retraction_minimum_travel": "filament_retraction_minimum_travel",
        "z_hop": "filament_z_hop",
    }
    for source, target in fields.items():
        key = f"{prefix}{source}"
        if key in values:
            profile[target] = arr(values[key])
    return profile


def filament_base(material: str, values: dict) -> dict:
    chamber_temperature = int(values["chamber"])
    profile = {
        "type": "filament",
        "name": filament_base_name(material),
        "from": "system",
        "instantiation": "false",
        "filament_vendor": arr(BUNDLE.vendor),
        "filament_type": arr(material),
        "default_filament_colour": arr(values["color"]),
        "filament_diameter": arr("1.75"),
        "filament_density": arr(values["density"]),
        "filament_cost": arr(values["cost"]),
        **({"filament_price_source": arr(values["price_source"])} if "price_source" in values else {}),
        **(
            {"filament_price_source_url": arr(values["price_source_url"])}
            if "price_source_url" in values
            else {}
        ),
        "filament_flow_ratio": arr(f"{values['flow']:.2f}"),
        "filament_max_volumetric_speed": arr(values["mvs"]),
        "filament_is_support": arr("0"),
        "filament_soluble": arr("0"),
        "nozzle_temperature": arr(values["temp"]),
        "nozzle_temperature_initial_layer": arr(values["temp_first"]),
        "nozzle_temperature_range_low": arr(values.get("temp_range_low", max(180, values["temp"] - 20))),
        "nozzle_temperature_range_high": arr(values.get("temp_range_high", values["temp_first"] + 20)),
        "hot_plate_temp": arr(values["bed"]),
        "hot_plate_temp_initial_layer": arr(values["bed_first"]),
        "cool_plate_temp": arr(values["bed"]),
        "cool_plate_temp_initial_layer": arr(values["bed_first"]),
        "eng_plate_temp": arr(values["bed"]),
        "eng_plate_temp_initial_layer": arr(values["bed_first"]),
        "textured_plate_temp": arr(values["bed"]),
        "textured_plate_temp_initial_layer": arr(values["bed_first"]),
        "chamber_temperature": arr(chamber_temperature),
        "activate_chamber_temp_control": arr("1" if chamber_temperature else "0"),
        "fan_min_speed": arr(values["fan"]),
        "fan_max_speed": arr(values["fan_max"]),
        "slow_down_for_layer_cooling": arr("1" if values["fan"] else "0"),
        "enable_pressure_advance": arr("1"),
        "pressure_advance": arr("0.020"),
        "required_nozzle_HRC": arr(
            values.get("required_nozzle_hrc", 3 if ("CF" in material or "GF" in material) else 0)
        ),
        "plastic_spool_weight": arr("1000"),
        "composite_enabled": arr("0"),
        "fiber_name": arr(""),
        "fiber_type": arr(""),
        "fiber_manufacturer": arr(""),
        "fiber_diameter": arr("0"),
        "fiber_linear_density": arr("0"),
        "fiber_spool_length_km": arr("0"),
        "fiber_cost": arr("0"),
        "fiber_plastic_name": arr(""),
        "fiber_plastic_type": arr(""),
        "fiber_plastic_manufacturer": arr(""),
        "fiber_plastic_diameter": arr("0"),
        "fiber_plastic_density": arr("0"),
        "fiber_plastic_cost": arr("0"),
        "fiber_plastic_spool_weight": arr("0"),
        "fiber_nozzle_temperature_preheat": arr("0"),
        "fiber_nozzle_temperature_standby": arr(values.get("temp_wait", 150)),
        "fiber_first_layers_height": arr("0"),
        "fiber_plastic_extrusion_speed": arr("0"),
        "fiber_extrusion_speed": arr("0"),
        "fiber_restart_pause": arr("0"),
        "fiber_finish_ironing_distance": arr("0"),
        "fiber_priming_line_height": arr("0"),
        "fiber_material_kind": arr("plastic"),
        "fiber_source_material_id": arr(f"{BUNDLE.source_id}-{material_slug(material).lower()}"),
    }
    return with_optional_filament_retraction(profile, values)


def plastic_filament(material: str) -> dict:
    values = MATERIALS[material]
    chamber_temperature = int(values["chamber"])
    return {
        "type": "filament",
        "name": plastic_filament_name(material),
        "inherits": filament_base_name(material),
        "from": "system",
        "setting_id": setting_id("filament", plastic_filament_name(material), f"TMX-FS3-{material_slug(material)}-P"),
        "filament_id": filament_id(
            plastic_filament_name(material), material, f"tinmanx1-fs3-{material_slug(material).lower()}-plastic"
        ),
        "instantiation": "true",
        "filament_vendor": arr(BUNDLE.vendor),
        "filament_type": arr(material),
        "default_filament_colour": arr(values["color"]),
        "filament_diameter": arr("1.75"),
        "filament_density": arr(values["density"]),
        "filament_cost": arr(values["cost"]),
        **({"filament_price_source": arr(values["price_source"])} if "price_source" in values else {}),
        **(
            {"filament_price_source_url": arr(values["price_source_url"])}
            if "price_source_url" in values
            else {}
        ),
        "filament_flow_ratio": arr(f"{values['flow']:.2f}"),
        "filament_max_volumetric_speed": arr(values["mvs"]),
        "nozzle_temperature": arr(values["temp"]),
        "nozzle_temperature_initial_layer": arr(values["temp_first"]),
        "nozzle_temperature_range_low": arr(values.get("temp_range_low", max(180, values["temp"] - 20))),
        "nozzle_temperature_range_high": arr(values.get("temp_range_high", values["temp_first"] + 20)),
        "hot_plate_temp": arr(values["bed"]),
        "hot_plate_temp_initial_layer": arr(values["bed_first"]),
        "cool_plate_temp": arr(values["bed"]),
        "cool_plate_temp_initial_layer": arr(values["bed_first"]),
        "eng_plate_temp": arr(values["bed"]),
        "eng_plate_temp_initial_layer": arr(values["bed_first"]),
        "textured_plate_temp": arr(values["bed"]),
        "textured_plate_temp_initial_layer": arr(values["bed_first"]),
        "chamber_temperature": arr(chamber_temperature),
        "activate_chamber_temp_control": arr("1" if chamber_temperature else "0"),
        "fan_min_speed": arr(values["fan"]),
        "fan_max_speed": arr(values["fan_max"]),
        "slow_down_for_layer_cooling": arr("1" if values["fan"] else "0"),
        "enable_pressure_advance": arr("1"),
        "pressure_advance": arr("0.020"),
        "composite_enabled": arr("0"),
        "compatible_printers": compatible_filament_printers(composite=False),
    }


def composite_filament(material: str, values: dict, fiber: dict) -> dict:
    name = composite_filament_name(material, fiber)
    suffix = fiber["profile_suffix"]
    baseline_note = (
        "Matrix values without a FibreSeek preset are estimates for coupon tuning; "
        "check the fibre's diameter and linear density against its datasheet."
    )
    profile = {
        "type": "filament",
        "name": name,
        "inherits": filament_base_name(material),
        "from": "system",
        "setting_id": setting_id("filament", name, f"TMX-FS3-{material_slug(material)}-{suffix.upper()}"),
        "filament_id": filament_id(name, "CFC", f"tinmanx1-fs3-{material_slug(material).lower()}-{suffix.lower()}"),
        "instantiation": "true",
        # The add-filament catalog reads these fields before inheritance is applied.
        "filament_vendor": arr(BUNDLE.vendor),
        "filament_type": arr("CFC"),
        "description": f"{name}. {fiber['confidence']}. {baseline_note}",
        "default_filament_colour": arr(fiber["color"]),
        "filament_diameter": arr("1.75"),
        "filament_density": arr(values.get("fiber_plastic_density", values["density"])),
        "filament_cost": arr(values.get("fiber_plastic_cost", values["cost"])),
        **({"filament_price_source": arr(values["price_source"])} if "price_source" in values else {}),
        **(
            {"filament_price_source_url": arr(values["price_source_url"])}
            if "price_source_url" in values
            else {}
        ),
        "filament_flow_ratio": arr(f"{fiber_value(values, fiber, 'fiber_flow'):.2f}"),
        "filament_max_volumetric_speed": arr(fiber_value(values, fiber, "fiber_mvs")),
        "nozzle_temperature": arr(fiber_value(values, fiber, "fiber_temp")),
        "nozzle_temperature_initial_layer": arr(fiber_value(values, fiber, "fiber_temp_first")),
        "nozzle_temperature_range_low": arr(max(180, fiber_value(values, fiber, "fiber_temp") - 20)),
        "nozzle_temperature_range_high": arr(fiber_value(values, fiber, "fiber_temp_first") + 20),
        "hot_plate_temp": arr(values["bed"]),
        "hot_plate_temp_initial_layer": arr(values["bed_first"]),
        "cool_plate_temp": arr(values["bed"]),
        "cool_plate_temp_initial_layer": arr(values["bed_first"]),
        "eng_plate_temp": arr(values["bed"]),
        "eng_plate_temp_initial_layer": arr(values["bed_first"]),
        "textured_plate_temp": arr(values["bed"]),
        "textured_plate_temp_initial_layer": arr(values["bed_first"]),
        "chamber_temperature": arr(int(values["chamber"])),
        "activate_chamber_temp_control": arr("1" if int(values["chamber"]) else "0"),
        "fan_min_speed": arr(fiber_value(values, fiber, "fiber_fan")),
        "fan_max_speed": arr(max(fiber_value(values, fiber, "fiber_fan"), values["fan_max"])),
        "enable_pressure_advance": arr("1"),
        "pressure_advance": arr("0.020"),
        "composite_enabled": arr("1"),
        "fiber_name": arr(fiber["label"]),
        "fiber_type": arr(fiber["type"]),
        "fiber_manufacturer": arr(fiber["manufacturer"] or BUNDLE.vendor),
        "fiber_diameter": arr(fiber["diameter"]),
        "fiber_linear_density": arr(fiber["linear_density"]),  # g/km
        "fiber_feed_factor": arr("0"),  # legacy model, unused while the multiplier below is set
        "fiber_composite_plastic_multiplier": arr(fiber["plastic_multiplier"]),
        "fiber_spool_length_km": arr("0.5"),
        "fiber_cost": arr(fiber["cost"]),
        "fiber_plastic_name": arr(values.get("fiber_plastic_name", material)),
        "fiber_plastic_type": arr(material),
        "fiber_plastic_manufacturer": arr(values.get("manufacturer", BUNDLE.vendor)),
        "fiber_plastic_diameter": arr("1.75"),
        "fiber_plastic_density": arr(values.get("fiber_plastic_density", values["density"])),
        "fiber_plastic_cost": arr(values.get("fiber_plastic_cost", values["cost"])),
        "fiber_plastic_spool_weight": arr("1000"),
        "fiber_nozzle_temperature_preheat": arr(values.get("fiber_temp_preheat", fiber_value(values, fiber, "fiber_temp"))),
        "fiber_nozzle_temperature_standby": arr(values.get("fiber_temp_standby", 150)),
        "fiber_first_layers_height": arr("0.25"),
        "fiber_plastic_extrusion_speed": arr(values.get("fiber_plastic_extrusion_speed", 8)),
        "fiber_extrusion_speed": arr(values.get("fiber_extrusion_speed", 25)),
        "fiber_restart_pause": arr("0"),
        # FibreSeek preset (Rocket Slicer 1.4.0, PresetVersion 16), the same for every CFC material.
        "fiber_finish_ironing_distance": arr("8"),
        "fiber_priming_line_height": arr("0.2"),
        "fiber_material_kind": arr("composite_matrix"),
        "fiber_source_material_id": arr(f"{BUNDLE.source_id}-{material_slug(material).lower()}-{suffix.lower()}"),
        "compatible_printers": compatible_filament_printers(composite=True),
        # The composite nozzle's matrix retraction, which both fibre writers read: FibreSeek preset
        # (Rocket Slicer 1.4.0, PresetVersion 16), the same for every CFC material. Its travel
        # restart extra (ExtraLengthP) is -1, so it primes 1 - 1 = V0 between routes, as Rocket
        # 1.4.0's G-code does (Rocket 1.3.1's primed 1 + 0 = V1); after a tool change 5 - 1 = V4.
        "filament_retraction_length": arr("1"),
        "filament_retract_length_toolchange": arr("5"),
        "filament_retract_restart_extra": arr("-1"),
        "filament_retract_restart_extra_toolchange": arr("-1"),
        "filament_retraction_speed": arr("10"),
    }
    return with_optional_filament_retraction(profile, values, prefix="fiber_")


def composite_machine_variant(plastic_nozzle: str) -> str:
    # Orca's validator wants a multi-nozzle variant to name every nozzle; the TinManX1 machines
    # keep the plastic nozzle, which their curated machine names are built from.
    return plastic_nozzle if BUNDLE.private else f"{plastic_nozzle}+{COMPOSITE_NOZZLE}"


def machine_variants() -> list[str]:
    return [*PLASTIC_NOZZLES, *(composite_machine_variant(n) for n in PLASTIC_NOZZLES)]


def machine_model() -> dict:
    return {
        "type": "machine_model",
        "name": MACHINE_MODEL,
        "model_id": "TinManX1-FibreSeek-Seeker-3" if BUNDLE.private else "FibreSeek-Seeker-3",
        "nozzle_diameter": ";".join(CANONICAL_NOZZLES if BUNDLE.private else machine_variants()),
        "machine_tech": "FFF",
        "family": BUNDLE.vendor,
        "default_materials": ";".join(
            [
                plastic_filament_name("PCTG"),
                composite_filament_name("PCTG"),
                plastic_filament_name("ABS"),
                composite_filament_name("ABS"),
                plastic_filament_name("PA-CF"),
                composite_filament_name("PA-CF"),
            ]
        ),
    }


def machine_common() -> dict:
    return {
        "type": "machine",
        "name": machine_common_name(),
        "from": "system",
        "instantiation": "false",
        "printer_model": MACHINE_MODEL,
        "gcode_flavor": "marlin",
        "single_extruder_multi_material": "0",
        **FIBRESEEK_BED_VOLUME,
        "machine_max_speed_x": arr("500"),
        "machine_max_speed_y": arr("500"),
        "machine_max_speed_z": arr("5"),
        "machine_max_acceleration_x": arr("5000"),
        "machine_max_acceleration_y": arr("5000"),
        "machine_max_acceleration_z": arr("100"),
        "machine_max_acceleration_e": arr("8000"),
        "machine_max_acceleration_extruding": arr("5000"),
        "machine_max_acceleration_retracting": arr("8000"),
        "machine_max_acceleration_travel": arr("5000"),
        "machine_max_jerk_x": arr("20"),
        "machine_max_jerk_y": arr("20"),
        "machine_max_jerk_z": arr("10"),
        "machine_max_jerk_e": arr("10"),
        "retraction_length": arr("1.0"),
        "retraction_minimum_travel": arr("3"),
        "retraction_speed": arr("30"),
        "deretraction_speed": arr("30"),
        "z_hop": arr("0.5"),
        "machine_start_gcode": "G92 E0\nG0 Z10 F900\n",
        "machine_end_gcode": "M400\nG90\nG0 X300 Y280 F6000\nG91\nG0 Z10 F900\n",
        "before_layer_change_gcode": ";BEFORE_LAYER_CHANGE\n;[layer_z]\nG92 E0\n",
        "layer_change_gcode": "G92 E0",
        "use_relative_e_distances": "1",
        "support_chamber_temp_control": "1",
        "fiber_enabled": "0",
        "fiber_shared_nozzle": "0",
        "plastic_nozzle_diameter": "0.4",
        "composite_nozzle_diameter": COMPOSITE_NOZZLE,
        "fiber_plastic_extruder_offset_x": "0",
        "fiber_plastic_extruder_offset_y": "0",
        "fiber_plastic_extruder_offset_z": "0",
        "fiber_composite_extruder_offset_x": "0",
        "fiber_composite_extruder_offset_y": "0",
        "fiber_composite_extruder_offset_z": "0",
        "fiber_plastic_extruder_heatup_speed": "5.5",
        "fiber_composite_extruder_heatup_speed": "3.2",
        "fiber_plastic_extruder_has_fan": "1",
        "fiber_composite_extruder_has_fan": "1",
        "fiber_plastic_extruder_fan_index": "1",
        "fiber_composite_extruder_fan_index": "2",
        "fiber_bed_heatup_speed": "0.9",
        "fiber_chamber_heatup_speed": "1.0",
        "fiber_motion_blocks_buffer_size": "16",
        "fiber_cut_distance": "58",
        "fiber_restart_length": "55",
        # FibreSeek's cut G-code, which the writers write verbatim at every cut. Its 54.8 is a
        # literal of that code; the cut position comes from fiber_cut_distance.
        "fiber_cut_gcode": "M2800\nM400\n;CUT DISTANCE 54.8",
        # The composite extruder's own values (FibreSeek preset ExtruderCs.json); the composite
        # machines take the Reinforced profile's.
        "fiber_nozzle_contact_radius": "1.2",
        "fiber_nozzle_contact_radius_extended": "1.8",
        "fiber_toolchange_gcode_before": "G0 Y280 F10000\nM106 P1 S255\nM400\n",
        "fiber_toolchange_gcode_after": (
            "SAVE_NOZZLE_TO_CLEAN\n"
            "T1\n"
            "G0 X304 Y285 F30000\n"
            "G0 Y290 F1200\n"
            "G0 Y335 F600\n"
            "G0 Y320 F10000\n"
            "G0 Y335 F10000\n"
            "G0 Y320 F10000\n"
            "G0 Y335 F10000\n"
            "G4 P1000\n"
            "G0 X289 F600\n"
            "G0 X306 F600\n"
            "G0 X289 F600\n"
            "G0 X306 F600\n"
            "G4 P2000\n"
            "G0 Y285 F1200\n"
            "M400\n"
            "RESTORE_NOZZLE_TO_PRINT\n"
            "M106 P1 S0"
        ),
        "fiber_slot_roles": "plastic,composite",
        "continuous_fiber_name": "X-CCF Carbon fiber 0.25 mm",
        "continuous_fiber_type": "carbon_fiber",
        "continuous_fiber_material_kind": "continuous_fiber",
        "continuous_fiber_source_material_id": f"{BUNDLE.source_id}-{CONTINUOUS_FIBERS[0]['source_id']}",
        "continuous_fiber_diameter": "0",
        "continuous_fiber_linear_density": "0",
        "fiber_postprocessor_type": "3",
        "fiber_machine_contract_payload": json.dumps(
            {
                "machine": MACHINE_MODEL,
                "plastic_extruder": {"heatup_speed_c_per_s": 5.5, "fan_index": 1, "has_fan": True},
                "composite_extruder": {
                    "heatup_speed_c_per_s": 3.2,
                    "fan_index": 2,
                    "has_fan": True,
                    "nozzle_diameter": float(COMPOSITE_NOZZLE),
                    "cut_distance": 58,
                    "restart_length": 55,
                    "contact_radius": 1.2,
                    "contact_radius_extended": 1.8,
                },
                "thermal": {"bed_heatup_speed_c_per_s": 0.9, "chamber_heatup_speed_c_per_s": 1.0},
                "motion": {"blocks_buffer_size": 16, "travel_speed_xy": 500, "travel_speed_z": 5},
            },
            sort_keys=True,
        ),
    }


def plastic_machine(nozzle: str, spec: dict) -> dict:
    return {
        **machine_common(),
        "type": "machine",
        "name": plastic_machine_name(nozzle),
        "inherits": machine_common_name(),
        "from": "system",
        "setting_id": setting_id("machine", plastic_machine_name(nozzle), f"TMX-FS3-M-P{nozzle}"),
        "instantiation": "true",
        "printer_model": MACHINE_MODEL,
        **FIBRESEEK_BED_VOLUME,
        "printer_variant": nozzle,
        "nozzle_diameter": arr(nozzle),
        "max_layer_height": arr(spec["max_layer"]),
        "min_layer_height": arr(spec["min_layer"]),
        "default_print_profile": f"{spec['layer']}mm Plastic Only @FibreSeek Seeker 3 {nozzle} nozzle",
        "default_filament_profile": arr(plastic_filament_name("PCTG")),
        "plastic_nozzle_diameter": nozzle,
        "fiber_enabled": "0",
    }


def composite_machine(plastic_nozzle: str, spec: dict) -> dict:
    return {
        **machine_common(),
        "type": "machine",
        "name": composite_machine_name(plastic_nozzle),
        "inherits": machine_common_name(),
        "from": "system",
        "setting_id": setting_id(
            "machine", composite_machine_name(plastic_nozzle), f"TMX-FS3-M-X{plastic_nozzle}-{COMPOSITE_NOZZLE}"
        ),
        "instantiation": "true",
        "printer_model": MACHINE_MODEL,
        **FIBRESEEK_BED_VOLUME,
        "printer_variant": composite_machine_variant(plastic_nozzle),
        "extruder_colour": ["#D84B4B", "#111111"],
        "extruder_offset": ["0x0", "0x0"],
        "extruder_type": ["Direct Drive", "Direct Drive"],
        "extruder_variant_list": ["Direct Drive Standard", "Direct Drive Standard"],
        "printer_extruder_id": ["1", "2"],
        "printer_extruder_variant": ["Direct Drive Standard", "Direct Drive Standard"],
        "nozzle_diameter": [plastic_nozzle, COMPOSITE_NOZZLE],
        "filament_map": ["1", "2"],
        "max_layer_height": [spec["max_layer"], "0.40"],
        "min_layer_height": [spec["min_layer"], "0.14"],
        "retraction_length": ["1.0", "1.0"],
        "default_print_profile": f"0.20mm Plastic + Continuous Fiber Medium @FibreSeek Seeker 3 {plastic_nozzle}+{COMPOSITE_NOZZLE} nozzle",
        "default_filament_profile": [plastic_filament_name("PCTG"), composite_filament_name("PCTG")],
        "plastic_nozzle_diameter": plastic_nozzle,
        "composite_nozzle_diameter": COMPOSITE_NOZZLE,
        "fiber_enabled": "1",
        "fiber_shared_nozzle": "1",
        "machine_start_gcode": FIBRESEEK_PROFILE_START_GCODE + machine_common()["machine_start_gcode"],
        # FibreSeek preset (Rocket Slicer 1.4.0, PresetVersion 16): the Reinforced PETG profile
        # overrides the composite extruder's 1.2 / 1.8 (machine common) with 0.4 / 0.8. Both of
        # Tinman's fibre modes are plastic plus fibre, like Rocket's Reinforced mode. The extended
        # radius places the cut: 58 - 0.8 = 57.2 mm before the route end.
        "fiber_nozzle_contact_radius": "0.4",
        "fiber_nozzle_contact_radius_extended": "0.8",
    }


def process_common() -> dict:
    return {
        "type": "process",
        "name": process_common_name(),
        "from": "system",
        "instantiation": "false",
        "bottom_surface_pattern": "monotonic",
        "bottom_shell_layers": "3",
        "top_shell_layers": "4",
        "top_shell_thickness": "0.8",
        "bridge_flow": "0.95",
        "brim_width": "5",
        "brim_object_gap": "0.1",
        "print_sequence": "by layer",
        "default_acceleration": "1500",
        "top_surface_acceleration": "1200",
        "travel_acceleration": "3000",
        "inner_wall_acceleration": "1500",
        "outer_wall_acceleration": "1200",
        "wall_infill_order": "inner wall/outer wall/infill",
        "infill_direction": "45",
        "sparse_infill_density": "15%",
        "sparse_infill_pattern": "crosshatch",
        "initial_layer_acceleration": "500",
        "initial_layer_print_height": "0.2",
        "infill_wall_overlap": "20%",
        "ironing_type": "no ironing",
        "reduce_infill_retraction": "1",
        "filename_format": "{input_filename_base}_{nozzle_diameter[0]}n_{layer_height}mm_{filament_type[initial_tool]}_{printer_model}_{print_time}.gcode",
        "detect_overhang_wall": "1",
        "wall_loops": "3",
        "raft_layers": "0",
        "seam_position": "aligned",
        "skirt_distance": "2",
        "skirt_height": "1",
        "skirt_loops": "0",
        "minimum_sparse_infill_area": "15",
        "spiral_mode": "0",
        "enable_support": "0",
        "resolution": "0.012",
        "support_type": "normal(auto)",
        "support_style": "default",
        "support_top_z_distance": "0.2",
        "support_object_xy_distance": "0.35",
        "tree_support_branch_angle": "45",
        "tree_support_wall_count": "0",
        "detect_thin_wall": "0",
        "top_surface_pattern": "monotonicline",
        "initial_layer_speed": "35",
        "initial_layer_infill_speed": "45",
        "outer_wall_speed": "60",
        "inner_wall_speed": "90",
        "internal_solid_infill_speed": "90",
        "top_surface_speed": "55",
        "gap_infill_speed": "90",
        "sparse_infill_speed": "90",
        "travel_speed": "220",
        "travel_speed_z": "5",
        "enable_prime_tower": "0",
        "xy_hole_compensation": "0",
        "xy_contour_compensation": "0",
        "enable_arc_fitting": "0",
        "fiber_manufacturing_mode": "plastic_plus_fiber_overlay",
        "fiber_generate_perimeters": "0",
        "fiber_generate_infill": "0",
        "fiber_reinforcement_mode": "light",
    }


def bridge_line_width_for_nozzle(nozzle: str) -> str:
    return "0.79" if nozzle == "0.8" else nozzle


def bridge_line_width_for_composite_nozzle(plastic_nozzle: str) -> str:
    return "0.69" if float(plastic_nozzle) > float(COMPOSITE_NOZZLE) else plastic_nozzle


def plastic_process(nozzle: str, spec: dict) -> dict:
    layer = spec["layer"]
    width = spec["line_width"]
    return {
        "type": "process",
        "name": f"{layer}mm Plastic Only @FibreSeek Seeker 3 {nozzle} nozzle",
        "inherits": process_common_name(),
        "from": "system",
        "setting_id": setting_id("process", f"{layer}mm Plastic Only @FibreSeek Seeker 3 {nozzle} nozzle", f"TMX-FS3-PROC-P{nozzle}"),
        "instantiation": "true",
        "layer_height": layer,
        "first_layer_height": "0.2",
        "line_width": width,
        "outer_wall_line_width": width,
        "inner_wall_line_width": width,
        "sparse_infill_line_width": width,
        "internal_solid_infill_line_width": width,
        "top_surface_line_width": width,
        "support_line_width": width,
        "bridge_line_width": bridge_line_width_for_nozzle(nozzle),
        "compatible_printers": compatible_process_printers(nozzle, fiber=False),
    }


def fiber_process(plastic_nozzle: str, spec: dict, mode_label: str, mode: dict, layup_payload: str = "{}") -> dict:
    width = spec["line_width"]
    name = f"0.20mm Plastic + Continuous Fiber {mode_label} @FibreSeek Seeker 3 {plastic_nozzle}+{COMPOSITE_NOZZLE} nozzle"
    return {
        "type": "process",
        "name": name,
        "inherits": process_common_name(),
        "from": "system",
        "setting_id": setting_id("process", name, f"TMX-FS3-PROC-X{plastic_nozzle}-{mode_label.upper()}"),
        "instantiation": "true",
        "layer_height": "0.2",
        "first_layer_height": "0.2",
        "line_width": width,
        "outer_wall_line_width": width,
        "inner_wall_line_width": width,
        "sparse_infill_line_width": width,
        "internal_solid_infill_line_width": width,
        "top_surface_line_width": width,
        "support_line_width": width,
        "bridge_line_width": bridge_line_width_for_composite_nozzle(plastic_nozzle),
        "wall_loops": "3",
        "sparse_infill_density": "18%",
        "sparse_infill_pattern": "grid",
        "top_shell_layers": "4",
        "bottom_shell_layers": "3",
        # The default mode, which the first prints use: plastic plus the fibre overlay.
        "fiber_manufacturing_mode": "plastic_plus_fiber_overlay",
        "fiber_generate_perimeters": mode["generate_perimeters"],
        "fiber_generate_infill": mode["generate_infill"],
        "fiber_reinforcement_mode": mode["mode"],
        "fiber_start_layer": "4",
        "fiber_print_order_code": "1",
        **FIBER_ROUTE_PHYSICS,
        "fiber_tension_release_fraction": "0",
        "fiber_feedrate_percent": "100",
        "fiber_override_correction_speed": "0",
        "fiber_correction_move_speed": "2",
        "fiber_correction_move_feedrate_percent": "0",
        "fiber_z_hop_after_cut": "0",
        "fiber_first_layer_flow_ratio": "0",
        "fiber_first_layer_line_width": "0",
        "fiber_first_layer_height": "0",
        "fiber_first_layer_speed_ratio": "0",
        "fiber_infill_pattern": "solid",
        "fiber_infill_density": "0",
        "fiber_infill_angles": "",
        "fiber_infill_source_policy": mode["infill_source"],
        "fiber_seam_position": "source",
        "fiber_seam_angle": "0",
        "fiber_line_width": mode.get("fiber_line_width", COMPOSITE_LINE_WIDTH),
        "fiber_infill_spacing": mode["spacing"],
        "fiber_macro_layer_height": "0.2",
        "fiber_layer_step": mode["layer_step"],
        "fiber_min_route_length": "55",
        "fiber_perimeter_min_route_length": "55",
        "fiber_mechanical_min_route_length": "55",
        "fiber_perimeter_inset": "0.85",
        "fiber_infill_inset": "1.2",
        "fiber_print_speed": FIBER_ROUTE_PHYSICS["fiber_normal_max_speed"],
        "fiber_start_speed": FIBER_ROUTE_PHYSICS["fiber_start_max_speed"],
        "fiber_max_routes_per_layer": mode["max_routes"],
        "fiber_routes_per_cut": mode["routes_per_cut"],
        "fiber_outer_perimeter_loops": mode["fiber_outer_perimeter_loops"],
        "fiber_inner_perimeter_loops": mode["fiber_inner_perimeter_loops"],
        "fiber_plastic_outer_loops_with_fiber": mode["fiber_plastic_outer_loops_with_fiber"],
        "fiber_plastic_inner_loops_with_fiber": mode["fiber_plastic_inner_loops_with_fiber"],
        "fiber_infill_solid_payload": ROCKET_COMPARE_SOLID_PAYLOAD,
        "fiber_reinforcement_payload": layup_payload,
        "compatible_printers": compatible_process_printers(plastic_nozzle, fiber=True),
    }


def rocket_compare_process(plastic_nozzle: str, spec: dict, mode_label: str, mode: dict, layup_payload: str = "{}") -> dict:
    process = fiber_process(plastic_nozzle, spec, mode_label, mode, layup_payload)
    process.update(ROCKET_COMPARE_MODE_OVERRIDES.get(mode_label, {}))
    name = f"0.20mm Rocket Compare Composite Only {mode_label} @FibreSeek Seeker 3 {plastic_nozzle}+{COMPOSITE_NOZZLE} nozzle"
    process.update(
        {
            "name": name,
            "setting_id": setting_id("process", name, f"TMX-FS3-PROC-RC-X{plastic_nozzle}-{mode_label.upper()}"),
            "fiber_manufacturing_mode": "composite_only",
            "fiber_infill_solid_payload": ROCKET_COMPARE_SOLID_PAYLOAD,
            "notes": (
                "Rocket comparison profile. These values intentionally track the Rocket/FibreSeek mode mapping "
                "for G-code comparison. The composite nozzle still deposits polymer matrix with continuous fiber."
            ),
        }
    )
    return process


def build_pack(layup_template: str = "none", bundle: Bundle = TINMANX1, rocket_compare: bool = False):
    global BUNDLE
    BUNDLE = bundle
    pack_root = PROFILE_ROOT / bundle.vendor
    index_path = PROFILE_ROOT / f"{bundle.vendor}.json"
    layup_payload = layup_payload_for_template(layup_template)
    preserved_entries: dict[str, list[dict]] = {}
    machine_contract_version = "3"
    if index_path.is_file() and bundle.private:
        existing_index = json.loads(index_path.read_text())
        machine_contract_version = existing_index.get(
            "tinman_codex_machine_contract", machine_contract_version
        )
        for list_key in ("machine_model_list", "machine_list", "process_list", "filament_list"):
            preserved_entries[list_key] = [
                item
                for item in existing_index.get(list_key, [])
                if len(Path(item.get("sub_path", "")).parts) > 2
            ]

    # Rebuild only generator-owned root files. Curated TinMan Codex profiles
    # live in nested directories and must survive every generator run.
    for section in ("machine", "process", "filament"):
        section_root = pack_root / section
        if section_root.is_dir():
            for path in section_root.glob("*.json"):
                path.unlink()

    top = {
        "name": bundle.vendor,
        "version": PACK_VERSION,
        "force_update": "0",
        "description": f"{bundle.common} Seeker 3 plastic and continuous-fiber profiles",
        "machine_model_list": [
            {"name": MACHINE_MODEL, "sub_path": f"machine/{MACHINE_MODEL}.json"}
        ],
        "process_list": [
            {"name": process_common_name(), "sub_path": f"process/{process_common_name()}.json"}
        ],
        "filament_list": [],
        "machine_list": [
            {"name": machine_common_name(), "sub_path": f"machine/{machine_common_name()}.json"}
        ],
        **({"tinman_codex_machine_contract": machine_contract_version} if bundle.private else {}),
    }

    def add(list_key: str, profile: dict):
        sub_path = f"{list_key.removesuffix('_list')}/{profile['name']}.json"
        top[list_key].append({"name": profile["name"], "sub_path": sub_path})
        write_json(pack_root / sub_path, profile)

    write_json(pack_root / "machine" / f"{MACHINE_MODEL}.json", machine_model())
    write_json(pack_root / "machine" / f"{machine_common_name()}.json", machine_common())
    write_json(pack_root / "process" / f"{process_common_name()}.json", process_common())

    for nozzle, spec in PLASTIC_NOZZLES.items():
        m = plastic_machine(nozzle, spec)
        if bundle.private:
            # The TinManX1 index lists only its curated machines for plastic-only printing.
            write_json(pack_root / f"machine/{m['name']}.json", m)
        else:
            add("machine_list", m)
        add("machine_list", composite_machine(nozzle, spec))
        add("process_list", plastic_process(nozzle, spec))
        for label, mode in FIBER_MODES.items():
            add("process_list", fiber_process(nozzle, spec, label, mode, layup_payload))
            if rocket_compare:
                add("process_list", rocket_compare_process(nozzle, spec, label, mode, layup_payload))

    for material, values in MATERIALS.items():
        add("filament_list", filament_base(material, values))
        add("filament_list", plastic_filament(material))
        for fiber in CONTINUOUS_FIBERS:
            add("filament_list", composite_filament(material, values, fiber))

    for list_key, items in preserved_entries.items():
        known_names = {item.get("name") for item in top[list_key]}
        top[list_key].extend(item for item in items if item.get("name") not in known_names)

    if bundle.private:
        write_json(index_path, top)
        return
    # The neutral bundle as orca_profile_tool.py update-index and fix-variant leave it: the
    # lists dependencies-first, and the machines' per-extruder arrays at full width.
    tool = profile_tool()
    sections, problems = tool.build_index_sections(str(PROFILE_ROOT), bundle.vendor)
    if sections is None or problems:
        raise SystemExit("\n".join(problems) or f"{bundle.vendor}: two profiles share a name")
    write_json(index_path, {**top, **sections})
    if tool.fix_variant_arrays(str(PROFILE_ROOT), [bundle.vendor])[1]:
        raise SystemExit(f"{bundle.vendor}: fix-variant failed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--fiber-layup-template",
        choices=["none", "balanced", "perimeter-shell", "tetragrid-core", "first-layer-off-tetragrid"],
        default="none",
        help="Optional advanced layup payload template to write into continuous-fiber process profiles.",
    )
    parser.add_argument(
        "--vendor",
        choices=sorted(BUNDLES),
        default="tinmanx1",
        help="tinmanx1 (default) writes the TinManX1 pack; fibreseek writes the neutral FibreSeek vendor bundle.",
    )
    parser.add_argument(
        "--rocket-compare",
        action="store_true",
        help="Also write the Rocket Compare Composite Only process lanes, for G-code comparison with Rocket Slicer.",
    )
    args = parser.parse_args()
    build_pack(args.fiber_layup_template, BUNDLES[args.vendor], args.rocket_compare)
