#!/usr/bin/env python3
"""Standalone, fail-closed Arc lease-v1 workbench (Python standard library only).

Adapted concepts and OSC framing from test/osc_workbench.py (serialosc review)
and devices-leases/tools/live_serialosc_lease.py (lease audit); rewritten for
explicit endpoint/profile selection and testable ownership gating. See
ARC-WORKBENCH.md and the repository COPYRIGHT for provenance and limitations.

Copyright (c) 2010-2015 William Light <wrl@illest.net>
Copyright (c) 2026 Peter Kassel
Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.
THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import ipaddress
import json
import math
import secrets
import socket
import struct
import sys
import time

MAX_PACKET = 4096
MAX_STRING = 1024
MAX_ARGS = 128
PREFIX = "/lapis-arc"


class WorkbenchError(ValueError):
    pass


class NoReply(WorkbenchError):
    """Unsupported, unreachable, dropped UDP or wrong endpoint; NOT zero devices."""


class LeaseLost(WorkbenchError):
    pass


class LeaseRejected(LeaseLost):
    pass


def _string(value):
    if type(value) is not str or "\0" in value:
        raise WorkbenchError("invalid OSC string")
    raw = value.encode("utf-8")
    if len(raw) > MAX_STRING:
        raise WorkbenchError("OSC string too long")
    raw += b"\0"
    return raw + b"\0" * (-len(raw) % 4)


def encode_message(path, tags="", args=()):
    if not isinstance(path, str) or not path.startswith("/") or any(ord(c) < 33 or ord(c) > 126 or c in "*,?[]{}#" for c in path):
        raise WorkbenchError("literal OSC path required")
    args = tuple(args)
    if len(tags) != len(args) or len(args) > MAX_ARGS:
        raise WorkbenchError("OSC argument count")
    packet = _string(path) + _string("," + tags)
    for tag, value in zip(tags, args):
        if tag == "i" and type(value) is int and -(2**31) <= value < 2**31:
            packet += struct.pack(">i", value)
        elif tag == "s":
            packet += _string(value)
        else:
            raise WorkbenchError("unsupported OSC type or invalid int32")
    if len(packet) > MAX_PACKET:
        raise WorkbenchError("OSC packet too large")
    return packet


def decode_message(packet):
    if not packet or len(packet) > MAX_PACKET or len(packet) % 4:
        raise WorkbenchError("OSC packet size")
    def string(offset):
        end = packet.find(b"\0", offset)
        next_offset = (end + 4) & ~3
        if end < 0 or end - offset > MAX_STRING or next_offset > len(packet):
            raise WorkbenchError("OSC unterminated/oversize string")
        if any(packet[end:next_offset]):
            raise WorkbenchError("OSC nonzero padding")
        try:
            return packet[offset:end].decode("utf-8", "strict"), next_offset
        except UnicodeError as exc:
            raise WorkbenchError("OSC invalid UTF-8") from exc
    path, offset = string(0)
    tags, offset = string(offset)
    if not tags.startswith(",") or len(tags) - 1 > MAX_ARGS:
        raise WorkbenchError("OSC typetags")
    values = []
    for tag in tags[1:]:
        if tag == "s":
            value, offset = string(offset)
        elif tag == "i" and offset + 4 <= len(packet):
            value = struct.unpack_from(">i", packet, offset)[0]
            offset += 4
        else:
            raise WorkbenchError("OSC unsupported type or truncated integer")
        values.append(value)
    if offset != len(packet):
        raise WorkbenchError("OSC trailing bytes")
    # Re-encoding also checks literal paths; bundles and coercions are forbidden.
    if encode_message(path, tags[1:], values) != packet:
        raise WorkbenchError("noncanonical OSC")
    return path, tags[1:], tuple(values)


def loopback(host):
    try:
        address = ipaddress.ip_address(host)
    except ValueError as exc:
        raise WorkbenchError("numeric loopback IPv4 required; no DNS/discovery") from exc
    if address.version != 4 or not address.is_loopback:
        raise WorkbenchError("only loopback IPv4 is supported")
    return str(address)


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise WorkbenchError(f"{label} must be {low}..{high}")
    return value


class UDPTransport:
    def __init__(self, host="127.0.0.1", port=0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.bind((loopback(host), integer(port, 0, 65535, "callback port")))
        except BaseException:
            self.sock.close()
            raise
        self.callback = self.sock.getsockname()

    def send(self, endpoint, message):
        self.sock.sendto(encode_message(*message), endpoint)

    def receive(self, seconds):
        if seconds <= 0:
            return None
        self.sock.settimeout(seconds)
        try:
            return self.sock.recvfrom(MAX_PACKET + 1)
        except socket.timeout:
            return None

    def close(self):
        self.sock.close()


@dataclass(frozen=True)
class LeaseState:
    version: int
    serial: str
    mode: str
    host: str
    port: int
    prefix: str
    remaining_ms: int
    owner: int


def parse_state(tags, args, serial):
    if tags != "isssisii" or len(args) != 8:
        raise WorkbenchError("invalid lease state types/shape")
    state = LeaseState(*args)
    if state.version != 1 or state.serial != serial:
        raise WorkbenchError("unsupported lease version or serial mismatch")
    if state.mode not in ("free", "legacy", "leased") or not state.host:
        raise WorkbenchError("invalid lease mode/host")
    integer(state.port, 0, 65535, "lease port")
    integer(state.remaining_ms, 0, 60000, "remaining TTL")
    integer(state.owner, 0, 1, "owner")
    if not state.prefix.startswith("/"):
        raise WorkbenchError("invalid lease prefix")
    if (state.mode == "free") != (state.port == 0):
        raise WorkbenchError("inconsistent free port")
    if state.mode != "leased" and (state.remaining_ms or state.owner):
        raise WorkbenchError("inconsistent lease liveness")
    if state.mode == "leased" and not state.remaining_ms:
        raise WorkbenchError("zero TTL leased state")
    return state


class ArcWorkbench:
    """One explicitly selected endpoint; no discovery, fallback or legacy writes."""
    def __init__(self, transport, serial, port, profile, host="127.0.0.1",
                 timeout=.25, ttl_ms=3000, clock=time.monotonic, report=print):
        self.transport = transport
        self.endpoint = (loopback(host), integer(port, 1, 65535, "device port"))
        if not serial or len(serial.encode()) > 128 or "\0" in serial:
            raise WorkbenchError("explicit serial required")
        if profile not in ("arc2", "arc4"):
            raise WorkbenchError("explicit arc2 or arc4 supplied profile required")
        if not math.isfinite(timeout) or not .01 <= timeout <= 1:
            raise WorkbenchError("timeout must be .01..1 seconds")
        integer(ttl_ms, 1000, 10000, "TTL ms")
        if timeout * 2 + .1 >= ttl_ms / 1000:
            raise WorkbenchError("TTL too short for timeout safety margin")
        self.serial, self.rings = serial, int(profile[-1])
        self.timeout, self.ttl_ms = timeout, ttl_ms
        self.clock, self.report = clock, report
        self.token = "lapis-arc-" + secrets.token_hex(24)
        self.prefix = PREFIX + "/" + secrets.token_hex(8)
        self.deadline = 0.0
        self.acknowledged = False
        self.lost = False
        self.events = 0
        self.touched = set()
        self.output_packets = 0
        self.info = None

    def send(self, path, tags="", args=()):
        self.transport.send(self.endpoint, (path, tags, tuple(args)))

    def fail_lost(self, reason):
        self.lost = True
        self.acknowledged = False
        raise LeaseLost(reason)

    def receive(self, deadline, allow_expired=False):
        packet = self.transport.receive(max(0, deadline - self.clock()))
        if packet is None:
            return None
        data, peer = packet
        if peer != self.endpoint:
            self.fail_lost("wrong UDP source; ownership disabled")
        try:
            path, tags, args = decode_message(data)
        except WorkbenchError:
            self.lost = True
            raise
        if path == "/sys/lease/lost":
            if tags != "ss" or len(args) != 2 or args[0] != self.token:
                self.fail_lost("invalid lost notification/token")
            if allow_expired and args[1] == "expired":
                self.lost, self.acknowledged = True, False
                return (path, tags, args)
            self.fail_lost("lease lost: " + args[1])
        if path in (self.prefix + "/enc/delta", self.prefix + "/enc/key"):
            if tags != "ii" or len(args) != 2 or not 0 <= args[0] < self.rings:
                self.fail_lost("invalid encoder event shape/ring")
            if path.endswith("/key") and args[1] not in (0, 1):
                self.fail_lost("invalid encoder key state")
            if not self.acknowledged or self.lost or self.clock() >= self.deadline:
                self.fail_lost("event outside owned lease")
            self.events += 1
            if self.events <= 200:
                self.report("ENCODER " + json.dumps({"path": path, "args": args}))
            return ("event", "", ())
        return path, tags, args

    def wait(self, expected, tags, args):
        end = self.clock() + self.timeout
        for _ in range(512):
            message = self.receive(end)
            if message is None:
                raise NoReply(f"NO_REPLY_UNKNOWN waiting for {expected}; not zero devices")
            path, actual_tags, values = message
            if path == "event":
                continue
            if path == "/sys/lease/rejected" and actual_tags == "ss" and len(values) == 2 and values[0] == self.token:
                self.lost, self.acknowledged = True, False
                raise LeaseRejected("lease rejected: " + values[1])
            if (path, actual_tags, values) != (expected, tags, tuple(args)):
                self.fail_lost("unexpected reply, type, token or TTL: " + path)
            return
        self.fail_lost("reply flood")

    def query(self):
        self.send("/sys/lease/info", "ssi", (self.token, *self.transport.callback))
        end = self.clock() + self.timeout
        for _ in range(512):
            message = self.receive(end)
            if message is None:
                raise NoReply("NO_REPLY_UNKNOWN lease v1 unsupported/unreachable; not zero devices")
            path, tags, args = message
            if path == "event":
                continue
            if path != "/sys/lease/state":
                self.fail_lost("unexpected lease query reply: " + path)
            try:
                return parse_state(tags, args, self.serial)
            except WorkbenchError:
                self.lost, self.acknowledged = True, False
                raise
        self.fail_lost("query flood")

    def probe(self):
        self.send("/sys/info", "si", self.transport.callback)
        schemas = {"/sys/id": "s", "/sys/size": "ii", "/sys/host": "s",
                   "/sys/port": "i", "/sys/prefix": "s", "/sys/rotation": "i"}
        info = {}
        end = self.clock() + self.timeout
        for _ in range(512):
            message = self.receive(end)
            if message is None:
                break
            path, tags, args = message
            if path not in schemas or tags != schemas[path]:
                raise WorkbenchError("invalid /sys/info field/types")
            if path in info and info[path] != args:
                raise WorkbenchError("conflicting /sys/info fields")
            info[path] = args
        else:
            raise WorkbenchError("info flood")
        if not info:
            raise NoReply("NO_REPLY_UNKNOWN /sys/info; not zero devices")
        # Arc does not consistently send /sys/size or /sys/rotation. They may
        # contradict a supplied Arc profile, but never establish ring count.
        required = {"/sys/id", "/sys/host", "/sys/port", "/sys/prefix"}
        if not required <= info.keys() or info["/sys/id"] != (self.serial,):
            raise WorkbenchError("incomplete info or serial identity mismatch")
        if "/sys/size" in info and info["/sys/size"] != (0, 0):
            raise WorkbenchError("grid/ambiguous surface contradicts supplied Arc profile")
        integer(info["/sys/port"][0], 0, 65535, "info port")
        if not info["/sys/host"][0] or not info["/sys/prefix"][0].startswith("/"):
            raise WorkbenchError("invalid info host/prefix")
        self.info = info
        return self.query()

    def owned(self, state):
        return (state.mode == "leased" and state.owner == 1 and
                (state.host, state.port) == self.transport.callback and
                state.prefix == self.prefix and 0 < state.remaining_ms <= self.ttl_ms)

    def ensure_owned(self):
        if self.lost or not self.acknowledged or self.clock() + .05 >= self.deadline:
            self.fail_lost("no current acknowledged ownership; no output/cleanup")
        state = self.query()
        if not self.owned(state) or self.clock() + .05 >= self.deadline:
            self.fail_lost("ownership readback failed; no output/cleanup")
        self.deadline = min(self.deadline, self.clock() + state.remaining_ms / 1000)
        if self.clock() + .05 >= self.deadline:
            self.fail_lost("insufficient verified TTL margin")
        return state

    def acquire(self, takeover=False):
        if self.acknowledged or self.lost:
            raise WorkbenchError("session cannot be reacquired")
        state = self.probe()
        if state.mode == "leased":
            raise WorkbenchError("busy leased destination; will not take over")
        if state.mode == "legacy" and not takeover:
            raise WorkbenchError("legacy destination requires --takeover-legacy")
        started = self.clock()
        self.send("/sys/lease/takeover" if state.mode == "legacy" else "/sys/lease/acquire",
                  "ssisi", (self.token, *self.transport.callback, self.prefix, self.ttl_ms))
        self.wait("/sys/lease/granted", "si", (self.token, self.ttl_ms))
        self.deadline = started + self.ttl_ms / 1000
        self.acknowledged = True
        self.ensure_owned()
        self.report("LEASE_GRANTED verified")

    def renew(self):
        self.ensure_owned()
        old_deadline = self.deadline
        started = self.clock()
        self.send("/sys/lease/renew", "sisi", (self.token, self.ttl_ms, *self.transport.callback))
        try:
            self.wait("/sys/lease/renewed", "si", (self.token, self.ttl_ms))
        except NoReply:
            self.fail_lost("renew acknowledgement missing; output disabled")
        if self.clock() + .05 >= old_deadline:
            self.fail_lost("late renew acknowledgement; cannot resurrect lease")
        self.deadline = started + self.ttl_ms / 1000
        self.ensure_owned()
        self.report("LEASE_RENEWED verified")

    def ring(self, ring, level):
        integer(ring, 0, self.rings - 1, "ring")
        integer(level, 0, 15, "level")
        if self.output_packets >= 64:
            raise WorkbenchError("bounded output budget exhausted")
        self.ensure_owned()
        self.send(self.prefix + "/ring/all", "ii", (ring, level))
        self.output_packets += 1
        self.touched.add(ring)

    def release(self):
        # No restoration of a legacy destination and no guessed LED cleanup.
        # The daemon's token-checked release performs darkening atomically.
        self.ensure_owned()
        self.send("/sys/lease/release", "ssi", (self.token, *self.transport.callback))
        ack_error = None
        try:
            self.wait("/sys/lease/released", "s", (self.token,))
        except (NoReply, LeaseRejected) as exc:
            ack_error = exc
        self.acknowledged = False
        state = self.query()
        if state.mode != "free" or state.port != 0 or state.owner:
            self.fail_lost("release not verified free")
        self.report("LEASE_RELEASED free verified" + (" (ack absent/rejected; readback authoritative)" if ack_error else ""))
        return state

    def cleanup(self):
        if self.lost or not self.acknowledged:
            self.report("CLEANUP_SKIPPED ownership not proved; bounded daemon TTL remains")
            return False
        try:
            self.release()
            return True
        except (WorkbenchError, OSError) as exc:
            self.report("CLEANUP_UNVERIFIED " + str(exc))
            return False

    def listen(self, seconds, renew=True):
        if not math.isfinite(seconds) or not 0 <= seconds <= 30:
            raise WorkbenchError("listen duration must be 0..30 seconds")
        end = self.clock() + seconds
        next_renew = self.clock() + self.ttl_ms / 3000 if renew else end
        count = 0
        while self.clock() < end:
            if renew and self.clock() >= next_renew:
                self.renew()
                next_renew = self.clock() + self.ttl_ms / 3000
            if self.clock() + .05 >= self.deadline:
                self.fail_lost("local lease deadline reached")
            message = self.receive(min(end, next_renew, self.deadline - .05))
            if message is not None and message[0] != "event":
                self.fail_lost("unexpected packet while listening")
            count += 1
            if count > 10000:
                self.fail_lost("receive flood")

    def expiry_test(self):
        # Explicitly omit renew/release, rely on server darkening. Never output
        # or attempt cleanup after the local deadline or lost notification.
        end = self.deadline + self.timeout
        observed = False
        for _ in range(10000):
            if self.clock() >= end:
                break
            message = self.receive(end, allow_expired=True)
            if message is None:
                break
            if message[0] == "/sys/lease/lost":
                observed = True
                continue
            if message[0] != "event":
                self.fail_lost("unexpected expiry reply")
        self.lost, self.acknowledged = True, False
        state = self.query()
        if state.mode != "free" or state.port != 0 or state.owner:
            raise WorkbenchError("expiry not verified free")
        # Free state alone also follows an intervening legacy /sys/port 0;
        # only a token-qualified lost/expired packet adds expiry evidence.
        evidence = "EXPIRY_NOTIFICATION_AND_FREE" if observed else "FREE_AFTER_NO_RENEW"
        self.report(f"{evidence} free; lost_observed={observed}; physical darkness NOT observed")
        return state


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command")
    sub.add_parser("inspect", help="offline safety/usage summary (default)")
    for command in ("probe", "run", "expiry-test"):
        p = sub.add_parser(command)
        p.add_argument("--serial", required=True)
        p.add_argument("--device-port", type=int, required=True)
        p.add_argument("--profile", choices=("arc2", "arc4"), required=True,
                       help="user-supplied physical profile; NEVER inferred")
        p.add_argument("--host", default="127.0.0.1")
        p.add_argument("--callback-host", default="127.0.0.1")
        p.add_argument("--callback-port", type=int, default=0)
        p.add_argument("--timeout", type=float, default=.25)
        p.add_argument("--ttl-ms", type=int, default=3000)
        if command != "probe":
            p.add_argument("--acquire", action="store_true", required=True)
            p.add_argument("--takeover-legacy", action="store_true")
            p.add_argument("--ring", type=int, default=0)
            p.add_argument("--level", type=int, default=4)
            if command == "run":
                p.add_argument("--seconds", type=float, default=5)
            else:
                p.add_argument("--confirm-expiry-test", action="store_true", required=True)
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.command in (None, "inspect"):
        print("OFFLINE: no sockets opened; no discovery, serial/USB access or claim.\n"
              "Use probe with explicit --serial --device-port --profile arc2|arc4.\n"
              "run additionally requires --acquire; takeover and expiry are explicit.\n"
              "No response means unsupported/unreachable/unknown, NEVER zero devices.\n"
              "See ARC-WORKBENCH.md; --help and each command --help are offline.")
        return 0
    transport = None
    bench = None
    try:
        # Validate EVERYTHING before binding or transmitting.
        loopback(args.host)
        loopback(args.callback_host)
        integer(args.callback_port, 0, 65535, "callback port")
        if args.command != "probe":
            integer(args.ring, 0, int(args.profile[-1]) - 1, "ring")
            integer(args.level, 0, 15, "level")
        if args.command == "run" and (not math.isfinite(args.seconds) or not 0 <= args.seconds <= 30):
            raise WorkbenchError("seconds must be 0..30")
        # Construct against a placeholder first so invalid options are offline.
        bench = ArcWorkbench(None, args.serial, args.device_port, args.profile,
                             args.host, args.timeout, args.ttl_ms)
        transport = UDPTransport(args.callback_host, args.callback_port)
        bench.transport = transport
        if args.command == "probe":
            print(json.dumps({"supplied_profile": args.profile, "profile_verified": False,
                              "lease": asdict(bench.probe()), "info": bench.info}, sort_keys=True))
        else:
            try:
                bench.acquire(args.takeover_legacy)
                bench.ring(args.ring, args.level)
                if args.command == "expiry-test":
                    bench.expiry_test()
                else:
                    bench.listen(args.seconds)
                    bench.release()
            finally:
                if bench.acknowledged:
                    bench.cleanup()
        return 0
    except KeyboardInterrupt:
        print("INTERRUPTED; ownership-gated cleanup attempted; no physical guarantee", file=sys.stderr)
        return 130
    except (WorkbenchError, OSError) as exc:
        print("ERROR " + str(exc), file=sys.stderr)
        return 2
    finally:
        if transport is not None:
            transport.close()


if __name__ == "__main__":
    raise SystemExit(main())
