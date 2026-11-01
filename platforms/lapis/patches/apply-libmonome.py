#!/usr/bin/env python3
"""Patch a NEW private copy only. Does not build, launch or open hardware."""
import argparse
import hashlib
import pathlib
import shutil
import subprocess
import sys

PIN = "9055ea22fd824839be821c10f6e9b13e9d2aa3cd"
# Baseline content checks avoid depending on .git being copied into packages.
HASHES = {'src/proto/mext.c': '6c25f8668db1920858cc9b794bba105e5a631bc1b5f130ca4a3239b0cc7391b5', 'src/platform/posix.c': 'a9af49a634b9b3db3e3e5afa3024d8bf2afb026131f658c50e3b328365a7d891', 'src/platform/linux.c': '6d8ac43722d3f656539ead18fb687fc87ca0c9802330bb41aa7225e709ec3149'}


def apply(source, destination):
    source, destination = pathlib.Path(source).resolve(), pathlib.Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError("destination must be a new private path")
    if source == destination or source in destination.parents:
        raise ValueError("destination must not be inside source")
    for name, expected in HASHES.items():
        actual = hashlib.sha256((source / name).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError("unexpected libmonome baseline: " + name)
    # No .git file that could point back at the real submodule's gitdir.
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns(".git"), symlinks=False)
    patch = pathlib.Path(__file__).with_name("libmonome-bounded-mext.patch")
    command = ["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)]
    try:
        subprocess.run(command + ["--dry-run"], cwd=destination, check=True)
        subprocess.run(command, cwd=destination, check=True)
    except Exception:
        # Preserve private failure output for inspection, never touch source.
        raise
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("destination", type=pathlib.Path)
    args = parser.parse_args()
    try:
        result = apply(args.source, args.destination)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")
    print("Patched private source only:", result)
    print("Build with LAPIS_SERIALOSC_BOUNDED_MEXT; no build or hardware validation performed.")


if __name__ == "__main__":
    main()
