"""Exercise ESPHome's full validation with valid and unsafe bus settings."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ConfigurationTests(unittest.TestCase):
    def check_config(self, changes=None, channel=None):
        config = yaml.safe_load((ROOT / 'examples/sonoff-t6-touch.yaml').read_text())
        config['external_components'][0]['source']['path'] = str(ROOT / 'components')
        if changes:
            config['mpr03x'].update(changes)
        if channel is not None:
            config['binary_sensor'][0]['channel'] = channel
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            path = pathlib.Path(directory) / 'test.yaml'
            path.write_text(yaml.safe_dump(config), encoding='utf-8')
            result = subprocess.run([sys.executable, '-m', 'esphome', 'config', str(path)],
                                    capture_output=True, text=True)
        return result.returncode, result.stdout + result.stderr

    def test_three_electrodes(self):
        code, output = self.check_config()
        self.assertEqual(code, 0, output)

    def test_mpr032(self):
        code, output = self.check_config({'address': 0x4B})
        self.assertEqual(code, 0, output)

    def test_reject_unsafe_settings(self):
        for changes in ({'sda': 'P0'}, {'touch_threshold': 6, 'release_threshold': 6},
                        {'sda': {'number': 'P1', 'inverted': True}},
                        {'address': 0x5A}, {'half_period_us': 0}, {'update_interval': '0ms'}):
            with self.subTest(changes=changes):
                code, output = self.check_config(changes)
                self.assertNotEqual(code, 0, output)
                self.assertIn('Failed config', output)

    def test_reject_fourth_electrode(self):
        code, output = self.check_config(channel=3)
        self.assertNotEqual(code, 0, output)
        self.assertIn('Failed config', output)


if __name__ == '__main__':
    unittest.main()
