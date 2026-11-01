"""Synthetic sysfs only. Loaded helper never opens a tty. No production /sys reads."""
import ctypes
import pathlib
import tempfile
import unittest
import os

LIBRARY = os.environ.get("LAPIS_USB_POLICY_LIBRARY")
# An explicitly configured bad library must fail, not skip.
LIB = ctypes.CDLL(str(pathlib.Path(LIBRARY).resolve())) if LIBRARY else None

class Policy(ctypes.Structure):
    _fields_ = [("mode", ctypes.c_int), ("vid", ctypes.c_uint), ("pid", ctypes.c_uint),
                ("interface_number", ctypes.c_uint), ("serial", ctypes.c_char * 129)]

if LIB is not None:
    LIB.sosc_lapis_usb_policy_parse.argtypes = [ctypes.POINTER(Policy)] + [ctypes.c_char_p] * 5
    LIB.sosc_lapis_usb_validate_at.argtypes = [ctypes.POINTER(Policy), ctypes.c_char_p, ctypes.c_char_p]

@unittest.skipUnless(LIBRARY, "set LAPIS_USB_POLICY_LIBRARY to compiled sysfs policy library")
class SysfsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name) / "sys"
        self.device = self.root / "devices/hub/usbdevice"
        self.interface = self.device / "interface"
        self.tty = self.interface / "ttyUSB73"
        self.tty.mkdir(parents=True)
        self.alias = self.root / "class/tty/ttyUSB73/device"
        self.alias.parent.mkdir(parents=True)
        self.alias.symlink_to(self.tty)
        for name, value in [("idVendor", "0403"), ("idProduct", "6001"), ("serial", "example-serial")]:
            (self.device / name).write_text(value + "\n")
        (self.interface / "bInterfaceNumber").write_text("00\n")
        self.policy = Policy()
        self.assertEqual(LIB.sosc_lapis_usb_policy_parse(ctypes.byref(self.policy), b"1", b"0403", b"6001", b"example-serial", b"00"), 0)

    def tearDown(self):
        self.temp.cleanup()

    def match(self, dev=b"/dev/ttyUSB73"):
        return LIB.sosc_lapis_usb_validate_at(ctypes.byref(self.policy), str(self.root).encode(), dev)

    def test_match_and_reconnect(self):
        self.assertEqual(self.match(), 1)
        (self.device / "serial").write_text("another-serial\n")
        self.assertEqual(self.match(), 0)

    def test_reject_wrong_interface(self):
        (self.interface / "bInterfaceNumber").write_text("01\n")
        self.assertEqual(self.match(), 0)

    def test_reject_paths(self):
        for path in [b"/dev/serial/by-id/example", b"/dev/ttyUSB73/../../x", b"/dev/ttyUSB74", b"ttyUSB73", b"/dev/null", b"/dev/tty"]:
            self.assertEqual(self.match(path), 0, path)

    def test_missing_attributes(self):
        for path in [self.device / "serial", self.interface / "bInterfaceNumber", self.device / "idProduct", self.device / "idVendor"]:
            saved = path.read_bytes()
            path.unlink()
            self.assertEqual(self.match(), 0)
            path.write_bytes(saved)

    def test_oversized_and_whitespace(self):
        for value in ["x" * 1024, "example-serial ", "example-serial\n\n", "*"]:
            (self.device / "serial").write_text(value + "\n")
            self.assertEqual(self.match(), 0)

    def test_no_hub_fallback(self):
        for name in ["idVendor", "idProduct", "serial"]:
            (self.device.parent / name).write_bytes((self.device / name).read_bytes())
        (self.device / "serial").write_text("not-allowlisted\n")
        self.assertEqual(self.match(), 0)
        (self.device / "serial").write_text("example-serial\n")
        (self.device / "idVendor").write_text("x" * 1000)
        self.assertEqual(self.match(), 0)
        (self.device / "idVendor").unlink()
        self.assertEqual(self.match(), 0)

    def test_escape_and_dangling_symlink(self):
        self.alias.unlink()
        self.alias.symlink_to(pathlib.Path(self.temp.name))
        self.assertEqual(self.match(), 0)
        self.alias.unlink()
        self.alias.symlink_to(self.root / "missing")
        self.assertEqual(self.match(), 0)

    def test_embedded_nul_rejected(self):
        for name, value in [("serial", b"example-serial\0evil\n"), ("idVendor", b"0403\0junk\n")]:
            path = self.device / name
            saved = path.read_bytes()
            path.write_bytes(value)
            self.assertEqual(self.match(), 0)
            path.write_bytes(saved)

    def test_legacy_not_allowed(self):
        self.policy.mode = 0
        self.assertEqual(self.match(), 0)

if __name__ == "__main__":
    unittest.main()
