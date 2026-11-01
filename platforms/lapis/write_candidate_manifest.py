#!/usr/bin/env python3
"""Hash a strict build. This records ownership/reproducibility, not certification."""
import argparse
import hashlib
import json
import os
import tempfile
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bin-dir', required=True, type=Path)
    p.add_argument('--source-commit', required=True)
    args = p.parse_args()
    files = {}
    for name in ('serialoscd', 'serialosc-device', 'serialosc-detector'):
        file = args.bin_dir / name
        if file.is_symlink() or not file.is_file():
            p.error(f'not a regular owned build file: {file}')
        # Compiler/linker inherits workspace umask; normalize owned artifact only.
        file.chmod(0o755)
        files[name] = hashlib.sha256(file.read_bytes()).hexdigest()
    receipt = dict(schema=1, strict=True, source_commit=args.source_commit,
                   files=files)
    fd, temporary = tempfile.mkstemp(prefix='.lapis-candidate.', dir=args.bin_dir)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as f:
            json.dump(receipt, f, indent=2)
            f.write('\n')
        os.replace(temporary, args.bin_dir / 'lapis-candidate.json')
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == '__main__':
    main()
