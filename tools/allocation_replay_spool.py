"""Lossless disk sequences for large diagnostic allocation replays."""
from __future__ import annotations

from collections.abc import Sequence
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import time


class JsonSpool(Sequence):
    """Append-only JSON values with a disk offset index and bounded RAM use."""

    def __init__(self, directory: Path, name: str, *, max_bytes=4 * 1024**3):
        self.path = directory / (name + '.jsonl')
        self.index_path = directory / (name + '.offsets')
        self._data = self.path.open('x+b')
        self._index = self.index_path.open('x+b')
        self._count = 0
        self._digest = hashlib.sha256(b'[')
        self._failed = False
        self._bytes = 0
        self._max_bytes = max_bytes
        self._free_budget = 0

    def _require_healthy(self):
        if self._failed:
            raise OSError('allocation replay spool is unusable after a storage failure')

    def append(self, value):
        self._require_healthy()
        encoded = json.dumps(value, sort_keys=True, separators=(',', ':')).encode()
        added = len(encoded) + 1 + 8
        if self._count % 4096 == 0:
            self._free_budget = shutil.disk_usage(self.path.parent).free - 2 * 1024**3
        if added > self._free_budget:
            raise OSError('allocation replay spool requires at least 2 GiB free disk space')
        if self._bytes + added > self._max_bytes:
            raise OSError('allocation replay spool exceeded its declared byte budget')
        try:
            self._data.seek(0, 2)
            offset = self._data.tell()
            if self._data.write(encoded + b'\n') != len(encoded) + 1:
                raise OSError('short replay spool data write')
            self._index.seek(0, 2)
            if self._index.write(struct.pack('<Q', offset)) != 8:
                raise OSError('short replay spool index write')
        except OSError:
            self._failed = True
            raise
        if self._count:
            self._digest.update(b',')
        self._digest.update(encoded)
        self._count += 1
        self._bytes += added
        self._free_budget -= added

    def __len__(self):
        return self._count

    def flush(self):
        self._require_healthy()
        try:
            self._data.flush()
            self._index.flush()
        except OSError:
            self._failed = True
            raise

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(len(self)))]
        if index < 0:
            index += self._count
        if not 0 <= index < self._count:
            raise IndexError(index)
        self.flush()
        self._index.seek(index * 8)
        offset, = struct.unpack('<Q', self._index.read(8))
        self._data.seek(offset)
        return json.loads(self._data.readline())

    def __iter__(self):
        self.flush()
        with self.path.open('rb') as stream:
            for _ in range(self._count):
                yield json.loads(stream.readline())

    def canonical_sha256(self):
        self._require_healthy()
        digest = self._digest.copy()
        digest.update(b']')
        return digest.hexdigest()

    def receipt(self):
        self.flush()
        digest = hashlib.sha256()
        with self.path.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(chunk)
        return {'path': str(self.path.resolve()), 'count': self._count,
                'max_bytes': self._max_bytes,
                'sha256': digest.hexdigest(),
                'canonical_array_sha256': self.canonical_sha256()}

    def close(self):
        try:
            self._data.close()
        finally:
            self._index.close()

    def __del__(self):
        for name in ('_data', '_index'):
            stream = getattr(self, name, None)
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass


def run_checked_file_model(runner, cwd, actions, outputs, directory: Path,
                           timeout=1800, max_bytes=4 * 1024**3):
    """Compare every checked-model output without materializing its history."""
    if len(actions) != len(outputs):
        return {'matches': False, 'mismatch_index': None,
                'error': 'native action and output counts differ'}
    directory.mkdir(parents=True, exist_ok=True)
    commands = directory / 'checked-commands.jsonl'
    result = directory / 'checked-outputs.jsonl'
    errors = directory / 'checked-stderr.log'
    command_bytes = 0
    with commands.open('x') as stream:
        for index, action in enumerate(actions):
            line = json.dumps(action['command'], sort_keys=True, separators=(',', ':')) + '\n'
            command_bytes += len(line.encode())
            if command_bytes > max_bytes:
                raise OSError('checked commands exceeded their declared byte budget')
            if index % 4096 == 0 and shutil.disk_usage(directory).free < 2 * 1024**3:
                raise OSError('checked commands require at least 2 GiB free disk space')
            stream.write(line)
    resource_error = None
    with commands.open('rb') as stdin, result.open('xb') as stdout, errors.open('xb') as stderr:
        process = subprocess.Popen(runner, cwd=cwd, stdin=stdin, stdout=stdout, stderr=stderr)
        deadline = time.monotonic() + timeout
        next_check = 0
        try:
            while process.poll() is None:
                now = time.monotonic()
                if now >= deadline:
                    resource_error = 'checked model exceeded declared timeout'
                    break
                if now >= next_check:
                    if result.stat().st_size + errors.stat().st_size > max_bytes:
                        resource_error = 'checked model exceeded declared output byte budget'
                        break
                    if shutil.disk_usage(directory).free < 2 * 1024**3:
                        resource_error = 'checked model requires at least 2 GiB free disk space'
                        break
                    next_check = now + 1
                time.sleep(min(0.05, max(0, deadline - now)))
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    if not resource_error and result.stat().st_size + errors.stat().st_size > max_bytes:
        resource_error = 'checked model exceeded declared output byte budget'
    if resource_error:
        return {'matches': False, 'mismatch_index': None,
                'error': resource_error, 'stderr': str(errors)}
    if process.returncode:
        return {'matches': False, 'mismatch_index': None,
                'error': f'checked model exited {process.returncode}', 'stderr': str(errors)}
    with result.open() as stream:
        for index, expected in enumerate(outputs):
            line = stream.readline()
            try:
                actual = json.loads(line)
            except ValueError:
                return {'matches': False, 'mismatch_index': index,
                        'error': 'checked model output is missing or invalid JSON'}
            if actual != expected:
                return {'matches': False, 'mismatch_index': index, 'error': None}
        if stream.read(1):
            return {'matches': False, 'mismatch_index': len(outputs),
                    'error': 'checked model emitted extra output'}
    return {'matches': True, 'mismatch_index': None, 'error': None}
