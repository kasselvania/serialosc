#!/usr/bin/env python3
"""Offline workbench tests. Only ephemeral loopback UDP; no hardware/12002.

Copyright (c) 2026 Peter Kassel. ISC terms: see ../arc_workbench.py.
"""
import contextlib
import importlib.util
import io
from pathlib import Path
import socket
import struct
import sys
import threading
import unittest
from unittest import mock

PATH = Path(__file__).resolve().parents[1] / "arc_workbench.py"
spec = importlib.util.spec_from_file_location("lapis_arc_workbench", PATH)
w = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = w
spec.loader.exec_module(w)


class Clock:
    def __init__(self):
        self.now = 10.

    def __call__(self):
        return self.now


class FakeTransport:
    callback = ("127.0.0.1", 43100)
    endpoint = ("127.0.0.1", 43200)

    def __init__(self, clock):
        self.clock = clock
        self.queue = []
        self.sent = []
        self.mode = "free"
        self.owner = 0
        self.host, self.port, self.prefix = "127.0.0.1", 0, "/old"
        self.token = ""
        self.ttl = 3000
        self.expires = 0
        self.hook = None
        self.silent = set()
        self.info_override = None
        self.late_renew = 0
        self.grant_delay = 0
        self.release_stuck = False
        self.expiry_notify = True

    def put(self, path, tags="", args=(), peer=None):
        self.queue.append((w.encode_message(path, tags, args), peer or self.endpoint))

    def send(self, endpoint, message):
        assert endpoint == self.endpoint
        path, tags, args = w.decode_message(w.encode_message(*message))
        self.sent.append((path, tags, args))
        if self.hook and self.hook(self, path, tags, args):
            return
        if path in self.silent:
            return
        if path == "/sys/info":
            fields = self.info_override or [
                ("/sys/id", "s", ("ARC-X",)), ("/sys/size", "ii", (0, 0)),
                ("/sys/host", "s", (self.host,)), ("/sys/port", "i", (self.port,)),
                ("/sys/prefix", "s", (self.prefix,)), ("/sys/rotation", "i", (0,))]
            for field in fields:
                self.put(*field)
        elif path == "/sys/lease/info":
            remain = max(1, int((self.expires - self.clock()) * 1000)) if self.mode == "leased" else 0
            self.put("/sys/lease/state", "isssisii", (1, "ARC-X", self.mode, self.host,
                     self.port, self.prefix, remain, int(self.owner and args[0] == self.token)))
        elif path in ("/sys/lease/acquire", "/sys/lease/takeover"):
            # Model persistence before daemon TTL refresh and grant.
            self.clock.now += self.grant_delay
            self.token, self.host, self.port, self.prefix, self.ttl = args
            self.mode, self.owner = "leased", 1
            self.expires = self.clock() + self.ttl / 1000
            self.put("/sys/lease/granted", "si", (self.token, self.ttl))
        elif path == "/sys/lease/renew":
            self.expires = self.clock() + self.ttl / 1000
            self.clock.now += self.late_renew
            self.put("/sys/lease/renewed", "si", (self.token, self.ttl))
        elif path == "/sys/lease/release":
            if not self.release_stuck:
                self.mode, self.port, self.owner = "free", 0, 0
            self.put("/sys/lease/released", "s", (self.token,))

    def receive(self, seconds):
        if self.queue:
            return self.queue.pop(0)
        target = self.clock() + seconds
        if self.mode == "leased" and target >= self.expires:
            self.clock.now = self.expires
            self.mode, self.owner, self.port = "free", 0, 0
            if self.expiry_notify:
                return w.encode_message("/sys/lease/lost", "ss", (self.token, "expired")), self.endpoint
        self.clock.now = target
        return None

    def paths(self):
        return [p for p, _, _ in self.sent]


class Fixture(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.transport = FakeTransport(self.clock)
        self.log = []
        self.b = w.ArcWorkbench(self.transport, "ARC-X", 43200, "arc2",
                                clock=self.clock, report=self.log.append)

    def claim(self):
        self.b.acquire()

    def no_release(self):
        self.assertNotIn("/sys/lease/release", self.transport.paths())


class CodecTests(unittest.TestCase):
    def test_roundtrip(self):
        packet = w.encode_message("/test", "sis", ("é", -123, ""))
        self.assertEqual(w.decode_message(packet), ("/test", "sis", ("é", -123, "")))

    def test_reject_malformed(self):
        good = w.encode_message("/x", "i", (2,))
        packets = [b"", b"#bundle\0" + b"\0" * 8, good[:-1], good + b"\0" * 4,
                   b"/x\0x,i\0\0" + struct.pack(">i", 1),
                   b"/x\0\0,s\0\0\xff\0\0\0", b"/x\0\0,f\0\0" + b"\0" * 4,
                   b"/x\0\0,i\0\0", b"x" * 4100,
                   b"/x\0\0" + b"abcd", b"/x\0\0,s\0\0" + b"a" * 1028]
        for packet in packets:
            with self.subTest(packet=packet[:30]), self.assertRaises(w.WorkbenchError):
                w.decode_message(packet)

    def test_no_coercion(self):
        for tags, args in [("i", (True,)), ("i", (1.0,)), ("i", (2**31,)),
                           ("f", (1.,)), ("s", (42,)), ("s", ("a\0b",)),
                           ("s", ("x" * 1025,)), ("ii", (1,)), ("i" * 129, (0,) * 129)]:
            with self.subTest(tags=tags[:8]), self.assertRaises(w.WorkbenchError):
                w.encode_message("/x", tags, args)
        for path in ("x", "/x*", "/x y", "#bundle"):
            with self.assertRaises(w.WorkbenchError):
                w.encode_message(path)


class LeaseTests(Fixture):
    def test_readonly_probe(self):
        self.assertEqual(self.b.probe().mode, "free")
        self.assertEqual(self.transport.paths(), ["/sys/info", "/sys/lease/info"])

    def test_full_lifecycle_explicit_release(self):
        self.claim()
        self.b.ring(1, 4)
        self.b.listen(4)
        self.assertGreaterEqual(self.transport.paths().count("/sys/lease/renew"), 2)
        self.assertEqual(self.b.release().mode, "free")
        self.assertFalse(self.b.acknowledged)
        self.assertEqual(self.transport.paths().count("/sys/lease/release"), 1)
        self.assertNotIn("/sys/host", self.transport.paths())
        self.assertFalse(self.b.cleanup())

    def test_legacy_requires_explicit_takeover(self):
        self.transport.mode, self.transport.port = "legacy", 54321
        with self.assertRaisesRegex(w.WorkbenchError, "takeover"):
            self.b.acquire()
        self.assertNotIn("/sys/lease/acquire", self.transport.paths())
        self.b.acquire(takeover=True)
        self.assertIn("/sys/lease/takeover", self.transport.paths())
        self.b.release()
        self.assertEqual(self.transport.port, 0)  # does NOT restore old callback

    def test_busy_not_takeover(self):
        self.transport.mode, self.transport.port = "leased", 54321
        self.transport.expires = self.clock() + 5
        with self.assertRaisesRegex(w.WorkbenchError, "busy"):
            self.b.acquire(takeover=True)
        self.assertNotIn("/sys/lease/takeover", self.transport.paths())

    def test_silence_not_zero_devices(self):
        for path in ("/sys/info", "/sys/lease/info", "/sys/lease/acquire"):
            with self.subTest(path=path):
                self.setUp()
                self.transport.silent.add(path)
                with self.assertRaisesRegex(w.NoReply, "NO_REPLY_UNKNOWN"):
                    self.b.acquire()
                self.assertFalse(self.b.cleanup())
                self.no_release()

    def test_wrong_source_and_wrong_token_grant(self):
        for peer, token in [(("127.0.0.2", 43200), None), (("127.0.0.1", 43201), None), (None, "wrong")]:
            with self.subTest(peer=peer, token=token):
                self.setUp()
                def hook(t, path, tags, args):
                    if path == "/sys/lease/acquire":
                        t.put("/sys/lease/granted", "si", (token or args[0], args[-1]), peer)
                        return True
                self.transport.hook = hook
                with self.assertRaises(w.LeaseLost):
                    self.claim()
                self.assertFalse(self.b.cleanup())
                self.no_release()

    def test_rejection(self):
        def hook(t, path, tags, args):
            if path == "/sys/lease/acquire":
                t.put("/sys/lease/rejected", "ss", (args[0], "busy"))
                return True
        self.transport.hook = hook
        with self.assertRaisesRegex(w.LeaseLost, "busy"):
            self.claim()
        self.no_release()

    def test_readback_mismatch_never_output_or_cleanup(self):
        for field, value in [("owner", 0), ("prefix", "/other"), ("host", "127.0.0.2"), ("port", 55000)]:
            with self.subTest(field=field):
                self.setUp()
                self.claim()
                setattr(self.transport, field, value)
                with self.assertRaises(w.LeaseLost):
                    self.b.ring(0, 2)
                self.assertFalse(self.b.cleanup())
                self.no_release()
                self.assertNotIn(self.b.prefix + "/ring/all", self.transport.paths())

    def test_loss_notifications_and_invalid_events(self):
        for msg in [("/sys/lease/lost", "ss", (None, "displaced")),
                    ("/sys/lease/lost", "ss", ("wrong", "expired")),
                    ("ENC/delta", "si", ("0", 1)), ("ENC/delta", "ii", (2, 1)),
                    ("ENC/key", "ii", (0, 2)), ("/other/enc/delta", "ii", (0, 1))]:
            with self.subTest(msg=msg):
                self.setUp()
                self.claim()
                path, tags, args = msg
                path = path.replace("ENC", self.b.prefix + "/enc")
                args = tuple(self.b.token if x is None else x for x in args)
                self.transport.put(path, tags, args)
                with self.assertRaises(w.LeaseLost):
                    self.b.listen(.2)
                self.b.cleanup()
                self.no_release()

    def test_valid_encoder_and_output_bounds(self):
        self.claim()
        self.transport.put(self.b.prefix + "/enc/delta", "ii", (1, -30))
        self.transport.put(self.b.prefix + "/enc/key", "ii", (0, 1))
        self.b.listen(.1)
        self.assertEqual(self.b.events, 2)
        self.assertTrue(any(x.startswith("ENCODER ") for x in self.log))
        for ring, level in [(2, 1), (-1, 1), (0, 16), (0, -1), (True, 2)]:
            with self.assertRaises(w.WorkbenchError):
                self.b.ring(ring, level)
        for _ in range(64):
            self.b.ring(0, 1)
        with self.assertRaisesRegex(w.WorkbenchError, "budget"):
            self.b.ring(0, 1)
        self.b.release()

    def test_renew_timeout_wrong_token_late_ack(self):
        for failure in ("timeout", "token", "late"):
            with self.subTest(failure=failure):
                self.setUp()
                self.claim()
                if failure == "timeout":
                    self.transport.silent.add("/sys/lease/renew")
                elif failure == "late":
                    self.transport.late_renew = 3
                else:
                    def hook(t, path, tags, args):
                        if path == "/sys/lease/renew":
                            t.put("/sys/lease/renewed", "si", ("wrong", 3000))
                            return True
                    self.transport.hook = hook
                with self.assertRaises(w.LeaseLost):
                    self.b.renew()
                self.b.cleanup()
                self.no_release()
                with self.assertRaises(w.LeaseLost):
                    self.b.ring(0, 1)

    def test_local_expiry_no_renew_or_release(self):
        self.claim()
        self.clock.now = self.b.deadline
        with self.assertRaises(w.LeaseLost):
            self.b.renew()
        self.no_release()
        self.assertNotIn("/sys/lease/renew", self.transport.paths())

    def test_cleanup_proves_ownership_first(self):
        self.claim()
        self.assertTrue(self.b.cleanup())
        paths = self.transport.paths()
        release_index = paths.index("/sys/lease/release")
        self.assertEqual(paths[release_index - 1], "/sys/lease/info")
        self.assertEqual(paths[release_index + 1], "/sys/lease/info")

    def test_release_failure_and_missing_ack_free_readback(self):
        self.claim()
        self.transport.release_stuck = True
        with self.assertRaisesRegex(w.LeaseLost, "not verified free"):
            self.b.release()
        self.assertFalse(self.b.cleanup())
        self.setUp()
        self.claim()
        def hook(t, path, tags, args):
            if path == "/sys/lease/release":
                t.mode, t.port, t.owner = "free", 0, 0
                return True
        self.transport.hook = hook
        self.assertEqual(self.b.release().mode, "free")
        self.assertIn("readback authoritative", self.log[-1])

    def test_expiry_with_and_without_notification(self):
        for notify in (True, False):
            with self.subTest(notify=notify):
                self.setUp()
                self.claim()
                self.b.ring(0, 4)
                self.transport.expiry_notify = notify
                sent_before = len(self.transport.sent)
                self.assertEqual(self.b.expiry_test().mode, "free")
                label = "EXPIRY_NOTIFICATION_AND_FREE" if notify else "FREE_AFTER_NO_RENEW"
                self.assertTrue(self.log[-1].startswith(label + " free;"))
                self.assertIn("physical darkness NOT observed", self.log[-1])
                self.assertFalse(any("EXPIRY_VERIFIED" in line for line in self.log))
                self.assertFalse(self.b.cleanup())
                with self.assertRaises(w.LeaseLost):
                    self.b.ring(0, 1)
                self.assertEqual(self.transport.paths()[sent_before:], ["/sys/lease/info"])
                self.no_release()
                self.assertNotIn("/sys/lease/renew", self.transport.paths())

    def test_delayed_grant_keeps_conservative_deadline_and_waits_for_daemon(self):
        self.transport.grant_delay = .2  # less than the .25 s reply timeout
        self.claim()
        acquire_time = self.clock() - self.transport.grant_delay
        self.assertAlmostEqual(self.b.deadline, acquire_time + self.b.ttl_ms / 1000)
        self.assertAlmostEqual(self.transport.expires, self.b.deadline + .2)
        # Persistence/grant delay must not extend the client's ownership window.
        # The extra reply timeout in the expiry experiment covers this delay.
        daemon_expiry = self.transport.expires
        self.b.ring(0, 4)
        sent_before = len(self.transport.sent)
        self.assertEqual(self.b.expiry_test().mode, "free")
        self.assertGreaterEqual(self.clock(), daemon_expiry)
        self.assertTrue(self.log[-1].startswith("EXPIRY_NOTIFICATION_AND_FREE free;"))
        self.assertFalse(self.b.cleanup())
        with self.assertRaises(w.LeaseLost):
            self.b.ring(0, 1)
        self.assertEqual(self.transport.paths()[sent_before:], ["/sys/lease/info"])

    def test_intervening_legacy_port_zero_is_only_free_after_no_renew(self):
        self.claim()
        self.b.ring(0, 4)
        self.clock.now += .5
        self.assertLess(self.clock(), self.transport.expires)
        # Another client sets /sys/port 0, with its best-effort displacement
        # notification dropped. This is not timer cleanup; no client write here.
        self.transport.mode, self.transport.port, self.transport.owner = "free", 0, 0
        self.transport.token = ""
        sent_before = len(self.transport.sent)
        self.assertEqual(self.b.expiry_test().mode, "free")
        self.assertTrue(self.log[-1].startswith("FREE_AFTER_NO_RENEW free;"))
        self.assertFalse(any("EXPIRY_NOTIFICATION_AND_FREE" in line or "EXPIRY_VERIFIED" in line
                             for line in self.log))
        self.assertFalse(self.b.cleanup())
        with self.assertRaises(w.LeaseLost):
            self.b.ring(0, 1)
        self.assertEqual(self.transport.paths()[sent_before:], ["/sys/lease/info"])

    def test_expiry_bad_or_displaced_notification_fails_closed_even_if_free(self):
        for tags, token, reason in [("ss", "wrong", "expired"),
                                    ("ss", None, "displaced"),
                                    ("si", None, 1)]:
            with self.subTest(tags=tags, token=token, reason=reason):
                self.setUp()
                self.claim()
                self.transport.mode, self.transport.port, self.transport.owner = "free", 0, 0
                self.transport.put("/sys/lease/lost", tags, (token or self.b.token, reason))
                sent_before = len(self.transport.sent)
                with self.assertRaises(w.LeaseLost):
                    self.b.expiry_test()
                self.assertFalse(self.b.cleanup())
                with self.assertRaises(w.LeaseLost):
                    self.b.ring(0, 1)
                self.assertEqual(self.transport.paths()[sent_before:], [])
                self.assertFalse(any(line.startswith(("FREE_AFTER_NO_RENEW",
                                                     "EXPIRY_NOTIFICATION_AND_FREE"))
                                     for line in self.log))

    def test_renew_rejected_no_lease_cannot_resurrect(self):
        self.claim()
        def hook(t, path, tags, args):
            if path == "/sys/lease/renew":
                t.put("/sys/lease/rejected", "ss", (t.token, "no_lease"))
                return True
        self.transport.hook = hook
        with self.assertRaisesRegex(w.LeaseLost, "no_lease"):
            self.b.renew()
        self.assertFalse(self.b.cleanup())
        self.no_release()

    def test_malformed_state_disables_cleanup(self):
        self.claim()
        def hook(t, path, tags, args):
            if path == "/sys/lease/info":
                t.put("/sys/lease/state", "isssisii",
                      (2, "ARC-X", "free", "127.0.0.1", 0, "/x", 0, 0))
                return True
        self.transport.hook = hook
        with self.assertRaises(w.WorkbenchError):
            self.b.ring(0, 1)
        self.assertFalse(self.b.cleanup())
        self.no_release()

    def test_failed_claim_readback_no_cleanup_without_proof(self):
        def hook(t, path, tags, args):
            if path == "/sys/lease/info" and t.mode == "leased":
                return True
        self.transport.hook = hook
        with self.assertRaises(w.NoReply):
            self.claim()
        self.assertFalse(self.b.cleanup())
        self.no_release()

    def test_wrong_release_token_does_not_pass_as_success(self):
        self.claim()
        def hook(t, path, tags, args):
            if path == "/sys/lease/release":
                t.mode, t.owner, t.port = "free", 0, 0
                t.put("/sys/lease/released", "s", ("wrong",))
                return True
        self.transport.hook = hook
        with self.assertRaises(w.LeaseLost):
            self.b.release()
        self.assertFalse(self.b.cleanup())

    def test_release_device_error_then_free_readback(self):
        self.claim()
        def hook(t, path, tags, args):
            if path == "/sys/lease/release":
                t.mode, t.owner, t.port = "free", 0, 0
                t.put("/sys/lease/rejected", "ss", (t.token, "device_error"))
                return True
        self.transport.hook = hook
        self.assertEqual(self.b.release().mode, "free")
        self.assertFalse(self.b.acknowledged)

    def test_transport_error_cleanup_does_not_emit_output(self):
        self.claim()
        with mock.patch.object(self.transport, "send", side_effect=OSError("socket closed")):
            self.assertFalse(self.b.cleanup())
        self.no_release()
        self.assertNotIn(self.b.prefix + "/ring/all", self.transport.paths())

    def test_malformed_source_packet_disables_cleanup(self):
        self.claim()
        self.transport.queue.append((b"x" * 4100, self.transport.endpoint))
        with self.assertRaises(w.WorkbenchError):
            self.b.listen(.1)
        self.assertFalse(self.b.cleanup())
        self.no_release()

    def test_wrong_grant_ttl_or_types(self):
        for tags, value in [("si", 2999), ("ss", "3000")]:
            with self.subTest(tags=tags):
                self.setUp()
                def hook(t, path, actual_tags, args):
                    if path == "/sys/lease/acquire":
                        t.put("/sys/lease/granted", tags, (args[0], value))
                        return True
                self.transport.hook = hook
                with self.assertRaises(w.LeaseLost):
                    self.claim()
                self.no_release()

    def test_expiry_not_free_fails_without_release(self):
        self.claim()
        def hook(t, path, tags, args):
            if path == "/sys/lease/info" and self.b.lost:
                t.put("/sys/lease/state", "isssisii",
                      (1, "ARC-X", "legacy", "127.0.0.1", 54321, "/other", 0, 0))
                return True
        self.transport.hook = hook
        with self.assertRaisesRegex(w.WorkbenchError, "expiry not verified"):
            self.b.expiry_test()
        self.assertFalse(self.b.cleanup())
        self.no_release()

    def test_info_mismatch_partial_grid_conflict(self):
        for fields in [ [("/sys/id", "s", ("OTHER",))],
                        [("/sys/id", "i", (1,))],
                        [("/sys/id", "s", ("ARC-X",)), ("/sys/id", "s", ("OTHER",))],
                        [("/sys/id", "s", ("ARC-X",)), ("/sys/size", "ii", (8, 8)),
                         ("/sys/host", "s", ("127.0.0.1",)), ("/sys/port", "i", (0,)),
                         ("/sys/prefix", "s", ("/old",))] ]:
            self.transport.info_override = fields
            with self.assertRaises(w.WorkbenchError):
                self.b.acquire()
            self.assertNotIn("/sys/lease/acquire", self.transport.paths())

    def test_invalid_state_shapes_and_consistency(self):
        args = [1, "ARC-X", "free", "127.0.0.1", 0, "/x", 0, 0]
        for index, value in [(0, 2), (1, "OTHER"), (2, "other"), (3, ""),
                             (4, 65536), (4, 1), (5, "bad"), (6, -1), (6, 5), (7, 1), (7, 2)]:
            values = args.copy()
            values[index] = value
            with self.subTest(index=index, value=value), self.assertRaises(w.WorkbenchError):
                w.parse_state("isssisii", values, "ARC-X")
        with self.assertRaises(w.WorkbenchError):
            w.parse_state("ssssssss", args, "ARC-X")
        with self.assertRaises(w.WorkbenchError):
            w.parse_state("isssisii", args[:-1], "ARC-X")


class CLITests(unittest.TestCase):
    def test_defaults_help_offline(self):
        with mock.patch.object(w.socket, "socket", side_effect=AssertionError("network forbidden")):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(w.main([]), 0)
                self.assertEqual(w.main(["inspect"]), 0)
                with self.assertRaises(SystemExit) as result:
                    w.main(["--help"])
                self.assertEqual(result.exception.code, 0)

    def test_missing_selection_claim_expiry_confirmation(self):
        bad = [["probe"], ["run", "--serial", "ARC-X", "--device-port", "43200", "--profile", "arc2"],
               ["expiry-test", "--serial", "ARC-X", "--device-port", "43200", "--profile", "arc2", "--acquire"]]
        with mock.patch.object(w.socket, "socket", side_effect=AssertionError("network forbidden")):
            for argv in bad:
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    w.main(argv)

    def test_invalid_options_offline(self):
        base = ["run", "--serial", "ARC-X", "--device-port", "43200", "--profile", "arc2", "--acquire"]
        options = [["--host", "10.0.0.1"], ["--host", "localhost"], ["--callback-host", "0.0.0.0"],
                   ["--callback-port", "-1"], ["--device-port", "0"], ["--seconds", "nan"],
                   ["--seconds", "31"], ["--ring", "2"], ["--level", "16"], ["--timeout", "nan"],
                   ["--ttl-ms", "999"], ["--ttl-ms", "11000"], ["--timeout", "1", "--ttl-ms", "1000"]]
        with mock.patch.object(w.socket, "socket", side_effect=AssertionError("network forbidden")):
            for option in options:
                with self.subTest(option=option), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(w.main(base + option), 2)


class UDPServer:
    """Real UDP framing backed by the injected daemon; ephemeral loopback only."""
    def __enter__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.endpoint = self.sock.getsockname()
        assert self.endpoint[1] != 12002
        self.sock.settimeout(.05)
        self.stop = threading.Event()
        self.errors = []
        self.fake = FakeTransport(Clock())
        self.fake.endpoint = self.endpoint
        self.thread = threading.Thread(target=self.run)
        self.thread.start()
        return self

    def run(self):
        while not self.stop.is_set():
            try:
                packet, callback = self.sock.recvfrom(w.MAX_PACKET + 1)
                self.fake.callback = callback
                self.fake.send(self.endpoint, w.decode_message(packet))
                while self.fake.queue:
                    data, _ = self.fake.queue.pop(0)
                    self.sock.sendto(data, callback)
            except socket.timeout:
                pass
            except Exception as exc:
                self.errors.append(exc)
                break

    def __exit__(self, *args):
        self.stop.set()
        self.thread.join(2)
        self.sock.close()
        if self.errors:
            raise self.errors[0]


class UDPIntegrationTests(unittest.TestCase):
    def test_real_udp_full_lifecycle(self):
        with UDPServer() as server:
            transport = w.UDPTransport()
            try:
                bench = w.ArcWorkbench(transport, "ARC-X", server.endpoint[1], "arc4", timeout=.05, report=lambda _: None)
                bench.acquire()
                bench.ring(3, 2)
                bench.renew()
                bench.release()
                self.assertEqual(server.fake.mode, "free")
                self.assertIn(bench.prefix + "/ring/all", server.fake.paths())
            finally:
                transport.close()

    def test_real_udp_no_reply(self):
        with UDPServer() as server:
            server.fake.silent.add("/sys/info")
            transport = w.UDPTransport()
            try:
                bench = w.ArcWorkbench(transport, "ARC-X", server.endpoint[1], "arc2", timeout=.02)
                with self.assertRaises(w.NoReply):
                    bench.probe()
            finally:
                transport.close()


if __name__ == "__main__":
    unittest.main()
