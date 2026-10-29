#!/usr/bin/env python3
"""Public workflow and synthetic privacy/identity rejection checks."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parent.parent


def main():
    build = Path(sys.argv[1]).resolve()
    artifacts = Path(sys.argv[2]).resolve()
    tool = REPO / 'tools/firmware_workflow.py'
    with tempfile.TemporaryDirectory(prefix='readiness-test-') as tmp:
        tmp = Path(tmp)
        out = tmp / 'public'
        public = [sys.executable, tool, 'public', '--output', out, '--build-dir', build]
        run = subprocess.run(public + ['--prebuilt'], capture_output=True, text=True, timeout=40)
        assert run.returncode == 0, run.stdout + run.stderr
        receipt = json.loads((out / 'readiness.json').read_text())
        diagnosis = receipt['results']['diagnosis']
        assert receipt['outcome'] == 'passed'
        assert receipt['build_mode'].startswith('explicit-verified-prebuilt')
        assert diagnosis['supported_boundary_reached']
        assert diagnosis['requested_pc'] == diagnosis['actual_pc']
        assert diagnosis['owners'] == [2, 2]
        assert diagnosis['actual_first_right_word'] == 0x00a00000
        assert diagnosis['expected_first_right_word'] == 0x00c00000
        results = receipt['results']
        assert results['reference']['image_sha256'] == results['corrected']['image_sha256']
        assert results['defective']['image_sha256'] != results['reference']['image_sha256']
        shutil.copytree(out, artifacts, dirs_exist_ok=True)

        # Synthetic private recipe only: never real private inputs in CI.
        source = tmp / 'synthetic-input'
        source.write_text('synthetic')
        runner = tmp / 'runner.py'
        runner.write_text('raise AssertionError("must not run")')
        recipe = tmp / 'recipe.json'
        definition = {'schema': 1, 'scene': str(tmp), 'runner': str(runner),
                      'pins': {str(source): '0' * 64}}
        recipe.write_text(json.dumps(definition))
        private = [sys.executable, tool, 'private', '--recipe', recipe, '--build-dir', build]
        result = subprocess.run(private + ['--output', tmp / 'rejected', '--prebuilt'],
                                capture_output=True, text=True, timeout=5)
        assert result.returncode == 1 and 'identity mismatch' in result.stderr

        definition.update(gcc=str(tmp / 'unpinned-gcc'), binutils=str(tmp / 'unpinned-binutils'))
        definition['pins'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [source, runner]}
        recipe.write_text(json.dumps(definition))
        result = subprocess.run(private + ['--output', tmp / 'unpinned-tools', '--rebuild'],
                                capture_output=True, text=True, timeout=5)
        assert result.returncode == 1 and 'tool lacks an explicit pinned identity' in result.stderr

        result = subprocess.run(private + ['--output', build / 'forbidden-private', '--prebuilt'],
                                capture_output=True, text=True, timeout=5)
        assert result.returncode == 2 and 'public repository' in result.stderr
        result = subprocess.run([sys.executable, tool, 'public', '--output', tmp / 'missing-tools',
                                 '--build-dir', build, '--rebuild'],
                                capture_output=True, text=True, timeout=5)
        assert result.returncode == 1 and 'explicit --assembler' in result.stderr
        result = subprocess.run(public + ['--prebuilt'], capture_output=True, text=True, timeout=5)
        assert result.returncode == 2 and 'existing evidence' in result.stderr
    print('source diagnosis/fix, capture equality and explicit identity/privacy checks passed')


if __name__ == '__main__':
    main()
