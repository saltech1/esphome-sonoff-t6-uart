import pathlib
import subprocess
import sys
import tempfile
import unittest
import yaml

ROOT = pathlib.Path(__file__).resolve().parents[1]


class UARTConfigurationTests(unittest.TestCase):
    def check_config(self, uart_changes=None, button_id=None):
        config = yaml.safe_load((ROOT / 'examples/sonoff-t6-uart.yaml').read_text())
        config['external_components'][0]['source']['path'] = str(ROOT / 'components')
        if uart_changes:
            config['uart'].update(uart_changes)
        if button_id is not None:
            config['binary_sensor'][0]['button_id'] = button_id
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            path = pathlib.Path(directory) / 'test.yaml'
            path.write_text(yaml.safe_dump(config), encoding='utf-8')
            result = subprocess.run([sys.executable, '-m', 'esphome', 'config', str(path)],
                                    capture_output=True, text=True)
        return result.returncode, result.stdout + result.stderr

    def test_receive_only(self):
        code, output = self.check_config()
        self.assertEqual(code, 0, output)

    def test_reject_wrong_framing(self):
        for changes in ({'baud_rate': 9600}, {'data_bits': 7}, {'parity': 'EVEN'}, {'stop_bits': 2}):
            with self.subTest(changes=changes):
                code, output = self.check_config(changes)
                self.assertNotEqual(code, 0, output)
                self.assertIn('Failed config', output)

    def test_reject_invalid_button(self):
        for button in (0, 4):
            code, output = self.check_config(button_id=button)
            self.assertNotEqual(code, 0, output)
            self.assertIn('Failed config', output)

    def test_reject_debugger_dummy_receiver(self):
        code, output = self.check_config({'debug': {'dummy_receiver': True}})
        self.assertNotEqual(code, 0, output)
        self.assertIn('dummy_receiver: false', output)

    def test_allow_non_consuming_debugger(self):
        code, output = self.check_config({'debug': {'dummy_receiver': False}})
        self.assertEqual(code, 0, output)
