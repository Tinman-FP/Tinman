"""Guard Windows CI failure propagation and the VS 2026 CMake requirement.

OrcaSlicer 2.5 replaced build_release_vs.bat with build_win.bat, which stops on every failed
step through its error_check macro and ships its own test suite (scripts/test_build_win.ps1).
These checks make sure CI still uses a CMake new enough for VS 2026, runs that suite before the
Windows builds, and lets a failed build_win.bat fail its step.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".github/workflows"


class WindowsBuildContract(unittest.TestCase):
    def test_native_cmake_supports_vs_2026(self):
        # VS 2026 needs CMake 4.2 or newer; every leg, Windows ARM64 included, gets 4.3.x.
        for name in ("build_deps.yml", "build_orca.yml"):
            text = (WORKFLOWS / name).read_text()
            self.assertIn('cmakeVersion: "~4.3.0"', text, name)
            self.assertNotIn("3.31.6", text, name)
            self.assertNotRegex(text, r"if: \$\{\{ !\(runner\.os == 'Windows' && inputs\.arch == 'arm64'\) \}\}\n\s+with:\n\s+cmakeVersion", name)

    def test_batch_build_stops_on_errors(self):
        text = (ROOT / "build_win.bat").read_text()
        self.assertIn('set "error_check=if not ^!errorlevel^! == 0 (set rc=^!errorlevel^!& goto :die)"', text)
        self.assertGreaterEqual(text.count("%error_check%"), 10)

    def test_build_script_suite_runs_before_windows_builds(self):
        text = (WORKFLOWS / "build_all.yml").read_text()
        self.assertIn(r".\scripts\test_build_win.ps1", text)
        self.assertRegex(text, r"build_windows:[\s\S]+?needs: check_build_script")

    def test_failed_build_fails_its_step(self):
        # A pwsh step's exit code is the last native command's, so build_win.bat must end each step.
        for name, marker in (("build_deps.yml", "- name: Build on Windows"), ("build_orca.yml", "- name: Build slicer Win")):
            text = (WORKFLOWS / name).read_text()
            step = text[text.index(marker):]
            run = re.search(r"run: \|\n((?:\s{10,}.*\n)+)", step)
            self.assertIsNotNone(run, name)
            last = [line.strip() for line in run.group(1).splitlines() if line.strip()][-1]
            self.assertTrue(last.startswith(r".\build_win.bat "), f"{name}: {last}")


if __name__ == "__main__":
    unittest.main()
