"""Tests for the profile checks Tinman adds to scripts/orca_profile_tool.py."""

import contextlib
import io
import os
import unittest

from tests.test_profile_tool import TreeCase, apt


class TestTinmanProfileChecks(TreeCase):
    def errors(self, check):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            errors = check(self.t.profiles)
        return errors, buf.getvalue()

    def test_a_clean_tree_reports_nothing(self):
        self.t.write("V", "filament/A.json", {
            "type": "filament", "name": "A", "instantiation": "true",
            "default_filament_colour": [""], "renamed_from": "Old A"})
        self.assertEqual(self.errors(apt.check_profile_artifacts)[0], 0)
        self.assertEqual(self.errors(apt.check_disallowed_keys_and_renames)[0], 0)

    def test_backup_and_scratch_files_are_errors(self):
        self.t.write("V", "filament/A.json", {"type": "filament", "name": "A"})
        for name in ("A.json.bak", "A.json~", ".DS_Store", "._A.json"):
            self.t.write_raw("V", "filament/" + name, b"{}")
        errors, out = self.errors(apt.check_profile_artifacts)
        self.assertEqual(errors, 4, out)
        self.assertIn("V/filament/A.json.bak", out)

    def test_a_filament_preset_may_not_carry_filament_colour(self):
        self.t.write("V", "filament/A.json", {
            "type": "filament", "name": "A", "filament_colour": ["#FFFFFF"]})
        errors, out = self.errors(apt.check_disallowed_keys_and_renames)
        self.assertEqual(errors, 1, out)
        self.assertIn('disallowed filament key "filament_colour"', out)

    def test_a_renamed_from_alias_names_one_instantiated_preset(self):
        for name in ("A", "B"):
            self.t.write("V", f"filament/{name}.json", {
                "type": "filament", "name": name, "instantiation": "true",
                "renamed_from": "Old;Other " + name})
        # A base profile is not a migration target, so it does not collide.
        self.t.write("W", "filament/Base.json", {
            "type": "filament", "name": "Base", "renamed_from": "Old"})
        errors, out = self.errors(apt.check_disallowed_keys_and_renames)
        self.assertEqual(errors, 1, out)
        self.assertIn('renamed_from alias "Old" is claimed by 2', out)


if __name__ == "__main__":
    unittest.main()
