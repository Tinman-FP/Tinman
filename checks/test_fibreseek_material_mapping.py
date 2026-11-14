#!/usr/bin/env python3
"""GUI wiring of the FibreSeek role map.

The map and its normalization are tested in tests/libslic3r/test_fibreseek_materials.cpp.
"""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def section(path, start, end):
    return (ROOT / path).read_text(encoding="utf-8").split(start, 1)[1].split(end, 1)[0]


class FibreSeekMaterialMappingTests(unittest.TestCase):
    def test_slicing_applies_the_role_map_after_the_plate_settings(self):
        body = section("src/slic3r/GUI/BackgroundSlicingProcess.cpp",
                       "Print::ApplyStatus BackgroundSlicingProcess::apply(",
                       "// Orca: prevent resetting")
        self.assertLess(body.index("new_config.apply(*m_current_plate->config())"),
                        body.index("apply_fibreseek_material_tool_map(*bundle, new_config)"))
        self.assertLess(body.index("apply_fibreseek_material_tool_map(*bundle, new_config)"),
                        body.index("m_print->apply(model, new_config)"))

    def test_plate_getters_report_the_role_map_and_manual_mode(self):
        maps = section("src/slic3r/GUI/PartPlate.cpp",
                       "std::vector<int> PartPlate::get_real_filament_maps(",
                       "FilamentMapMode PartPlate::get_real_filament_map_mode(")
        self.assertLess(maps.index("fibreseek_role_map(m_plater)"), maps.index("get_filament_maps()"))
        mode = section("src/slic3r/GUI/PartPlate.cpp",
                       "FilamentMapMode PartPlate::get_real_filament_map_mode(",
                       "bool PartPlate::has_spiral_mode_config()")
        self.assertLess(mode.index("fibreseek_role_map(m_plater)"), mode.index("get_filament_map_mode()"))
        self.assertLess(mode.index("return fmmManual"), mode.index("get_filament_map_mode()"))
        # The CLI has no Plater or GUI_App.
        helper = section("src/slic3r/GUI/PartPlate.cpp",
                         "static std::optional<std::vector<int>> fibreseek_role_map(",
                         "std::vector<int> PartPlate::get_real_filament_maps(")
        self.assertIn("plater ? wxGetApp().preset_bundle : nullptr", helper)

    def test_slicing_normalizes_the_project_first(self):
        body = section("src/slic3r/GUI/Plater.cpp",
                       "unsigned int Plater::priv::update_background_process(",
                       "set_extruder_filament_info")
        self.assertLess(body.index("normalize_fibreseek_material_mapping(*preset_bundle)"),
                        body.index("get_real_filament_maps("))

    def test_project_save_normalizes_before_reading_the_config(self):
        body = section("src/slic3r/GUI/Plater.cpp", "int Plater::export_3mf(", "const std::string path_u8 =")
        self.assertLess(body.index("normalize_fibreseek_material_mapping(*wxGetApp().preset_bundle)"),
                        body.index("full_config_secure()"))

    def test_plate_export_writes_the_copy_not_the_live_plate(self):
        body = section("src/slic3r/GUI/PartPlate.cpp",
                       "int PartPlateList::store_to_3mf_structure(",
                       "plate_data_list.push_back(plate_data_item)")
        apply = "apply_fibreseek_material_tool_map(*wxGetApp().preset_bundle, plate_data_item->config)"
        self.assertLess(body.index("plate_data_item->config.apply(*m_plate_list[i]->config())"), body.index(apply))
        self.assertLess(body.index("m_plater &&"), body.index(apply))
        self.assertIn('plate_data_item->filament_maps = plate_data_item->config.option<ConfigOptionInts>("filament_map")->values',
                      body)
        self.assertNotIn("apply_fibreseek_material_tool_map(*wxGetApp().preset_bundle, *m_plate_list", body)

    def test_bundle_normalizes_on_its_own_selection_paths(self):
        bundle = (ROOT / "src/libslic3r/PresetBundle.cpp").read_text(encoding="utf-8")
        for start, end in (
            ("void PresetBundle::update_multi_material_filament_presets(", "static void normalize_compatible_field("),
            ("void PresetBundle::set_filament_preset(", "void PresetBundle::set_default_suppressed("),
        ):
            self.assertIn("normalize_fibreseek_material_mapping(*this);", bundle.split(start, 1)[1].split(end, 1)[0])
        project_load = bundle.split("// 4) Load the project config values", 1)[1].split("break;", 1)[0]
        self.assertLess(project_load.index("this->fibreseek_mapping = {};"),
                        project_load.index("this->project_config.apply_only(config, s_project_options);"))


if __name__ == "__main__":
    unittest.main()
