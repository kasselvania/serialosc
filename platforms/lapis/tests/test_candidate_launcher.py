#!/usr/bin/env python3
"""Hardware-free launcher tests: temporary Python children, no daemon/ports/USB."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

PATH = Path(__file__).resolve().parents[1] / "run_candidate.py"
spec = importlib.util.spec_from_file_location("candidate_launcher", PATH)
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux process groups/procfs")
class LauncherTests(unittest.TestCase):
    def setUp(self):
        # A copied launcher layout preserves real CLI containment policy in /tmp.
        self.temp = tempfile.TemporaryDirectory(prefix="lapis-launcher-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.root.chmod(0o700)
        root_patch = mock.patch.object(c, "ROOT", self.root)
        root_patch.start()
        self.addCleanup(root_patch.stop)
        self.launcher = self.root / "platforms" / "lapis" / "run_candidate.py"
        self.launcher.parent.mkdir(parents=True)
        shutil.copyfile(PATH, self.launcher)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.state = self.root / "state"
        self.profile = self.root / "profile.json"
        self.p = dict(vid="0403", pid="6001", serial="TEST-EXACT", interface="00", surface="arc2")
        self.profile.write_text(json.dumps(self.p))
        self.profile.chmod(0o600)
        self.write_build("raise SystemExit(0)\n")
        self.args = ["--profile", str(self.profile), "--bin-dir", str(self.bin), "--state-dir", str(self.state)]
        self.active = self.args + ["--activate", "--acknowledge-device-io", "--grace", ".15", "--term-grace", ".15"]

    def write_build(self, body):
        files = {}
        for name in c.BINARIES:
            path = self.bin / name
            path.write_text("#!" + sys.executable + "\n" + body)
            path.chmod(0o700)
            files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.build = dict(schema=1, strict=True, source_commit="a" * 40, files=files)
        self.save_manifest()

    def save_manifest(self):
        (self.bin / "lapis-candidate.json").write_text(json.dumps(self.build))
        (self.bin / "lapis-candidate.json").chmod(0o600)

    def run_main(self, args=None):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return c.main(self.args if args is None else args)

    def start(self):
        process = subprocess.Popen([sys.executable, str(self.launcher)] + self.active,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        def cleanup():
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.communicate()
        self.addCleanup(cleanup)
        return process

    def until(self, predicate, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(.02)
        self.fail("timed out waiting for dummy child")

    def receipt(self):
        return json.loads((self.state / "receipt.json").read_text())

    def test_validate_is_inert_and_env_is_not_inherited(self):
        with mock.patch.object(c.subprocess, "Popen", side_effect=AssertionError("spawn forbidden")):
            self.assertEqual(self.run_main(), 0)
        self.assertFalse(self.state.exists())
        with mock.patch.dict(os.environ, {"HOME": "/never", "LD_PRELOAD": "bad", "LAPIS_SERIALOSC_USB_SERIAL": "wrong"}):
            self.assertEqual(c.environment(self.p), {
                "LAPIS_SERIALOSC_STRICT": "1", "LAPIS_SERIALOSC_USB_VID": "0403",
                "LAPIS_SERIALOSC_USB_PID": "6001", "LAPIS_SERIALOSC_USB_SERIAL": "TEST-EXACT",
                "LAPIS_SERIALOSC_USB_INTERFACE": "00", "LAPIS_SERIALOSC_SURFACES": "TEST-EXACT:arc2"})

    def test_two_activation_flags_required(self):
        for flag in ("--activate", "--acknowledge-device-io"):
            self.assertEqual(self.run_main(self.args + [flag]), 2)
            self.assertFalse(self.state.exists())

    def test_profiles_fail_closed_and_no_serial_heuristics(self):
        for field, value in (("vid", "0x0403"), ("pid", 6001), ("interface", "0"),
                             ("serial", "../escape"), ("serial", "."), ("serial", ".."),
                             ("serial", "x" * 129), ("serial", "x:y"), ("surface", "arc"),
                             ("surface", "arc3"), ("surface", None)):
            with self.subTest(field=field, value=value):
                self.profile.write_text(json.dumps(dict(self.p, **{field: value})))
                self.assertEqual(self.run_main(), 2)
        for surface in ("grid", "arc2", "arc4"):
            self.profile.write_text(json.dumps(dict(self.p, serial="not-a-model", surface=surface)))
            self.assertEqual(self.run_main(), 0)
        for p in ({k: v for k, v in self.p.items() if k != "surface"}, dict(self.p, extra="x")):
            self.profile.write_text(json.dumps(p))
            self.assertEqual(self.run_main(), 2)

    def test_duplicate_json_keys_rejected(self):
        self.profile.write_text(json.dumps(self.p)[:-1] + ', "surface":"grid"}')
        self.assertEqual(self.run_main(), 2)

    def test_manifest_fields_and_hashes(self):
        original = json.loads(json.dumps(self.build))
        for field, value in (("schema", True), ("schema", 2), ("strict", False),
                             ("strict", 1), ("source_commit", "short"), ("files", {})):
            self.build = dict(original, **{field: value})
            self.save_manifest()
            self.assertEqual(self.run_main(), 2)
        self.build = original
        self.save_manifest()
        for name in c.BINARIES:
            path = self.bin / name
            data = path.read_bytes()
            path.write_bytes(data + b"# changed\n")
            self.assertEqual(self.run_main(), 2)
            path.write_bytes(data)
        self.assertFalse(self.state.exists())

    def test_symlink_and_non_regular_binary_rejected(self):
        path = self.bin / "serialosc-device"
        data = path.read_bytes()
        path.unlink()
        target = self.root / "real"
        target.write_bytes(data)
        target.chmod(0o700)
        path.symlink_to(target)
        self.assertEqual(self.run_main(), 2)
        path.unlink()
        path.mkdir()
        self.assertEqual(self.run_main(), 2)

    def test_manifest_symlink_rejected(self):
        path = self.bin / "lapis-candidate.json"
        path.rename(self.root / "manifest")
        path.symlink_to(self.root / "manifest")
        self.assertEqual(self.run_main(), 2)

    def test_unsafe_paths_permissions_and_nested_trees_rejected(self):
        with tempfile.TemporaryDirectory() as outside:
            for state in (outside, str(c.ROOT), str(self.bin / "state")):
                args = self.args.copy()
                args[-1] = state
                self.assertEqual(self.run_main(args), 2)
        link = self.root / "link"
        link.symlink_to(self.bin, target_is_directory=True)
        args = self.args.copy()
        args[3] = str(link)
        self.assertEqual(self.run_main(args), 2)
        self.state.mkdir(mode=0o755)
        self.assertEqual(self.run_main(), 2)
        self.state.chmod(0o700)
        (self.state / "config").symlink_to(self.bin, target_is_directory=True)
        self.assertEqual(self.run_main(), 2)

    def test_lock_is_persistent_and_nonblocking(self):
        self.state.mkdir(mode=0o700)
        with c.locks(self.state, self.bin):
            with self.assertRaises(c.Invalid):
                with c.locks(self.state, self.bin):
                    self.fail("duplicate lock acquired")
        self.assertTrue((self.state / "launcher.lock").exists())
        with c.locks(self.state, self.bin):
            pass

    def test_natural_exit_and_private_atomic_files(self):
        self.assertEqual(self.run_main(self.active), 0)
        self.assertEqual(self.receipt()["status"], "stopped")
        self.assertEqual(self.receipt()["returncode"], 0)
        for path in (self.state, self.state / "config"):
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o700)
        for name in ("profile.json", "candidate-manifest.json", "receipt.json", "launcher.lock"):
            self.assertEqual(stat.S_IMODE((self.state / name).stat().st_mode), 0o600)
        self.assertFalse(list(self.state.glob(".*")))
        self.assertEqual(self.run_main(self.active), 0)

    def test_nonzero_child_exit_propagated(self):
        self.write_build("raise SystemExit(7)\n")
        self.assertEqual(self.run_main(self.active), 7)
        self.assertEqual(self.receipt()["returncode"], 7)

    def test_spawn_failure_is_recorded_without_killing(self):
        with mock.patch.object(c.subprocess, "Popen", side_effect=OSError("synthetic spawn failure")):
            self.assertEqual(self.run_main(self.active), 2)
        self.assertEqual(self.receipt()["status"], "spawn-failed")

    def test_uninterruptible_cleanup_model_is_bounded_and_blocks_retry(self):
        # Simulated kernel-stuck process: no actual hanging process is created.
        child = mock.Mock(pid=123456)
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.object(c, "leader_exited", return_value=False))
            wait = stack.enter_context(mock.patch.object(c, "wait_group", return_value=False))
            stack.enter_context(mock.patch.object(c, "group_live", return_value=True))
            kill = stack.enter_context(mock.patch.object(c.os, "kill"))
            group = stack.enter_context(mock.patch.object(c, "signal_group"))
            complete, code, escalated = c.supervise(child, [signal.SIGINT], .1, .1)
        self.assertFalse(complete)
        self.assertIsNone(code)
        self.assertEqual(escalated, ["SIGTERM", "SIGKILL"])
        self.assertEqual(wait.call_count, 3)
        kill.assert_called_once_with(child.pid, signal.SIGINT)
        self.assertEqual(group.call_count, 2)
        child.wait.assert_not_called()
        self.state.mkdir(mode=0o700)
        c.atomic_json(self.state / "receipt.json", {"status": "cleanup-incomplete"})
        self.assertEqual(self.run_main(self.active), 2)

    def test_preserve_existing_config(self):
        self.state.mkdir(mode=0o700)
        config = self.state / "config"
        config.mkdir(mode=0o700)
        path = config / "TEST-EXACT.conf"
        path.write_text("do not replace me\n")
        path.chmod(0o600)
        self.assertEqual(self.run_main(self.active), 0)
        self.assertEqual(path.read_text(), "do not replace me\n")

    def test_stale_live_receipt_never_kills_persisted_pid(self):
        self.state.mkdir(mode=0o700)
        c.atomic_json(self.state / "receipt.json", {"status": "running", "child_pid": os.getpid()})
        with mock.patch.object(c.os, "kill", side_effect=AssertionError("must not signal")), \
             mock.patch.object(c.os, "killpg", side_effect=AssertionError("must not signal")):
            self.assertEqual(self.run_main(self.active), 2)

    def test_sigint_and_sigterm_orderly_then_exit(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signum=signum):
                self.write_build('''import json, os, signal, sys, time
from pathlib import Path
state = Path(sys.argv[2]).parent
(state / "dummy-env.json").write_text(json.dumps(dict(os.environ)))
def stop(number, frame):
    (state / "got-signal").write_text(str(number))
    raise SystemExit(0)
signal.signal(signal.SIGINT, stop)
signal.signal(signal.SIGTERM, stop)
(state / "ready").touch()
while True: time.sleep(.02)
''')
                process = self.start()
                self.until(lambda: (self.state / "ready").exists())
                process.send_signal(signum)
                process.communicate(timeout=5)
                self.assertEqual(process.returncode, 128 + signum)
                self.assertEqual((self.state / "got-signal").read_text(), str(signum))
                self.assertEqual(self.receipt()["escalated"], [])
                env = json.loads((self.state / "dummy-env.json").read_text())
                # Python itself may add LC_CTYPE for locale coercion.
                self.assertNotIn("HOME", env)
                self.assertEqual(env["LAPIS_SERIALOSC_SURFACES"], "TEST-EXACT:arc2")
                (self.state / "ready").unlink()

    def test_stubborn_group_escalation_and_unrelated_child_survives(self):
        self.write_build('''import os, signal, sys, time
from pathlib import Path
state = Path(sys.argv[2]).parent
signal.signal(signal.SIGINT, signal.SIG_IGN)
signal.signal(signal.SIGTERM, signal.SIG_IGN)
pid = os.fork()
if pid == 0:
    while True: time.sleep(.02)
(state / "descendant").write_text(str(pid))
(state / "ready").touch()
while True: time.sleep(.02)
''')
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(20)"], start_new_session=True)
        self.addCleanup(lambda: (unrelated.terminate(), unrelated.wait()) if unrelated.poll() is None else None)
        process = self.start()
        self.until(lambda: (self.state / "ready").exists())
        descendant = int((self.state / "descendant").read_text())
        start = time.monotonic()
        process.send_signal(signal.SIGTERM)
        process.communicate(timeout=5)
        self.assertLess(time.monotonic() - start, 3)
        self.assertEqual(self.receipt()["escalated"], ["SIGTERM", "SIGKILL"])
        self.assertEqual(self.receipt()["status"], "stopped")
        self.assertIsNone(unrelated.poll())
        stat_path = Path("/proc") / str(descendant) / "stat"
        if stat_path.exists():
            self.assertEqual(stat_path.read_text().rsplit(")", 1)[1].split()[0], "Z")

    def test_live_duplicate_same_or_different_state_refused(self):
        self.write_build('''import signal, sys, time
from pathlib import Path
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
(Path(sys.argv[2]).parent / "ready").touch()
while True: time.sleep(.02)
''')
        process = self.start()
        self.until(lambda: (self.state / "ready").exists())
        self.assertEqual(self.run_main(self.active), 2)
        other = self.active.copy()
        other[5] = str(self.root / "other-state")
        self.assertEqual(self.run_main(other), 2)
        self.assertIsNone(process.poll())
        process.send_signal(signal.SIGTERM)
        process.communicate(timeout=5)

    def test_own_group_cleanup_on_spontaneous_leader_exit(self):
        self.write_build('''import os, signal, sys, time
from pathlib import Path
state = Path(sys.argv[2]).parent
pid = os.fork()
if pid == 0:
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    while True: time.sleep(.02)
(state / "descendant").write_text(str(pid))
time.sleep(.1)
raise SystemExit(0)
''')
        self.assertEqual(self.run_main(self.active), 0)
        self.assertEqual(self.receipt()["status"], "stopped")
        self.assertEqual(self.receipt()["escalated"], ["SIGTERM", "SIGKILL"])


if __name__ == "__main__":
    unittest.main()
