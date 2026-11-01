"""Build/launcher contract checks. All inputs are temporary local files."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PLATFORM = ROOT / 'platforms/lapis'

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

class BuildContractTests(unittest.TestCase):
    def test_generated_manifest_passes_actual_launcher_validator(self):
        launcher = module('launcher_contract', PLATFORM / 'run_candidate.py')
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name in launcher.BINARIES:
                binary = folder / name
                binary.write_text('not executed\n')
                binary.chmod(0o700)
            subprocess.run([sys.executable, str(PLATFORM / 'write_candidate_manifest.py'),
                            '--bin-dir', str(folder), '--source-commit', 'a' * 40], check=True)
            receipt = launcher.manifest(folder)
            self.assertIs(receipt['strict'], True)
            self.assertEqual(receipt['source_commit'], 'a' * 40)

    def test_cmake_rejects_wrong_patch_baseline_without_copying(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'source/src/proto').mkdir(parents=True)
            (root / 'source/src/proto/mext.c').write_text('wrong source\n')
            script = root / 'test.cmake'
            script.write_text('include("' + str(PLATFORM / 'cmake/PrivateDependencies.cmake') + '")\n'
                + 'lapis_patched_dependency(libmonome "' + str(root / 'source') + '" ignored output)\n')
            result = subprocess.run(['cmake', '-P', str(script)], cwd=root,
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Unexpected libmonome baseline', result.stderr)
            self.assertFalse((root / 'lapis-private').exists())

if __name__ == '__main__':
    unittest.main()
