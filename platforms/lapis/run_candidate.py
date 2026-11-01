#!/usr/bin/env python3
"""Inert-by-default, checkout-local strict candidate launcher (Linux/Python 3)."""
import argparse
import contextlib
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import stat
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BINARIES = ("serialoscd", "serialosc-device", "serialosc-detector")
PROFILE_KEYS = {"vid", "pid", "serial", "interface", "surface"}


class Invalid(ValueError):
    pass


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise Invalid("duplicate JSON key: " + key)
        result[key] = value
    return result


def _regular(path, executable=False):
    info = path.lstat()
    if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
            or info.st_mode & 0o022 or info.st_nlink != 1):
        raise Invalid("expected owned, non-writable-by-others regular file: " + str(path))
    if executable and not os.access(path, os.X_OK):
        raise Invalid("not executable: " + str(path))
    return info


def _read_json(path):
    _regular(path)
    with path.open(encoding="utf-8") as stream:
        result = json.load(stream, object_pairs_hook=_pairs)
    if not isinstance(result, dict):
        raise Invalid("JSON object required: " + str(path))
    return result


def profile(path):
    result = _read_json(path)
    if set(result) != PROFILE_KEYS or any(not isinstance(v, str) for v in result.values()):
        raise Invalid("profile requires exactly string vid/pid/serial/interface/surface")
    for field, size in (("vid", 4), ("pid", 4), ("interface", 2)):
        if not re.fullmatch("[0-9a-fA-F]{%d}" % size, result[field]):
            raise Invalid(field + " must be exact hexadecimal (no 0x prefix)")
    if (not re.fullmatch(r"[A-Za-z0-9._-]{1,128}", result["serial"])
            or result["serial"] in (".", "..")):
        raise Invalid("invalid exact serial")
    if result["surface"] not in ("arc2", "arc4", "grid"):
        raise Invalid("surface must explicitly be arc2, arc4 or grid")
    return result


def environment(p):
    # Deliberate allowlist, not an inherited environment with a few overrides.
    # No HOME/XDG, loader injection, PYTHONPATH, legacy surface or USB defaults.
    return {
        "LAPIS_SERIALOSC_STRICT": "1",
        "LAPIS_SERIALOSC_USB_VID": p["vid"],
        "LAPIS_SERIALOSC_USB_PID": p["pid"],
        "LAPIS_SERIALOSC_USB_SERIAL": p["serial"],
        "LAPIS_SERIALOSC_USB_INTERFACE": p["interface"],
        "LAPIS_SERIALOSC_SURFACES": p["serial"] + ":" + p["surface"],
    }


def local_path(value, root=None):
    path = Path(os.path.abspath(value))
    root = (ROOT if root is None else root).resolve()
    try:
        parts = path.relative_to(root).parts
    except ValueError:
        raise Invalid("path must be inside checkout: " + str(path)) from None
    current = root
    for part in parts:
        current /= part
        if current.is_symlink():
            raise Invalid("symlinked path component: " + str(current))
        if current.exists() and current.stat().st_uid != os.getuid():
            raise Invalid("path is not owned by current user: " + str(current))
    return path


def manifest(bin_dir):
    result = _read_json(bin_dir / "lapis-candidate.json")
    if (set(result) != {"schema", "strict", "source_commit", "files"}
            or type(result["schema"]) is not int or result["schema"] != 1
            or result["strict"] is not True
            or not isinstance(result["source_commit"], str)
            or not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", result["source_commit"])
            or not isinstance(result["files"], dict)
            or set(result["files"]) != set(BINARIES)):
        raise Invalid("invalid schema-1 strict build manifest")
    for name in BINARIES:
        expected = result["files"][name]
        if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise Invalid("invalid SHA-256: " + name)
        path = bin_dir / name
        _regular(path, executable=True)
        with path.open("rb") as stream:
            actual = hashlib.file_digest(stream, "sha256").hexdigest()
        if actual != expected:
            raise Invalid("build hash mismatch: " + name)
    return result


def private_directory(path, create=False):
    if not path.exists():
        if create:
            path.mkdir(mode=0o700)  # Parent must already exist; never mkdir a HOME tree.
            path.chmod(0o700)  # Clear inherited setgid bits from shared checkouts.
        else:
            return
    info = path.lstat()
    if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid()
            or stat.S_IMODE(info.st_mode) != 0o700):
        raise Invalid("directory must be owned, real and mode 0700: " + str(path))


def check_state(state):
    private_directory(state)
    private_directory(state / "config")
    if (state / "config").exists():
        for path in (state / "config").iterdir():
            info = _regular(path)
            if stat.S_IMODE(info.st_mode) != 0o600:
                raise Invalid("config files must be mode 0600: " + str(path))
    receipt = state / "receipt.json"
    if receipt.exists() or receipt.is_symlink():
        old = _read_json(receipt)
        if old.get("status") in ("starting", "running", "cleanup-incomplete"):
            raise Invalid("previous activation not confirmed stopped; inspect receipt before manual recovery")


def atomic_json(path, value):
    if path.exists() or path.is_symlink():
        _regular(path)
    fd, temporary = tempfile.mkstemp(prefix="." + path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


@contextlib.contextmanager
def locks(state, bin_dir):
    # State lock is persistent; never unlink a flock inode. Also lock all three
    # executable inodes so a second state-dir cannot reuse this same build.
    descriptors = []
    try:
        lock_path = state / "launcher.lock"
        if lock_path.exists() or lock_path.is_symlink():
            _regular(lock_path)
        fd = os.open(lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        descriptors.append(fd)
        for name in BINARIES:
            descriptors.append(os.open(bin_dir / name, os.O_RDONLY | os.O_NOFOLLOW))
        for fd in descriptors:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                raise Invalid("candidate already locked; no process was signalled") from None
        yield tuple(descriptors)
    finally:
        for fd in reversed(descriptors):
            os.close(fd)


def leader_exited(child):
    # Keep leader waitable (unreaped) until group cleanup is complete. Its PID
    # cannot be recycled into an unrelated group while we may still signal it.
    return os.waitid(os.P_PID, child.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT) is not None


def group_live(pgid):
    # Linux procfs: ignore zombies, including our intentionally unreaped leader.
    # No killing based on a process name, persisted PID or command-line match.
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / "stat").read_text().rsplit(")", 1)[1].split()
            if int(fields[2]) == pgid and fields[0] not in ("Z", "X"):
                return True
        except (FileNotFoundError, ProcessLookupError):
            pass
        except (PermissionError, IndexError, ValueError):
            # Unknown visibility: fail conservatively and use bounded cleanup.
            return True
    return False


def signal_group(pgid, signum):
    try:
        os.killpg(pgid, signum)
    except ProcessLookupError:
        pass


def wait_group(child, seconds):
    deadline = time.monotonic() + seconds
    while group_live(child.pid):
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.02)
    return True


def supervise(child, requested, grace, term_grace):
    while not requested and not leader_exited(child):
        time.sleep(0.02)
    # First give the supervisor a chance to perform its own orderly shutdown.
    # On spontaneous supervisor exit, clean up only its remaining group members.
    if not leader_exited(child):
        os.kill(child.pid, requested[0])
    elif group_live(child.pid):
        signal_group(child.pid, signal.SIGINT)
    escalated = []
    if not wait_group(child, grace):
        escalated.append("SIGTERM")
        signal_group(child.pid, signal.SIGTERM)
        if not wait_group(child, term_grace):
            escalated.append("SIGKILL")
            signal_group(child.pid, signal.SIGKILL)
            wait_group(child, term_grace)
    complete = not group_live(child.pid)
    # Never block in wait() on an uninterruptible kernel serial close.
    result = child.wait() if leader_exited(child) else None
    return complete, result, escalated


def activate(args, p, build, bin_dir, state):
    if not sys.platform.startswith("linux") or not Path("/proc/self/stat").exists():
        raise Invalid("activation requires Linux procfs")
    private_directory(state, create=True)
    with locks(state, bin_dir) as lock_fds:
        check_state(state)
        # Recheck after locking. Owner-controlled files must remain unchanged
        # throughout activation; this is integrity checking, not code signing.
        if manifest(bin_dir) != build:
            raise Invalid("manifest changed during validation")
        private_directory(state / "config", create=True)
        atomic_json(state / "profile.json", p)
        atomic_json(state / "candidate-manifest.json", build)
        receipt = {"schema": 1, "status": "starting", "source_commit": build["source_commit"],
                   "launcher_pid": os.getpid(), "started_at": time.time()}
        atomic_json(state / "receipt.json", receipt)
        requested = []
        def handle(signum, _frame):
            if not requested:
                requested.append(signum)
        previous = {s: signal.signal(s, handle) for s in (signal.SIGINT, signal.SIGTERM)}
        child = None
        try:
            child = subprocess.Popen(
                [str(bin_dir / "serialoscd"), "--config-dir", str(state / "config")],
                cwd=bin_dir, env=environment(p), stdin=subprocess.DEVNULL,
                start_new_session=True, pass_fds=lock_fds, umask=0o077)
            receipt.update(status="running", child_pid=child.pid, child_pgid=child.pid)
            atomic_json(state / "receipt.json", receipt)
            complete, code, escalated = supervise(child, requested, args.grace, args.term_grace)
            receipt.update(status="stopped" if complete else "cleanup-incomplete",
                           returncode=code, escalated=escalated, stopped_at=time.time())
            atomic_json(state / "receipt.json", receipt)
            if not complete:
                return 1
            return 128 + requested[0] if requested else (code if code is not None and code >= 0 else 1)
        except BaseException:
            if child is not None:
                complete, code, escalated = supervise(child, [signal.SIGINT], args.grace, args.term_grace)
                receipt.update(status="stopped" if complete else "cleanup-incomplete",
                               returncode=code, escalated=escalated, stopped_at=time.time())
            else:
                receipt.update(status="spawn-failed", stopped_at=time.time())
            atomic_json(state / "receipt.json", receipt)
            raise
        finally:
            for signum, handler in previous.items():
                signal.signal(signum, handler)


def bounded_seconds(value):
    number = float(value)
    if not math.isfinite(number) or not 0.05 <= number <= 30:
        raise argparse.ArgumentTypeError("must be finite and between 0.05 and 30 seconds")
    return number


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, type=Path)
    parser.add_argument("--bin-dir", required=True)
    parser.add_argument("--state-dir", required=True)
    parser.add_argument("--activate", action="store_true")
    parser.add_argument("--acknowledge-device-io", action="store_true")
    parser.add_argument("--grace", type=bounded_seconds, default=3.0)
    parser.add_argument("--term-grace", type=bounded_seconds, default=1.0)
    args = parser.parse_args(argv)
    try:
        if args.activate != args.acknowledge_device_io:
            raise Invalid("real activation requires both --activate and --acknowledge-device-io")
        bin_dir, state = local_path(args.bin_dir), local_path(args.state_dir)
        if not bin_dir.is_dir() or not state.parent.is_dir() or state == ROOT:
            raise Invalid("existing bin directory and state parent inside checkout required")
        if bin_dir == state or bin_dir in state.parents or state in bin_dir.parents:
            raise Invalid("state and bin directories must be separate non-nested trees")
        p, build = profile(args.profile), manifest(bin_dir)
        check_state(state)
        if not args.activate:
            print("Validated strict candidate only; no directories created, process spawned, ports bound or devices opened.")
            return 0
        return activate(args, p, build, bin_dir, state)
    except (Invalid, OSError, ValueError) as exc:
        print("candidate refused: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
