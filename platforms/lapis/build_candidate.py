#!/usr/bin/env python3
"""Build an inert strict Linux candidate with pinned Nix build dependencies.
No installation, daemon/probe execution, serial access or activation occurs.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

REV = '08d3a97586f6b6343b3b5e799980773629432610'
ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-lapis-candidate')
    parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Release')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    build = args.build_dir.absolute()
    if not 1 <= args.jobs <= 8:
        parser.error('jobs must be 1..8')
    # Keep launcher artifacts local and do not replace arbitrary existing files.
    if build == ROOT or ROOT not in build.resolve().parents or build.is_symlink():
        parser.error('build directory must be a non-symlink child of this checkout')
    if build.exists():
        cache = build / 'CMakeCache.txt'
        if not cache.is_file() or f'CMAKE_HOME_DIRECTORY:INTERNAL={ROOT}' not in cache.read_text():
            parser.error('existing build is not owned by this source checkout')
    outputs = subprocess.check_output([
        'nix', 'build', '--no-link', '--print-out-paths',
        f'github:NixOS/nixpkgs/{REV}#systemd.dev',
        f'github:NixOS/nixpkgs/{REV}#pkg-config'], text=True).splitlines()
    pkgconfig = next((Path(p) / 'bin/pkg-config' for p in outputs
                      if (Path(p) / 'bin/pkg-config').is_file()), None)
    pcdirs = [str(Path(p) / 'lib/pkgconfig') for p in outputs
              if (Path(p) / 'lib/pkgconfig/libudev.pc').is_file()]
    if pkgconfig is None or not pcdirs:
        parser.error('pinned Nix build dependencies incomplete')
    environment = dict(os.environ, PKG_CONFIG_PATH=os.pathsep.join(pcdirs))
    subprocess.run(['cmake', '-S', str(ROOT), '-B', str(build),
                    '-DCMAKE_BUILD_TYPE=' + args.configuration,
                    '-DSOSC_LAPIS_STRICT=ON', '-DPKG_CONFIG_EXECUTABLE=' + str(pkgconfig)],
                   env=environment, check=True)
    subprocess.run(['cmake', '--build', str(build), '--parallel', str(args.jobs)],
                   env=environment, check=True)
    subprocess.run(['ctest', '--test-dir', str(build), '--output-on-failure'],
                   env=environment, check=True)
    print(json.dumps({'build': str(build), 'hardware_accepted': False,
                      'started': False, 'nixpkgs': REV, 'dependencies': outputs}))

if __name__ == '__main__':
    main()
