#!/usr/bin/env python3
"""Sidebar wiring of the continuous-fiber panel.

The capability, lane and resolver rules themselves are tested in
tests/libslic3r/test_fibreseek_materials.cpp.
"""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ContinuousFiberPanelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src/slic3r/GUI/Plater.cpp").read_text(encoding="utf-8")

    def function(self, start, end):
        return self.source.split(start, 1)[1].split(end, 1)[0]

    def fiber_callback(self):
        return self.function("auto apply_selected_fiber = [this]", "p->m_combo_continuous_fiber->Bind")

    def test_sidebar_uses_the_shared_capability_check(self):
        capability = self.function(
            "static bool sidebar_selected_printer_supports_continuous_fiber(",
            "static void sidebar_ensure_filament_preset_slot(")
        self.assertIn("selected_printer_supports_continuous_fiber(*preset_bundle)", capability)
        self.assertNotIn("printer.name", capability)
        self.assertNotIn("find(", capability)

    def test_panel_only_shows_for_continuous_fiber_printers(self):
        visibility = self.function("void Sidebar::update_continuous_fiber_visibility()",
                                   "void Sidebar::refresh_continuous_fiber_choice()")
        self.assertIn("printer_technology() == ptFFF", visibility)
        self.assertIn("sidebar_selected_printer_supports_continuous_fiber(bundle)", visibility)
        self.assertIn("m_panel_continuous_fiber->Show(visible)", visibility)
        self.assertIn("m_combo_continuous_fiber->Enable(visible)", visibility)
        self.assertIn("m_scrolled_sizer->Layout()", visibility)
        for start, end in (
            ("void Sidebar::update_all_preset_comboboxes()", "void Sidebar::update_presets("),
            ("void Sidebar::update_presets(Preset::Type", "void Sidebar::update_presets_from_to("),
        ):
            self.assertIn("update_continuous_fiber_visibility();", self.function(start, end))
        construction = self.source.split("scrolled_sizer->Add(p->m_panel_continuous_fiber", 1)[1]
        self.assertIn("update_continuous_fiber_visibility();", construction.split("//add project title", 1)[0])

    def test_ordinary_printers_get_no_fiber_settings(self):
        # The fiber table carries identity only; material data comes from the composite profile.
        self.assertNotIn("sidebar_apply_continuous_fiber_material", self.source)
        table = self.function("static const std::vector<SidebarContinuousFiberMaterial>& sidebar_continuous_fiber_materials()",
                              "static bool sidebar_is_tinmanx1_matrix_material(")
        self.assertNotIn("0.25, 102", table)
        self.assertNotIn('set_key_value("continuous_fiber_diameter"', self.source)
        callback = self.fiber_callback()
        self.assertLess(callback.index("sidebar_selected_printer_supports_continuous_fiber(preset_bundle)"),
                        callback.index('app_config->set("continuous_fiber_material_index"'))

    def test_fiber_choice_resolves_a_profile_before_writing(self):
        apply = self.function("bool Sidebar::apply_continuous_fiber_material_to_slot(",
                              "bool Sidebar::sync_continuous_fiber_selector_from_filament_preset(")
        # Slot and project data commit together, only after resolution (FibreSeekMaterials).
        self.assertIn("commit_continuous_fiber_choice(", apply)
        self.assertNotIn("apply_filament_slot_selection(", apply)
        self.assertNotIn("set_filament_preset(", apply)
        refresh = self.function("void Sidebar::refresh_continuous_fiber_choice()",
                                "void Sidebar::refresh_continuous_fiber_slot_choice()")
        self.assertLess(refresh.index("sync_continuous_fiber_selector_from_filament_preset("),
                        refresh.index("apply_continuous_fiber_material_to_slot("))
        for name in ("sidebar_composite_filament_name", "sidebar_composite_filament_alias",
                     "sidebar_resolve_loaded_composite_filament_name", "sidebar_composite_filament_profile_exists"):
            self.assertNotIn(name, self.source)
        callback = self.fiber_callback()
        validation = callback.index("if (!apply_continuous_fiber_material_to_slot(material, false))")
        self.assertLess(validation, callback.index('app_config->set("continuous_fiber_material_index"'))

    def test_selector_sync_matches_material_data_not_names(self):
        identity = self.function("static int sidebar_continuous_fiber_material_index_for_filament_preset(",
                                 "static int sidebar_continuous_fiber_material_index()")
        self.assertIn("filament_is_cfc_slot_profile(*preset)", identity)
        self.assertIn("fiber_name == material_name && fiber_type == material_type", identity)
        self.assertIn('material_kind == "composite_matrix"', identity)
        self.assertIn("recipe || source_id.empty() || source_id == material_source", identity)
        self.assertNotIn("searchable", identity)
        self.assertNotIn("profile_suffix", identity)

    def test_fiber_choice_is_a_named_native_control(self):
        self.assertIn("wxChoice* m_combo_continuous_fiber", self.source)
        self.assertIn('SetName(_L("Continuous fiber material"))', self.source)
        self.assertIn("m_combo_continuous_fiber->Bind(wxEVT_CHOICE", self.source)


if __name__ == "__main__":
    unittest.main()
