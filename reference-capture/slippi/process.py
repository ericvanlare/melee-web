# SPDX-License-Identifier: MIT
"""Bounded ownership of local Slippi test processes.

Each child is started in its own process group. A client gets SIGINT first so
Dolphin can run its normal cleanup; the local service gets SIGTERM. On timeout,
the supervisor escalates only within the process group it created and waits for
the owned leader to exit.
"""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import signal
import subprocess
import time
from typing import Mapping, Sequence


@dataclass
class OwnedProcess:
    name: str
    process: subprocess.Popen[bytes]
    log_path: Path
    _log_stream: object
    graceful_signal: int

    @property
    def pid(self) -> int:
        return self.process.pid


class ProcessSupervisor:
    """Start and stop only the child processes owned by one local run."""

    def __init__(self, *, graceful_timeout: float = 8.0, term_timeout: float = 2.0):
        if graceful_timeout <= 0 or term_timeout <= 0:
            raise ValueError("process shutdown deadlines must be positive")
        self.graceful_timeout = graceful_timeout
        self.term_timeout = term_timeout
        self._children: list[OwnedProcess] = []
        self._closed = False

    def start(
        self,
        name: str,
        command: Sequence[str],
        *,
        log_path: Path,
        env: Mapping[str, str] | None = None,
        graceful_signal: int = signal.SIGTERM,
    ) -> OwnedProcess:
        if self._closed:
            raise RuntimeError("process supervisor is closed")
        if (
            not name
            or not command
            or any(not isinstance(part, str) or not part for part in command)
        ):
            raise ValueError("a process name and a nonempty argument vector are required")
        log_path = Path(log_path)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        stream = log_path.open("ab", buffering=0)
        try:
            process = subprocess.Popen(
                list(command),
                stdin=subprocess.DEVNULL,
                stdout=stream,
                stderr=subprocess.STDOUT,
                env=None if env is None else dict(env),
                start_new_session=True,
                close_fds=True,
            )
        except BaseException:
            stream.close()
            raise
        child = OwnedProcess(name, process, log_path, stream, graceful_signal)
        self._children.append(child)
        return child

    def close(self) -> list[dict[str, int | str | bool]]:
        if self._closed:
            return []
        results = []
        errors = []
        for child in reversed(self._children):
            try:
                results.append(self._stop(child))
            except BaseException as error:
                errors.append(f"{child.name}: {error}")
            finally:
                if not child._log_stream.closed:
                    child._log_stream.close()
        if errors:
            raise RuntimeError("owned process cleanup failed: " + "; ".join(errors))
        self._closed = True
        return results

    def stop(self, child: OwnedProcess) -> dict[str, int | str | bool]:
        """Stop one owned child while keeping the remaining children managed."""
        if self._closed or child not in self._children:
            raise ValueError("process is not an active child of this supervisor")
        result = self._stop(child)
        self._children.remove(child)
        if not child._log_stream.closed:
            child._log_stream.close()
        return result

    def _stop(self, child: OwnedProcess) -> dict[str, int | str | bool]:
        process = child.process
        sent_graceful = process.poll() is None
        if sent_graceful:
            self._signal_group(process.pid, child.graceful_signal)
            try:
                process.wait(timeout=self.graceful_timeout)
            except subprocess.TimeoutExpired:
                self._signal_group(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=self.term_timeout)
                except subprocess.TimeoutExpired:
                    self._signal_group(process.pid, signal.SIGKILL)
                    process.wait(timeout=self.term_timeout)

        # A child may have exited while a descendant remains in its process
        # group. Close that group before declaring this owned process clean.
        if self._group_exists(process.pid):
            self._signal_group(process.pid, signal.SIGTERM)
            self._wait_group_gone(process.pid, min(self.term_timeout, 0.5))
        if self._group_exists(process.pid):
            self._signal_group(process.pid, signal.SIGKILL)
            self._wait_group_gone(process.pid, self.term_timeout)
        if self._group_exists(process.pid):
            raise RuntimeError(f"process group {process.pid} remained after SIGKILL")

        returncode = process.poll()
        if returncode is None:
            raise RuntimeError("owned leader did not exit")
        child._log_stream.close()
        return {
            "name": child.name,
            "pid": child.pid,
            "returncode": returncode,
            "graceful_signal_sent": sent_graceful,
            "process_group_released": True,
        }

    @staticmethod
    def _signal_group(process_group: int, signum: int) -> None:
        try:
            os.killpg(process_group, signum)
        except ProcessLookupError:
            pass

    @staticmethod
    def _group_exists(process_group: int) -> bool:
        try:
            os.killpg(process_group, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        return True

    @classmethod
    def _wait_group_gone(cls, process_group: int, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not cls._group_exists(process_group):
                return
            time.sleep(0.02)

    def __enter__(self) -> ProcessSupervisor:
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        self.close()
        return False
