#!/usr/bin/env python3
"""Bounded developer handoff using existing fixture builders, oracle and op1-debug.

Public: explicit source rebuild or explicitly verified prebuilt exercise.
Private: hash-verified, explicitly supplied private recipe/runner and output.
No downloads, guest writes, device access or implicit input substitution.
"""
import argparse
import difflib
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parent.parent
FIXTURE = REPO / 'tests/fixtures/diagnose_guest'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify(pins):
    for name, expected in pins.items():
        path = Path(name)
        if not path.is_absolute() or not path.is_file():
            raise ValueError('missing explicit absolute input: ' + name)
        if sha(path) != expected:
            raise ValueError('input identity mismatch: ' + name)


def command(argv, log, timeout=40, cwd=None, env=None):
    with log.open('w') as output:
        result = subprocess.run(list(map(str, argv)), cwd=cwd, stdout=output,
                                stderr=subprocess.STDOUT, timeout=timeout, env=env)
    if result.returncode:
        raise RuntimeError(f'command failed ({result.returncode}); see {log}')


def public(args, out, build_dir):
    builder = load_module('diagnose_builder', FIXTURE / 'build.py')
    expected = json.loads((FIXTURE / 'manifest.json').read_text())
    verify({str(REPO / 'tests/fixtures' / p): digest for p, digest in expected['sources'].items()})
    if args.rebuild:
        if not args.assembler or not args.linker:
            raise ValueError('--rebuild requires explicit --assembler and --linker')
        manifest = builder.build(out / 'source-build', args.assembler, args.linker)
        if manifest != expected:
            raise ValueError('source rebuild differs from pinned fixture identities')
        images = {name: out / 'source-build' / name / 'guest.ldr' for name in manifest['images']}
        build_mode = 'source-rebuild'
    else:
        manifest = expected
        images = {name: FIXTURE / (name + '.ldr') for name in manifest['images']}
        for name, source in builder.sources().items():
            d = out / 'source-build' / name
            d.mkdir(parents=True)
            (d / 'guest.s').write_text(source)
        build_mode = 'explicit-verified-prebuilt; source rebuild not claimed'
    verify({str(images[name]): pin['image_sha256'] for name, pin in manifest['images'].items()})
    (out / 'source-identities.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    source = builder.sources()
    for a, b in [('reference', 'defective'), ('defective', 'corrected')]:
        (out / (a + '-to-' + b + '.patch')).write_text(''.join(difflib.unified_diff(
            source[a].splitlines(True), source[b].splitlines(True), fromfile=a + '/guest.s', tofile=b + '/guest.s')))
    oracle = load_module('stream_oracle', REPO / 'tests/stream_guest_integration.py')
    control = load_module('debug_control', REPO / 'tests/debugger_integration.py')
    reports = {}
    for name, image in images.items():
        prefix = out / name
        argv = [build_dir / 'op1-stream-guest-test', image, prefix, 'work', '--rate', '44100']
        with tempfile.TemporaryDirectory(prefix='readiness-public-') as tmp:
            command(argv, prefix.with_suffix('.log'), cwd=tmp)
        report = json.loads(prefix.with_suffix('.json').read_text())
        if name != 'defective':
            oracle.check_capture(prefix, 'work', report, manifest['images'][name]['symbols'], 44100)
        else:
            try:
                oracle.check_capture(prefix, 'work', report, manifest['images'][name]['symbols'], 44100)
            except AssertionError as error:
                if str(error) != 'generation/index mismatch at fresh component 1':
                    raise
                reports['negative_oracle'] = str(error)
            else:
                raise AssertionError('intentional source defect unexpectedly passed independent oracle')
        reports[name] = {'guest_outcome': 'normal-completion', 'image_sha256': sha(image),
                         **{ext: sha(prefix.with_suffix(ext)) for ext in ['.pcm', '.csv', '.json']}}
    for ext in ['.pcm', '.csv', '.json']:
        if (out / 'reference').with_suffix(ext).read_bytes() != (out / 'corrected').with_suffix(ext).read_bytes():
            raise AssertionError('source-corrected rebuild differs: ' + ext)
    # Diagnose the actual defective source output before any DMA transfer.
    pc = manifest['images']['defective']['symbols']['start_dma']
    script, debug_log = out / 'diagnose.commands.jsonl', out / 'diagnose.debug.jsonl'
    script.write_text(''); debug_log.write_text('')
    control.dump(script, [dict(cmd='break', pc=pc), dict(cmd='continue'), dict(cmd='wait-stop')])
    prefix = out / 'defective-debug'
    argv = [build_dir / 'op1-debug', '--ldr', images['defective'], '--commands', script,
            '--log', debug_log, '--codec-mclk', '11289600', '--capture', prefix]
    with tempfile.TemporaryDirectory(prefix='readiness-diagnose-') as tmp, prefix.with_suffix('.log').open('w') as log:
        proc = subprocess.Popen(list(map(str, argv)), cwd=tmp, stdout=log, stderr=subprocess.STDOUT)
        try:
            client = control.Control(script, debug_log, proc)
            client.receive(lambda r: r.get('kind') == 'stop')
            state = client.stable()
            if state['pc'] != pc or state['reason'] != 'pc' or not state['targets'][0]['verified']:
                raise AssertionError('requested exact source boundary not reached')
            ram = client.command('ram', address=0x3000, bytes=16)
            ownership = client.command('ram', address=0x2000, bytes=256)
            if not ram['ok'] or not ownership['ok']:
                raise AssertionError('passive diagnostic reads failed')
            words = struct.unpack('<4I', bytes(ram['bytes']))
            box = struct.unpack('<64I', bytes(ownership['bytes']))
            if words != (0x00800000, 0x00a00000, 0x00810000, 0x00a10000):
                raise AssertionError('unexpected defect in producer RAM')
            if box[44:46] != (2, 2) or state['dma4']['work'] or state['dma4']['running']:
                raise AssertionError('diagnosis did not precede first DMA submission with READY buffers')
            reports['diagnosis'] = {'requested_pc': pc, 'actual_pc': state['pc'], 'supported_boundary_reached': True,
                                    'snapshot': state, 'buffer_words': list(words), 'owners': list(box[44:46]),
                                    'expected_first_right_word': 0x00c00000, 'actual_first_right_word': words[1],
                                    'source_error': 'right-channel tag literal 0x20, expected 0x40',
                                    'minimum_host_pause_ms': 50}
            client.command('remove', pc=pc); client.resume()
            if proc.wait(timeout=20):
                raise AssertionError('defective guest diagnostic session did not complete')
        finally:
            if proc.poll() is None:
                proc.kill(); proc.wait()
    for ext in ['.pcm', '.csv']:
        if prefix.with_suffix(ext).read_bytes() != (out / 'defective').with_suffix(ext).read_bytes():
            raise AssertionError('debug session altered the defective output: ' + ext)
    (out / 'diagnosis.json').write_text(json.dumps(reports, indent=2, sort_keys=True) + '\n')
    return {'profile': 'public', 'build_mode': build_mode, 'outcome': 'passed',
            'negative_guest': 'normal completion; independent oracle expected failure at component1',
            'corrected': 'source rebuild identity and complete PCM/CSV/JSON match reference',
            'debugger': 'actual source boundary reached; passive RAM identifies wrong producer tag',
            'results': reports}


def private(args, out, build_dir):
    if not args.recipe or not args.recipe.is_absolute():
        raise ValueError('private profile requires an explicit absolute --recipe')
    if args.recipe.resolve().is_relative_to(REPO) or out.is_relative_to(REPO):
        raise ValueError('private recipe and output must be outside the public repository')
    recipe = json.loads(args.recipe.read_text())
    if recipe.get('schema') != 1:
        raise ValueError('unsupported private recipe schema')
    verify(recipe['pins'])
    runner = Path(recipe['runner']); scene = Path(recipe['scene'])
    if not runner.is_absolute() or str(runner) not in recipe['pins'] or not scene.is_absolute():
        raise ValueError('private runner must have an absolute pinned identity; explicit scene required')
    argv = [sys.executable, runner, '--repo', REPO, '--scene', scene, '--output', out, '--build-dir', build_dir]
    if args.rebuild:
        gcc, binutils = Path(recipe['gcc']), Path(recipe['binutils'])
        pinned_paths = {Path(name).resolve() for name in recipe['pins']}
        for tool in [gcc, gcc.parent / 'cc1', binutils / 'gas/as-new', binutils / 'ld/ld-new']:
            if not tool.is_absolute() or tool.resolve() not in pinned_paths:
                raise ValueError('private rebuild tool lacks an explicit pinned identity: ' + str(tool))
        argv += ['--rebuild', '--gcc', gcc, '--binutils', binutils]
    command(argv, out / 'private-run.log', timeout=180)
    result = json.loads((out / 'private-result.json').read_text())
    if result.get('outcome') != 'passed':
        raise ValueError('private runner did not establish reference equality')
    return {'profile': 'private', 'outcome': 'passed', 'recipe_sha256': sha(args.recipe),
            'pins_verified': len(recipe['pins']), 'source_rebuild_requested': args.rebuild, 'result': result}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('profile', choices=['public', 'private'])
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--build-native', action='store_true')
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument('--rebuild', action='store_true')
    mode.add_argument('--prebuilt', action='store_true')
    p.add_argument('--assembler', type=Path); p.add_argument('--linker', type=Path)
    p.add_argument('--recipe', type=Path)
    args = p.parse_args()
    if not args.output.is_absolute():
        p.error('--output must be explicit and absolute')
    out, build_dir = args.output.resolve(), args.build_dir.resolve()
    if args.profile == 'private' and out.is_relative_to(REPO):
        p.error('private output cannot be in the public repository')
    if out.exists() and any(out.iterdir()):
        p.error('output must be new or empty; existing evidence is preserved')
    cache = build_dir / 'CMakeCache.txt'
    if not cache.is_file() or ('CMAKE_HOME_DIRECTORY:INTERNAL=' + str(REPO)) not in cache.read_text().splitlines():
        p.error('--build-dir must be an existing CMake configuration for this checkout')
    out.mkdir(parents=True, exist_ok=True)
    try:
        if args.build_native:
            if 'FETCHCONTENT_FULLY_DISCONNECTED:BOOL=ON' not in cache.read_text().splitlines():
                raise ValueError('--build-native requires FETCHCONTENT_FULLY_DISCONNECTED=ON; no dependency downloads')
            command(['cmake', '--build', build_dir, '--target', 'op1-debug', 'op1-stream-guest-test', '-j6'],
                    out / 'native-build.log', timeout=180, env={**os.environ, 'CARGO_NET_OFFLINE': 'true'})
        for name in ['op1-debug', 'op1-stream-guest-test']:
            if not (build_dir / name).is_file():
                raise ValueError('missing native tool; use --build-native: ' + name)
        result = public(args, out, build_dir) if args.profile == 'public' else private(args, out, build_dir)
        result['native_tools'] = {name: sha(build_dir / name) for name in ['op1-debug', 'op1-stream-guest-test']}
        (out / 'readiness.json').write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
        print(json.dumps({k: result[k] for k in ['profile', 'outcome']}, sort_keys=True))
    except (Exception, KeyboardInterrupt) as error:
        timed_out = isinstance(error, subprocess.TimeoutExpired)
        (out / 'readiness.json').write_text(json.dumps({'profile': args.profile,
            'outcome': 'host-timeout' if timed_out else 'failed', 'error': str(error)}, indent=2) + '\n')
        print(str(error), file=sys.stderr)
        return 124 if timed_out else 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
