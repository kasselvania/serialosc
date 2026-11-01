#!/usr/bin/env python3
"""Software-only compiler/tests, temp filesystem + fake transport, no tty opens."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("apply_libmonome", HERE.parent / "patches/apply-libmonome.py")
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)

def run(*args):
    subprocess.run([str(x) for x in args], check=True, timeout=30)

def main():
    source = REPO / "third-party/libmonome"
    before = {n: hashlib.sha256((source/n).read_bytes()).hexdigest() for n in patcher.HASHES}
    with tempfile.TemporaryDirectory(prefix="lapis-usb-policy-") as tmp:
        tmp = Path(tmp)
        private = patcher.apply(source, tmp/"private-libmonome")
        try:
            patcher.apply(source, private)
            raise AssertionError("existing destination accepted")
        except ValueError:
            pass
        altered = tmp/"altered"
        import shutil
        shutil.copytree(source, altered, ignore=shutil.ignore_patterns(".git"))
        (altered/"src/proto/mext.c").write_text("wrong baseline")
        try:
            patcher.apply(altered, tmp/"must-not-exist")
            raise AssertionError("wrong baseline accepted")
        except ValueError:
            assert not (tmp/"must-not-exist").exists()
        cc = os.environ.get("CC", "cc")
        for flags in [[], ["-DNDEBUG", "-O2"]]:
            run(cc, "-std=c17", "-Wall", "-Wextra", "-Werror", *flags, "-I"+str(REPO/"include"),
                REPO/"src/common/lapis_usb_policy.c", REPO/"tests/lapis_usb_policy_test.c", "-o", tmp/"selector")
            run(tmp/"selector")
        run(cc, "-std=c17", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", "-I"+str(REPO/"include"),
            REPO/"src/common/lapis_usb_policy.c", REPO/"src/common/lapis_usb_linux.c", "-o", tmp/"sysfs.so")
        subprocess.run([sys.executable, str(HERE/"test_usb_sysfs.py")], check=True, timeout=30,
                       env={**os.environ, "LAPIS_USB_POLICY_LIBRARY": str(tmp/"sysfs.so")})
        includes = ["-I"+str(private/p) for p in ["public", "src/private", "src/proto", "src/platform"]]
        for test in ["mext_fake_test", "posix_io_fake_test", "linux_poll_fake_test"]:
            run(cc, "-std=gnu17", "-DEMBED_PROTOS", "-DLAPIS_SERIALOSC_BOUNDED_MEXT", *includes,
                HERE/(test+".c"), "-o", tmp/test)
            run(tmp/test)
        # Syntax compile both guarded and legacy source, no daemon execution.
        for defines in [[], ["-DLAPIS_SERIALOSC_BOUNDED_MEXT"]]:
            for path in ["src/proto/mext.c", "src/platform/posix.c", "src/platform/linux.c"]:
                run(cc, "-std=gnu17", "-DEMBED_PROTOS", *defines, *includes, "-c", private/path, "-o", tmp/"syntax.o")
    after = {n: hashlib.sha256((source/n).read_bytes()).hexdigest() for n in patcher.HASHES}
    assert before == after, "submodule source changed"
    print("all selector/sysfs/patch application/fake transport gates passed; no hardware acceptance claimed")

if __name__ == "__main__":
    main()
