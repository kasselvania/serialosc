#!/usr/bin/env python3
"""Run only an explicitly built isolated supervisor fixture, never a daemon."""
import argparse
import json
import os
from pathlib import Path
import selectors
import signal
import socket
import subprocess
import tempfile
import time
import unittest

FIXTURE = None


def osc(path):
    def string(value):
        value = value.encode() + b"\0"
        return value + b"\0" * (-len(value) % 4)
    return string(path) + string(",")


class ShutdownTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="lapis-supervisor-test-")
        self.addCleanup(self.temp.cleanup)
        self.log = Path(self.temp.name) / "events.jsonl"
        self.proc = None
        self.addCleanup(self.cleanup_processes)

    def cleanup_processes(self):
        if self.proc is not None:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.proc.wait(timeout=3)
            self.proc.stderr.close()

    def events(self):
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def wait_event(self, event):
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            found = [item for item in self.events() if item["event"] == event]
            if found:
                return found[-1]
            if self.proc.poll() is not None:
                self.fail(f"fixture exited {self.proc.returncode}: {self.proc.stderr.read()}")
            time.sleep(0.01)
        self.fail(f"timed out waiting for {event}: {self.events()}")

    def start(self, **extra):
        env = dict(os.environ, LAPIS_SERIALOSC_STRICT="1",
                   LAPIS_SERIALOSC_USB_VID="0403", LAPIS_SERIALOSC_USB_PID="6001",
                   LAPIS_SERIALOSC_USB_SERIAL="fixture-only", LAPIS_SERIALOSC_USB_INTERFACE="00",
                   LAPIS_SERIALOSC_SURFACES="fixture-only:arc2",
                   LAPIS_FIXTURE_LOG=str(self.log))
        env.update(extra)
        self.proc = subprocess.Popen([str(FIXTURE), "-c", self.temp.name], env=env,
                                     stderr=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                     text=True, start_new_session=True)
        selector = selectors.DefaultSelector()
        selector.register(self.proc.stderr, selectors.EVENT_READ)
        try:
            self.assertTrue(selector.select(4), "fixture did not report a bound socket")
            line = self.proc.stderr.readline().strip()
        finally:
            selector.close()
        self.assertTrue(line.startswith("FIXTURE_OSC_PORT="), line)
        self.port = int(line.split("=", 1)[1])
        self.assertNotEqual(self.port, 12002)
        self.assertGreater(self.port, 0)

    def send(self, path):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(osc(path), ("127.0.0.1", self.port))

    def finish(self):
        code = self.proc.wait(timeout=4)
        self.assertEqual(code, 0, self.proc.stderr.read())
        events = self.events()
        self.assertFalse([e for e in events if "unexpected" in e["event"] or "bad-ipc" in e["event"]], events)
        for event in events:
            if event["event"] in ("detector-start", "worker-start"):
                with self.assertRaises(ProcessLookupError):
                    os.kill(event["pid"], 0)
        return events

    def test_sigint_waits_for_worker_cleanup_and_rejects_late_spawn(self):
        self.start(LAPIS_FIXTURE_LATE_CONNECTION="1")
        self.wait_event("worker-start")
        self.proc.send_signal(signal.SIGINT)
        self.wait_event("worker-should-exit")
        self.assertIsNone(self.proc.poll())
        self.proc.send_signal(signal.SIGINT)
        self.proc.send_signal(signal.SIGTERM)
        self.send("/serialosc/enable")
        events = self.finish()
        self.assertEqual(sum(e["event"] == "worker-start" for e in events), 1)
        self.assertEqual(sum(e["event"] == "detector-start" for e in events), 1)
        self.assertTrue(any(e["event"] == "late-connection" for e in events))
        self.assertTrue(any(e["event"] == "worker-cleanup-complete" for e in events))

    def test_sigterm_waits_for_worker(self):
        self.start()
        self.wait_event("worker-start")
        self.proc.send_signal(signal.SIGTERM)
        self.wait_event("worker-should-exit")
        self.assertIsNone(self.proc.poll())
        self.finish()

    def test_signal_during_osc_disable(self):
        self.start()
        self.wait_event("worker-start")
        self.send("/serialosc/disable")
        self.wait_event("worker-should-exit")
        self.proc.send_signal(signal.SIGTERM)
        events = self.finish()
        self.assertEqual(sum(e["event"] == "worker-should-exit" for e in events), 1)

    def test_signal_after_osc_disable_completed(self):
        self.start()
        self.wait_event("worker-start")
        self.send("/serialosc/disable")
        self.wait_event("worker-cleanup-complete")
        time.sleep(0.1)
        self.assertIsNone(self.proc.poll())
        self.proc.send_signal(signal.SIGTERM)
        self.finish()

    def test_no_workers(self):
        self.start(LAPIS_FIXTURE_NO_DEVICE="1")
        self.wait_event("detector-start")
        self.proc.send_signal(signal.SIGTERM)
        events = self.finish()
        self.assertFalse(any(e["event"] == "worker-start" for e in events))

    def test_pending_enable_signal_before_first_uv_run(self):
        self.start(LAPIS_FIXTURE_SIGNAL_BEFORE_RUN="1")
        self.finish()

    def test_stuck_worker_keeps_supervisor_alive_for_launcher_fallback(self):
        self.start(LAPIS_FIXTURE_STUCK_WORKER="1")
        self.wait_event("worker-start")
        self.proc.send_signal(signal.SIGTERM)
        self.wait_event("worker-should-exit")
        time.sleep(0.4)
        self.assertIsNone(self.proc.poll())
        # Test-owned isolated process group only. Production launcher owns
        # its own bounded grace deadline and forced cleanup behavior.


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", type=Path, default=os.getenv("LAPIS_SUPERVISOR_FIXTURE"))
    args, remaining = parser.parse_known_args()
    if not args.fixture:
        parser.error("--fixture or LAPIS_SUPERVISOR_FIXTURE is required")
    FIXTURE = args.fixture.resolve()
    if FIXTURE.name != "lapis-supervisor-fixture" or FIXTURE.parent.name != "fixture-bin":
        parser.error("an explicitly built isolated lapis-supervisor-fixture is required")
    for name in ("serialosc-device", "serialosc-detector"):
        child = FIXTURE.parent / name
        if b"Inert native-ABI IPC child" not in child.read_bytes():
            parser.error(f"refusing non-fixture child: {child}")
    unittest.main(argv=[__file__, *remaining], verbosity=2)
