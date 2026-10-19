"""Guard Windows CI failure propagation and the VS 2026 CMake requirement."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WindowsBuildContract(unittest.TestCase):
    def test_native_cmake_supports_vs_2026(self):
        for name in ("build_deps.yml", "build_orca.yml"):
            text = (ROOT / ".github/workflows" / name).read_text()
            self.assertIn('$ver = "4.3.4"', text)
            self.assertNotIn('$ver = "3.31.6"', text)

    def test_batch_build_commands_fail_immediately(self):
        lines = (ROOT / "build_release_vs.bat").read_text().splitlines()
        for index, line in enumerate(lines):
            if line.strip().startswith("cmake ") or line.strip() == "call scripts/run_gettext.bat":
                self.assertEqual(lines[index + 1].strip(), "if errorlevel 1 exit /b 1")

    def test_dependency_build_is_checked_before_packaging(self):
        lines = (ROOT / ".github/workflows/build_deps.yml").read_text().splitlines()
        for index, line in enumerate(lines):
            if line.strip().startswith(".\\build_release_vs.bat "):
                self.assertIn("if ($LASTEXITCODE -ne 0)", lines[index + 1])


if __name__ == "__main__":
    unittest.main()
