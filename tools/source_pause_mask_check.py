#!/usr/bin/env python3
"""Bounded source-only pause/mask control for the compiled snapshot probe.

The Node worker sends only ordinary raw PAD records.  This launcher validates
source/runtime identities, owns one child process group, and retains evidence
or failure output.  It performs no build and makes no browser or rollback claim.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

SUPERVISOR_RELATIVE = Path('reference-capture/slippi/process.py')
NODE_RELATIVE = Path('.deps/emsdk/node/24.19.0_64bit/bin/node')
SUPERVISOR_SHA256 = '5b769f0b3ed11fc087a3e0af6e83d1a23587103c4d461472886dc1325e8157aa'
CLEANUP_RESERVE_SECONDS = 5.0
SCHEMA = 'melee-web-source-pause-mask-check-v1'


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def require_file(path: Path, expected: str, label: str) -> str:
    path = path.expanduser()
    if path.is_symlink() or not path.is_file():
        raise RuntimeError(f'{label} is missing or symlinked: {path}')
    actual = sha(path)
    if actual != expected:
        raise RuntimeError(f'{label} SHA differs: {actual} != {expected}')
    return actual


def load_supervisor(source_root: Path):
    path = source_root / SUPERVISOR_RELATIVE
    require_file(path, SUPERVISOR_SHA256, 'ProcessSupervisor')
    spec = importlib.util.spec_from_file_location('melee_web_public_pause_supervisor', path)
    if spec is None or spec.loader is None:
        raise RuntimeError('cannot load ProcessSupervisor')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def run_group(supervisor_module, command: list[str], cwd: Path, log: Path, wait_seconds: float) -> dict:
    supervisor = supervisor_module.ProcessSupervisor(graceful_timeout=1.5, term_timeout=1.0)
    child = None
    cleanup = None
    wait_returncode = None
    timed_out = False
    stop_error = None
    close_error = None
    try:
        old_cwd = Path.cwd()
        try:
            # ProcessSupervisor.start intentionally has no cwd parameter.
            import os
            os.chdir(cwd)
            child = supervisor.start('source-public-pause-mask-check', command,
                                     log_path=log, graceful_signal=signal.SIGINT)
        finally:
            os.chdir(old_cwd)
        try:
            wait_returncode = child.process.wait(timeout=wait_seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
    finally:
        if child is not None:
            try:
                cleanup = supervisor.stop(child)
            except BaseException as error:
                stop_error = str(error)
        try:
            supervisor.close()
        except BaseException as error:
            close_error = str(error)
    pid_absent = False
    if child is not None:
        try:
            os.kill(child.pid, 0)
        except ProcessLookupError:
            pid_absent = True
    group_released = isinstance(cleanup, dict) and cleanup.get('process_group_released') is True
    cleanup_ok = (group_released and stop_error is None and close_error is None and
                  type(cleanup.get('pid')) is int and cleanup['pid'] > 0 and
                  child is not None and cleanup['pid'] == child.pid and pid_absent and
                  type(cleanup.get('returncode')) is int and
                  cleanup.get('returncode') == wait_returncode)
    return {'wait_returncode': wait_returncode, 'timed_out': timed_out, 'cleanup': cleanup,
            'stop_error': stop_error, 'close_error': close_error,
            'process_group_released': group_released, 'cleanup_succeeded': cleanup_ok,
            'log': str(log), 'wait_seconds': wait_seconds,
            'pid': child.pid if child else None, 'pid_absent': pid_absent}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source-probe', type=Path, required=True)
    parser.add_argument('--source-cmake', type=Path, required=True)
    parser.add_argument('--profile-helper', type=Path, required=True)
    parser.add_argument('--stage-kind-bridge', type=Path, required=True)
    parser.add_argument('--probe-sha256', required=True)
    parser.add_argument('--cmake-sha256', required=True)
    parser.add_argument('--profile-helper-sha256', required=True)
    parser.add_argument('--stage-kind-bridge-sha256', required=True)
    parser.add_argument('--runtime-sha256', required=True)
    parser.add_argument('--wasm-sha256', required=True)
    parser.add_argument('--worker-sha256', required=True)
    parser.add_argument('--node', type=Path)
    parser.add_argument('--node-sha256', required=True)
    parser.add_argument('--timeout', type=float, default=30.0)
    args = parser.parse_args(argv)
    started = time.monotonic()
    if not math.isfinite(args.timeout) or not CLEANUP_RESERVE_SECONDS < args.timeout <= 30.0:
        raise SystemExit('--timeout must be finite, >5, and <=30 seconds')
    root = args.source_root.expanduser().resolve()
    runtime = args.runtime.expanduser()
    wasm = runtime.with_suffix('.wasm')
    out = args.out.expanduser().resolve()
    if out.exists():
        raise SystemExit('output directory must be fresh')
    out.parent.mkdir(parents=True, exist_ok=True)
    report = {'schema': f'{SCHEMA}-launcher-v1', 'result': 'failed', 'started_at_utc': datetime.now(timezone.utc).isoformat(),
              'finished_at_utc': None, 'source_root': str(root), 'output': str(out),
              'node_sha256': args.node_sha256.lower(),
              'checker_sha256': sha(Path(__file__)),
              'scope': {'source_only': True, 'build_performed': False, 'browser_exercised': False}, 'failure': None}
    try:
        if time.monotonic() + CLEANUP_RESERVE_SECONDS >= started + args.timeout:
            raise RuntimeError('whole deadline exhausted before preflight')
        if len(args.probe_sha256) != 64 or any(c not in '0123456789abcdefABCDEF' for c in args.probe_sha256):
            raise RuntimeError('probe SHA must be 64 hex characters')
        for value, label in ((args.cmake_sha256, 'CMake'), (args.profile_helper_sha256, 'profile helper'),
                             (args.stage_kind_bridge_sha256, 'stage bridge'), (args.runtime_sha256, 'runtime'), (args.wasm_sha256, 'Wasm'),
                             (args.worker_sha256, 'worker')):
            if len(value) != 64 or any(c not in '0123456789abcdefABCDEF' for c in value):
                raise RuntimeError(f'{label} SHA must be 64 hex characters')
        source_identities = {
            'probe_sha256': require_file(args.source_probe, args.probe_sha256.lower(), 'source probe'),
            'cmake_sha256': require_file(args.source_cmake, args.cmake_sha256.lower(), 'source CMake'),
            'profile_helper_sha256': require_file(args.profile_helper, args.profile_helper_sha256.lower(), 'profile helper'),
            'stage_kind_bridge_sha256': require_file(args.stage_kind_bridge, args.stage_kind_bridge_sha256.lower(), 'stage bridge'),
        }
        require_file(runtime, args.runtime_sha256.lower(), 'runtime JS')
        require_file(wasm, args.wasm_sha256.lower(), 'runtime Wasm')
        assets = args.assets.expanduser()
        if assets.is_symlink() or not assets.is_dir():
            raise RuntimeError('assets directory is missing or symlinked')
        if len(args.node_sha256) != 64 or any(c not in '0123456789abcdefABCDEF' for c in args.node_sha256):
            raise RuntimeError('Node SHA must be 64 hex characters')
        node = (args.node or (root / NODE_RELATIVE)).expanduser()
        require_file(node, args.node_sha256.lower(), 'Node executable')
        runner = root / 'tools/source_pause_mask_runtime.mjs'
        report['worker_sha256'] = require_file(runner, args.worker_sha256.lower(), 'pause worker')
        supervisor = load_supervisor(root)
        out.mkdir()
        worker_out = out / 'worker'
        command = [str(node.resolve()), str(runner.resolve()), '--runtime', str(runtime.resolve()), '--assets', str(assets.resolve()), '--out', str(worker_out),
                   '--probe-sha256', args.probe_sha256.lower(), '--cmake-sha256', args.cmake_sha256.lower(),
                   '--profile-helper-sha256', args.profile_helper_sha256.lower(), '--stage-kind-bridge-sha256', args.stage_kind_bridge_sha256.lower(),
                   '--runtime-sha256', args.runtime_sha256.lower(), '--wasm-sha256', args.wasm_sha256.lower(), '--profile-offset', '0x1234']
        wait_seconds = started + args.timeout - time.monotonic() - CLEANUP_RESERVE_SECONDS
        if wait_seconds <= 0:
            raise RuntimeError('whole deadline exhausted before worker start')
        phase = run_group(supervisor, command, root, out / 'worker.process.log', wait_seconds)
        report['worker'] = phase
        if not phase['cleanup_succeeded']:
            raise RuntimeError(f'worker cleanup failed: {phase}')
        evidence = worker_out / 'evidence.json'; failure = worker_out / 'failure.json'
        if phase['wait_returncode'] == 0 and evidence.is_file():
            worker_report = json.loads(evidence.read_text())
            if worker_report.get('result') != 'passed' or worker_report.get('cleanup', {}).get('success') is not True:
                raise RuntimeError('worker evidence did not pass with source close')
            report['result'] = 'passed'
        elif phase['wait_returncode'] != 0 and failure.is_file():
            report['failure_evidence'] = str(failure)
            raise RuntimeError(f'worker exited {phase["wait_returncode"]}; retained failure evidence')
        else:
            raise RuntimeError('worker result/evidence contract failed')
        report['source'] = source_identities
        report['runtime'] = {'js_sha256': args.runtime_sha256.lower(), 'wasm_sha256': args.wasm_sha256.lower()}
    except BaseException as error:
        report['failure'] = str(error)
        report['result'] = 'failed'
    finally:
        report['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        report['elapsed_seconds'] = round(time.monotonic() - started, 3)
        if time.monotonic() > started + args.timeout:
            report['failure'] = f'{report.get("failure") or ""}; whole timeout expired'.strip('; ')
            report['result'] = 'failed'
        out.mkdir(exist_ok=True)
        (out / 'launcher-report.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    return 0 if report['result'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
