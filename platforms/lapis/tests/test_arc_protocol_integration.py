#!/usr/bin/env python3
"""Actual C production-protocol interoperability, no hardware or discovery.

Run with LAPIS_ARC_FIXTURE=/absolute/path/to/serialosc-arc-protocol-fixture.
An unset variable deliberately skips; a configured but broken fixture fails.
Only owned subprocesses, temporary config, and ephemeral loopback UDP are used.
"""
import importlib.util
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import unittest

PATH = Path(__file__).resolve().parents[1] / "arc_workbench.py"
spec = importlib.util.spec_from_file_location("lapis_arc_production_integration", PATH)
w = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = w
spec.loader.exec_module(w)

FIXTURE = os.environ.get("LAPIS_ARC_FIXTURE")
SERIAL = "ARC-PROTOCOL-FIXTURE"


class ProductionFixture:
    def __enter__(self):
        self.temp = tempfile.TemporaryDirectory(prefix="lapis-arc-protocol-")
        self.stderr = tempfile.TemporaryFile(mode="w+t")
        self.process = None
        self.buffer = b""
        try:
            self.process = subprocess.Popen(
                [str(Path(FIXTURE).resolve()), self.temp.name], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=self.stderr, bufsize=0)
            ready = self.line()
            if set(ready) != {"ready", "port"} or ready["ready"] is not True:
                raise AssertionError(f"invalid readiness: {ready!r}")
            self.port = ready["port"]
            if type(self.port) is not int or not 1 <= self.port <= 65535 or self.port == 12002:
                raise AssertionError(f"unsafe fixture port: {self.port!r}")
            return self
        except BaseException:
            self.__exit__(*sys.exc_info())
            raise

    def line(self):
        import time
        end = time.monotonic() + 3
        while b"\n" not in self.buffer:
            remaining = end - time.monotonic()
            if remaining <= 0 or not select.select([self.process.stdout], [], [], remaining)[0]:
                raise AssertionError("fixture response deadline exceeded")
            chunk = os.read(self.process.stdout.fileno(), 4096)
            if not chunk:
                self.stderr.seek(0)
                raise AssertionError("fixture exited before response: " + self.stderr.read())
            self.buffer += chunk
            if len(self.buffer) > 8192:
                raise AssertionError("fixture response unbounded")
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def command(self, text):
        self.process.stdin.write((text + "\n").encode("ascii"))
        self.process.stdin.flush()

    def status(self):
        self.command("status")
        return self.line()

    def __exit__(self, kind, value, tb):
        failure = None
        try:
            if self.process is not None:
                if self.process.poll() is None:
                    self.process.stdin.close()  # Orderly EOF, not a daemon signal.
                    try:
                        self.process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        self.process.kill()  # Only the exact child we own.
                        self.process.wait(timeout=3)
                        failure = "fixture failed orderly EOF shutdown"
                if self.process.returncode != 0:
                    self.stderr.seek(0)
                    failure = f"fixture exit {self.process.returncode}: {self.stderr.read()}"
                if self.process.stdin and not self.process.stdin.closed:
                    self.process.stdin.close()
                self.process.stdout.close()
        finally:
            self.stderr.close()
            self.temp.cleanup()
        if kind is None and failure:
            raise AssertionError(failure)


@unittest.skipUnless(FIXTURE, "set LAPIS_ARC_FIXTURE to the compiled production fixture")
class ProductionProtocolTests(unittest.TestCase):
    def bench(self, fixture, transport, log):
        self.assertNotEqual(transport.callback[1], 12002)
        return w.ArcWorkbench(transport, SERIAL, fixture.port, "arc2",
                              timeout=.15, ttl_ms=1500, report=log.append)

    def test_acquire_renew_output_release_and_durable_free(self):
        with ProductionFixture() as fixture:
            transport = w.UDPTransport()
            try:
                log = []
                bench = self.bench(fixture, transport, log)
                self.assertEqual(bench.probe().mode, "free")
                self.assertEqual(fixture.status()["calls"], 0)
                bench.acquire()
                state = bench.query()
                self.assertTrue(bench.owned(state))
                bench.ring(1, 4)
                # A subsequent production readback on the same UDP flow fences
                # output delivery before inspecting the fake hardware edge.
                self.assertTrue(bench.owned(bench.query()))
                status = fixture.status()
                self.assertEqual(status["levels"], [0, 4])
                self.assertEqual(status["calls"], 3)  # two dark + one output
                self.assertEqual(status["saved_port"], 0)
                bench.renew()
                self.assertIn("LEASE_RENEWED verified", log)
                fixture.command("delta")
                bench.listen(.05, renew=False)
                self.assertEqual(bench.events, 1)
                self.assertTrue(any(line.startswith("ENCODER ") for line in log))
                released = bench.release()
                self.assertEqual((released.mode, released.port, released.owner), ("free", 0, 0))
                self.assertEqual(released.prefix, "/saved")
                status = fixture.status()
                self.assertEqual(status["levels"], [0, 0])
                self.assertEqual(status["calls"], 5)
                self.assertEqual((status["mode"], status["port"], status["saved_port"]), ("free", 0, 0))
                self.assertFalse(bench.cleanup())
                fixture.command("exit")
                self.assertEqual(fixture.process.wait(timeout=3), 0)
                self.assertEqual(list(Path(fixture.temp.name).iterdir()), [])
            finally:
                transport.close()

    def test_server_ttl_expiry_without_renew_or_release(self):
        with ProductionFixture() as fixture:
            transport = w.UDPTransport()
            try:
                log = []
                bench = self.bench(fixture, transport, log)
                bench.acquire()
                bench.ring(0, 5)
                self.assertTrue(bench.owned(bench.query()))
                self.assertEqual(fixture.status()["levels"], [5, 0])
                state = bench.expiry_test()
                self.assertEqual((state.mode, state.port, state.owner), ("free", 0, 0))
                self.assertTrue(any("lost_observed=True" in line for line in log))
                status = fixture.status()
                self.assertEqual(status["levels"], [0, 0])
                self.assertEqual(status["calls"], 5)
                self.assertEqual(status["saved_port"], 0)
                self.assertFalse(bench.cleanup())
                with self.assertRaises(w.LeaseLost):
                    bench.ring(0, 1)
            finally:
                transport.close()


if __name__ == "__main__":
    unittest.main()
