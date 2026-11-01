#!/usr/bin/env python3
"""Inert native-ABI IPC child: never imports a hardware library or opens a tty."""
import ctypes
import json
import os
from pathlib import Path
import signal
import sys
import time


class Payload(ctypes.Union):
    _fields_ = [("pointers", ctypes.c_void_p * 2), ("port", ctypes.c_uint16)]


class Message(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("payload", Payload), ("magic", ctypes.c_uint16)]


def log(event, **extra):
    line = json.dumps(dict(event=event, pid=os.getpid(), time=time.monotonic(), **extra)) + "\n"
    fd = os.open(os.environ["LAPIS_FIXTURE_LOG"], os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o600)
    try:
        os.write(fd, line.encode())
    finally:
        os.close(fd)


def connection(label):
    msg = Message(type=0, magic=0x505C)
    text = label.encode()
    return bytes(msg) + bytes(ctypes.c_size_t(len(text))) + text + bytes(ctypes.c_uint16(0x505C))


def detector_stop(signum, frame):
    log("detector-stop")
    if os.getenv("LAPIS_FIXTURE_LATE_CONNECTION"):
        os.write(1, connection("inert-late-device"))
        log("late-connection")
    time.sleep(0.05)
    log("detector-exit")
    sys.exit(0)


def worker_signal(signum, frame):
    log("worker-unexpected-signal", signal=signum)
    sys.exit(91)


if Path(sys.argv[0]).name == "serialosc-detector":
    signal.signal(signal.SIGTERM, detector_stop)
    log("detector-start")
    if not os.getenv("LAPIS_FIXTURE_NO_DEVICE"):
        os.write(1, connection("inert-device"))
    while True:
        signal.pause()
else:
    signal.signal(signal.SIGINT, worker_signal)
    signal.signal(signal.SIGTERM, worker_signal)
    log("worker-start", label=sys.argv[-1])
    header = b""
    while len(header) < ctypes.sizeof(Message):
        part = os.read(0, ctypes.sizeof(Message) - len(header))
        if not part:
            log("worker-unexpected-eof")
            sys.exit(92)
        header += part
    msg = Message.from_buffer_copy(header)
    if msg.type != 5 or msg.magic != 0x505C:
        log("worker-bad-ipc", type=msg.type, magic=msg.magic)
        sys.exit(93)
    log("worker-should-exit")
    if os.getenv("LAPIS_FIXTURE_STUCK_WORKER"):
        while True:
            signal.pause()
    time.sleep(0.30)
    # This is a software marker, not a claim about real hardware darkening.
    log("worker-cleanup-complete")
    sys.exit(0)
