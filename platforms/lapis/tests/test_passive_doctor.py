import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('passive_doctor', Path(__file__).parents[1] / 'passive_doctor.py')
doctor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(doctor)


class PassiveDoctorTests(unittest.TestCase):
    def test_empty_tree_has_no_ready_claim(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = doctor.observe(Path(tmp))
        self.assertEqual(result['devices'], [])
        self.assertEqual(result['hub_ports'], [])
        self.assertEqual(result['mode'], 'sysfs-read-only')
        self.assertNotIn('ready', result)

    def test_synthetic_arc_and_power_are_observations_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            usb = root / 'bus/usb/devices'
            device = usb / '1-1.2'
            device.mkdir(parents=True)
            values = {'idVendor': '0403', 'idProduct': '6001', 'serial': 'm1001113', 'bMaxPower': '500mA'}
            for name, value in values.items():
                (device / name).write_text(value)
            interface = usb / '1-1:1.0'
            port = interface / '1-1-port2'
            port.mkdir(parents=True)
            for name, value in {'power_enabled': '1', 'overcurrent': '0', 'over_current_count': '0', 'disable': '0'}.items():
                (port / name).write_text(value)
            before = {str(p): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            original_open = Path.open
            opened = []
            def guarded_open(path, mode='r', *args, **kwargs):
                self.assertEqual(mode, 'r')
                self.assertTrue(path.is_relative_to(root))
                opened.append(str(path))
                return original_open(path, mode, *args, **kwargs)
            with patch.object(Path, 'open', guarded_open):
                result = doctor.observe(root)
            after = {str(p): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            self.assertEqual(before, after)
            self.assertTrue(opened)
            self.assertEqual(result['devices'][0]['serial'], 'm1001113')
            self.assertEqual(result['devices'][0]['bMaxPower'], '500mA')
            self.assertEqual(result['hub_ports'][0]['power_enabled'], '1')
            self.assertNotIn('ready', result['devices'][0])
            self.assertTrue(any('not measured' in text for text in result['limitations']))
            json.dumps(result)

    def test_bounded_attribute(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'attribute'
            p.write_text('x' * 9000)
            self.assertEqual(len(doctor.read_attribute(p)), 4096)
            self.assertIn('unavailable', doctor.read_attribute(p.parent / 'absent'))


if __name__ == '__main__':
    unittest.main()
