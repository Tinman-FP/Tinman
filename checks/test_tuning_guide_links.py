"""Compile and exercise the URL resolver used by native setting/calibration help."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = 'https://github.com/Tinman-FP/tinmans-tuning-guide/wiki/'


class TuningGuideLinks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('C++ compiler is required for the native URL resolver')
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(cls.temp.name)
        source = root/'links.cpp'
        source.write_text('#include "libslic3r/TuningGuide.hpp"\n#include <iostream>\n'
                          'int main() { std::string s; while(std::getline(std::cin,s)) '
                          'std::cout << Slic3r::tuning_guide_url(s) << "\\n"; }\n')
        cls.binary = root/'links'
        subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'src'),str(source),'-o',str(cls.binary)],check=True,capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def resolve(self, values):
        result = subprocess.run([str(self.binary)],input='\n'.join(values)+'\n',text=True,capture_output=True,check=True)
        return result.stdout.splitlines()

    def test_setting_destinations_and_specific_overrides(self):
        inputs = ['quality_settings_seam#staggered-inner-seams',
                  'support_settings_advanced#independent-support-layer-height',
                  'material_flow_ratio_and_pressure_advance#flow-ratio',
                  'material_flow_ratio_and_pressure_advance#pressure-advance',
                  'material_flow_ratio_and_pressure_advance#Pressure Advance',
                  'printer_motion_ability#input-shaping']
        expected = ['12-quality-tab#seam','15-support-tab#advanced-support',
                    '05-flow-ratio','04-pressure-advance','04-pressure-advance',
                    '19-motion-diagnostics#input-shaping-is-not-an-acceleration-limit']
        self.assertEqual(self.resolve(inputs),[BASE+x for x in expected])

    def test_full_old_and_new_orca_paths(self):
        self.assertEqual(self.resolve([
            'https://www.orcaslicer.com/wiki/quality_settings_line_width#outer-wall',
            'print_settings/support/support_settings_advanced#z-distance']),
            [BASE+'12-quality-tab#line-width', BASE+'15-support-tab#advanced-support'])

    def test_calibration_navigation_and_safe_unknown_fallback(self):
        self.assertEqual(self.resolve(['','temp_calib','volumetric_speed_calib',
                                      'preferences_future_control','support_settings_future_control',
                                      'unknown?access_code=private']),
                         [BASE+'Home',BASE+'02-conditioning-and-temperature',
                          BASE+'03-volumetric-flow',BASE+'23-application-and-project-help',
                          BASE+'15-support-tab',BASE+'11-orca-tinmanx1-settings-reference'])

    def test_native_help_entry_points_use_tinman_routes(self):
        files=['OptionsGroup.cpp','Preferences.cpp','MainFrame.cpp','calib_dlg.cpp',
               'StepMeshDialog.cpp','ObjColorDialog.cpp','UnsavedChangesDialog.cpp']
        for name in files:
            self.assertNotIn('https://www.orcaslicer.com/wiki/',(ROOT/'src/slic3r/GUI'/name).read_text(),name)
        self.assertNotIn('orcaslicer.com/wiki/',(ROOT/'resources/data/hints.ini').read_text())


if __name__ == '__main__':
    unittest.main()
