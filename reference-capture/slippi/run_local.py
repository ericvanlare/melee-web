# SPDX-License-Identifier: MIT
"""Run a private, loopback-only two-client Slippi match/rematch scenario.

All game control is sent through Dolphin's Pipe controller device. Source
MemoryWatcher data and Slippi's own replay files decide when a step completed.
The script never writes guest memory or uses production Slippi endpoints.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools"))

from process import OwnedProcess, ProcessSupervisor  # noqa: E402
from runtime import (CSS_MARIO_CHARACTER_KIND, ClientProfile, ControllerPipe, MemoryWatcher,
                     create_client_profile)  # noqa: E402
import slippi_format  # noqa: E402


LOCK_PATH = HERE / "client.lock.json"
DEFAULT_WORK = ROOT / "work" / "slippi-local-networking"
CLIENT_RELATIVE = Path("SlippiHeadless.app/Contents/MacOS/dolphin-emu-nogui")
MOD_RELATIVE = Path("SlippiHeadless.app/Contents/Resources/Sys/GameSettings/GALE01r2.ini")
SERVICE_RELATIVE = Path("matchmaker-build/slippi-local-matchmaker")
BUTTON_BITS = {"A": 0x0100}
SCENE_CHOOSER = 0x0001
SCENE_CSS = 0x0008
SCENE_SSS = 0x0108
SCENE_GAME = 0x0208
# Each isolated process uses local SI port 1. The peer assignment separately
# maps that controller to online player slots 1 and 2.
LOCAL_CONTROLLER_PORT = 1
SLIPPI_MARIO_CHARACTER_ID = 8
DIRECT_FIRST_MATCH_STAGES = (3, 8, 28, 31, 32, 2)
SLIPPI_UNFREEZE_INPUT_FRAME = 84
SCRIPTED_INPUT_START_FRAME = 110
SSS_ENTRY_SETTLE_FRAMES = 30
SSS_CURSOR_X_BOUND = 27.0
SSS_CURSOR_Y_BOUND = 19.0
SSS_CURSOR_MAX_STEP_PER_FRAME = (127 - 0x1E) * 0.03
SSS_CURSOR_SWEEP_X_FRAMES = math.ceil(
    2 * SSS_CURSOR_X_BOUND / SSS_CURSOR_MAX_STEP_PER_FRAME
)
SSS_CURSOR_SWEEP_Y_ROWS = math.ceil(
    2 * SSS_CURSOR_Y_BOUND / SSS_CURSOR_MAX_STEP_PER_FRAME
) + 1


def _gameplay_controller_port(name: str) -> int:
    if name in ("p1", "p2"):
        return 1
    raise ValueError(f"unknown local Slippi client: {name}")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _tree_sha256(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(_sha256(path)))
    return digest.hexdigest()


def _json_lines(path: Path) -> list[dict]:
    if not path.exists():
        return []
    records = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            records.append(value)
    return records


def _menu_state(watcher: MemoryWatcher) -> tuple[int, int, int] | None:
    value = watcher.values.get("804a04f0")
    if value is None:
        return None
    return ((value >> 24) & 0xFF, (value >> 16) & 0xFF, value & 0xFFFF)


def _menu_state_observation(watcher: MemoryWatcher) -> dict | None:
    value = watcher.values.get("804a04f0")
    state = _menu_state(watcher)
    if value is None or state is None:
        return None
    return {
        "menu_word_hex": f"{value:08x}",
        "submenu": state[0],
        "previous_submenu": state[1],
        "selected_option": state[2],
        "scene_kind": watcher.scene_kind(),
        "online_scene_code": watcher.online_scene_code(),
        "source_frame": watcher.values.get("80479d58"),
    }


def _mario_cursor_confirmed(state: dict) -> bool:
    """Whether the live CSS cursor has settled on Mario and released its token."""
    if state["held"] >= 0 or state["selected"] != CSS_MARIO_CHARACTER_KIND:
        return False
    x, y = state["cursor"]
    left, right, top, bottom = state["bounds"]
    return left < x < right and bottom < y < top


def _stage_is_valid_for_game(stage_id: int, game_number: int) -> bool:
    if game_number == 1:
        # The pinned Direct flow locks its initial map to the authored random
        # stage pool while submitting the connect code.
        return stage_id in DIRECT_FIRST_MATCH_STAGES
    if game_number == 2:
        return stage_id == 32
    return False


def _slippi_players_are_mario_mario(players: list[dict]) -> bool:
    return [player.get("character_id") for player in players] == [
        SLIPPI_MARIO_CHARACTER_ID, SLIPPI_MARIO_CHARACTER_ID,
    ]


def _game_frame_delta(previous: int, current: int) -> int:
    """Count progress when the source frame word resets at a new game scene."""
    return current - previous if current >= previous else current


def _ticket_accepted(rows: list[dict], uid: str) -> bool:
    return any(row.get("event") == "ticket_accepted" and row.get("uid") == uid
               for row in rows)


def _parse_lsof_output(output: str) -> list[dict[str, str]]:
    observations = []
    for row in output.splitlines()[1:]:
        fields = row.split()
        protocol_index = next((index for index, value in enumerate(fields)
                               if value in ("TCP", "UDP")), None)
        if protocol_index is None or protocol_index + 1 >= len(fields):
            continue
        protocol = fields[protocol_index]
        endpoint = fields[protocol_index + 1]
        local = endpoint.split("->", 1)[0]
        local_address = local.rsplit(":", 1)[0].strip("[]")
        if local_address != "127.0.0.1":
            raise RuntimeError("an owned client/service socket is not bound to loopback")
        remote = endpoint.split("->", 1)[1].split(" ", 1)[0] if "->" in endpoint else ""
        if remote:
            address = remote.rsplit(":", 1)[0].strip("[]")
            if address != "127.0.0.1":
                raise RuntimeError("an owned client/service opened a non-loopback network peer")
        observations.append({"protocol": protocol, "endpoint": endpoint})
    return observations


def _lsof_destinations(pid: int) -> list[dict[str, str]]:
    lsof = shutil.which("lsof")
    if not lsof:
        raise RuntimeError("lsof is required for local network destination checks")
    completed = subprocess.run(
        [lsof, "-nP", "-a", "-p", str(pid), "-i"],
        check=False,
        capture_output=True,
        text=True,
        timeout=5,
    )
    if completed.returncode not in (0, 1):
        raise RuntimeError("lsof could not inspect an owned process")
    return _parse_lsof_output(completed.stdout)


def _verify_pinned_client_source(lock: dict) -> dict:
    source = ROOT / ".deps" / "slippi-dolphin-local"
    rust = source / "Externals" / "SlippiRustExtensions"
    if not source.is_dir() or not rust.is_dir():
        raise FileNotFoundError("local pinned Slippi source checkout is missing")

    def git(path: Path, *args: str) -> str:
        result = subprocess.run(
            ["git", "-C", str(path), *args], check=False,
            capture_output=True, text=True, timeout=15,
        )
        if result.returncode:
            raise RuntimeError("could not verify pinned local Slippi source state")
        return result.stdout.rstrip()

    client_pin = lock["client"]["commit"]
    if git(source, "rev-parse", "HEAD") != client_pin:
        raise RuntimeError("local Slippi Dolphin source commit differs from client.lock.json")
    expected_submodules = {row["path"]: row["commit"] for row in lock["client"]["submodules"]}
    actual_submodules = {}
    for line in git(source, "submodule", "status", "--recursive").splitlines():
        if len(line) < 43 or line[0] != " ":
            raise RuntimeError("local Slippi source has an uninitialized or mismatched submodule")
        actual_submodules[line[42:].split(" ", 1)[0]] = line[1:41]
    if actual_submodules != expected_submodules:
        raise RuntimeError("recursive Slippi submodule pins differ from client.lock.json")

    version_source = source / "Source" / "Core" / "Common" / "Version.cpp"
    version_text = version_source.read_text(encoding="utf-8")
    version_match = re.search(
        r'^#define SLIPPI_REV_STR "([^\"]+)"\s*// netplay version$',
        version_text,
        re.MULTILINE,
    )
    if (version_match is None
            or version_match.group(1) != lock["client"]["slippi_semver"]):
        raise RuntimeError("pinned Slippi semantic version differs from client.lock.json")

    def verify_patch_tree(tree: Path, patch_relative: str, allowed_extra: set[str] | None = None):
        patch = HERE / patch_relative
        patch_paths = {
            line.split("a/", 1)[1].split(" b/", 1)[0]
            for line in patch.read_text(encoding="utf-8").splitlines()
            if line.startswith("diff --git a/") and " b/" in line
        }
        if not patch_paths:
            raise RuntimeError("pinned client patch has no auditable file list")
        check = subprocess.run(
            ["git", "-C", str(tree), "apply", "--reverse", "--check", str(patch)],
            check=False, capture_output=True, text=True, timeout=15,
        )
        if check.returncode:
            raise RuntimeError("downstream client patch is not applied to its pinned source")
        with tempfile.TemporaryDirectory(prefix="slippi-patch-index-") as scratch:
            env = os.environ.copy()
            env["GIT_INDEX_FILE"] = str(Path(scratch) / "index")

            def indexed_git(*args: str) -> str:
                result = subprocess.run(
                    ["git", "-C", str(tree), *args], check=False,
                    capture_output=True, text=True, timeout=15, env=env,
                )
                if result.returncode:
                    raise RuntimeError("could not compare the local client patch tree")
                return result.stdout.strip()

            indexed_git("read-tree", "HEAD")
            indexed_git("write-tree")
            applied = subprocess.run(
                ["git", "-C", str(tree), "apply", "--cached", str(patch)],
                check=False, capture_output=True, text=True, timeout=15, env=env,
            )
            if applied.returncode:
                raise RuntimeError("reviewed downstream client patch cannot reproduce its source tree")
            expected_tree = indexed_git("write-tree")
            indexed_git("read-tree", "HEAD")
            indexed_git("add", "--all", "--", ".")
            actual_tree = indexed_git("write-tree")
            if expected_tree != actual_tree:
                raise RuntimeError("local client source tree differs from the reviewed patch")
        status = git(tree, "status", "--porcelain", "--untracked-files=all")
        changed = set()
        for line in status.splitlines():
            if len(line) < 4 or line[0] != " ":
                raise RuntimeError("pinned client source contains staged or untracked changes")
            changed.add(line[3:])
        if changed != patch_paths | (allowed_extra or set()):
            raise RuntimeError("local source changes extend beyond the reviewed client patch")
        cached = subprocess.run(["git", "-C", str(tree), "diff", "--cached", "--quiet"],
                                check=False, timeout=15)
        if cached.returncode != 0:
            raise RuntimeError("pinned client source has staged changes")
        return sorted(patch_paths)

    dolphin_paths = verify_patch_tree(
        source, lock["patches"][0], {"Externals/SlippiRustExtensions"}
    )
    rust_paths = verify_patch_tree(rust, lock["patches"][1])
    return {
        "commit": client_pin,
        "slippi_semver": version_match.group(1),
        "recursive_submodule_pins_verified": True,
        "dolphin_patch_files": dolphin_paths,
        "rust_patch_files": rust_paths,
    }


def _verify_build_profile(lock: dict, source_checkout: dict) -> dict:
    dolphin_build = DEFAULT_WORK / "dolphin-build"
    matchmaker_build = DEFAULT_WORK / "matchmaker-build"
    dolphin_source = ROOT / ".deps" / "slippi-dolphin-local"
    matchmaker_source = HERE / "local_matchmaker"

    def read_cache(directory: Path) -> dict[str, str]:
        path = directory / "CMakeCache.txt"
        if not path.is_file():
            raise FileNotFoundError("pinned Slippi CMake build cache is missing")
        cache = {}
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line or line.lstrip().startswith("//") or "=" not in line or ":" not in line:
                continue
            key_type, value = line.split("=", 1)
            key = key_type.split(":", 1)[0]
            cache[key] = value
        return cache

    client_cache = read_cache(dolphin_build)
    server_cache = read_cache(matchmaker_build)
    profile = lock["build_profile"]
    if sys.platform != "darwin" or platform.machine() != "arm64":
        raise RuntimeError("local Slippi client build host is not macOS arm64")
    if Path(client_cache.get("CMAKE_HOME_DIRECTORY", "")).resolve() != dolphin_source.resolve():
        raise RuntimeError("Dolphin build cache is not configured from the patched local source")
    if Path(server_cache.get("CMAKE_HOME_DIRECTORY", "")).resolve() != matchmaker_source.resolve():
        raise RuntimeError("matchmaker build cache is not configured from this repository's service")
    for cache in (client_cache, server_cache):
        cmake_version = ".".join(cache.get(f"CMAKE_CACHE_{part}_VERSION", "")
                                 for part in ("MAJOR", "MINOR", "PATCH"))
        if cmake_version != profile["cmake"] or cache.get("CMAKE_GENERATOR") != "Ninja":
            raise RuntimeError("local Slippi build generator/version differs from client.lock.json")
        if not cache.get("CMAKE_MAKE_PROGRAM"):
            raise RuntimeError("local Slippi Ninja executable is absent from its CMake cache")
        ninja = subprocess.run([cache["CMAKE_MAKE_PROGRAM"], "--version"], check=False,
                               capture_output=True, text=True, timeout=10)
        expected_ninja = profile["generator"].split(" ", 1)[1]
        if ninja.returncode or ninja.stdout.strip() != expected_ninja:
            raise RuntimeError("local Ninja version differs from client.lock.json")
    if client_cache.get("CMAKE_BUILD_TYPE") != profile["configuration"]:
        raise RuntimeError("local Dolphin build type differs from client.lock.json")
    for key, expected in profile["compiler_flags"].items():
        if client_cache.get(key) != expected:
            raise RuntimeError("local Dolphin compiler flags differ from client.lock.json")
    for key, enabled in profile["cmake_options"].items():
        expected = "ON" if enabled else "OFF"
        if client_cache.get(key) != expected:
            raise RuntimeError("local Dolphin CMake options differ from client.lock.json")
    compiler_version = profile["compiler"].split("clang ", 1)[1].split(" ", 1)[0]
    for build in (dolphin_build, matchmaker_build):
        compiler_file = build / "CMakeFiles" / profile["cmake"] / "CMakeCXXCompiler.cmake"
        compiler_info = compiler_file.read_text(encoding="utf-8", errors="replace")
        if (f'set(CMAKE_CXX_COMPILER_ID "AppleClang")' not in compiler_info
                or f'set(CMAKE_CXX_COMPILER_VERSION "{compiler_version}.' not in compiler_info):
            raise RuntimeError("local Apple Clang compiler identity differs from client.lock.json")
    rust_toolchain = (ROOT / ".deps" / "slippi-dolphin-local" / "Externals"
                      / "SlippiRustExtensions" / "rust-toolchain.toml")
    if not rust_toolchain.is_file() or f'channel = "{profile["rust_toolchain"]}"' \
            not in rust_toolchain.read_text(encoding="utf-8"):
        raise RuntimeError("Slippi Rust toolchain differs from client.lock.json")
    server_source = Path(server_cache.get("SLIPPI_DOLPHIN_SOURCE", "")).resolve()
    try:
        server_source.relative_to(ROOT.resolve())
    except ValueError as error:
        raise RuntimeError("local matchmaker source is outside the pinned repository checkouts") from error
    source_head = subprocess.run(["git", "-C", str(server_source), "rev-parse", "HEAD"],
                                 check=False, capture_output=True, text=True, timeout=15)
    source_status = subprocess.run(
        ["git", "-C", str(server_source), "status", "--porcelain", "--untracked-files=all"],
        check=False, capture_output=True, text=True, timeout=15,
    )
    enet_status = subprocess.run(
        ["git", "-C", str(server_source), "submodule", "status", "Externals/enet/enet"],
        check=False, capture_output=True, text=True, timeout=15,
    )
    expected_enet = next(row["commit"] for row in lock["client"]["submodules"]
                         if row["path"] == "Externals/enet/enet")
    actual_enet = enet_status.stdout.rstrip()
    if (server_cache.get("CMAKE_BUILD_TYPE") != "Release"
            or source_head.returncode or source_head.stdout.strip() != lock["client"]["commit"]
            or source_status.returncode or source_status.stdout.strip()
            or enet_status.returncode or len(actual_enet) < 42
            or actual_enet[0] != " " or actual_enet[1:41] != expected_enet
            or actual_enet[42:].split(" ", 1)[0] != "Externals/enet/enet"):
        raise RuntimeError("local matchmaker build is not bound to the pinned ENet checkout")
    built_client = dolphin_build / "Binaries" / "dolphin-emu-nogui"
    bundled_client = DEFAULT_WORK / CLIENT_RELATIVE
    if not built_client.is_file() or not bundled_client.is_file():
        raise FileNotFoundError("pinned headless client build output or staged bundle is missing")
    if _sha256(built_client) != _sha256(bundled_client):
        raise RuntimeError("staged Slippi client differs from the pinned Dolphin build output")
    source_mtimes = [
        (dolphin_source / relative).stat().st_mtime_ns
        for relative in source_checkout["dolphin_patch_files"]
    ] + [
        (dolphin_source / "Externals" / "SlippiRustExtensions" / relative).stat().st_mtime_ns
        for relative in source_checkout["rust_patch_files"]
    ]
    if source_mtimes and max(source_mtimes) > built_client.stat().st_mtime_ns:
        raise RuntimeError("pinned Dolphin binary predates a local Slippi patch; rebuild it")
    service_binary = DEFAULT_WORK / SERVICE_RELATIVE
    service_sources = tuple(
        (matchmaker_source / relative).stat().st_mtime_ns
        for relative in (
            "CMakeLists.txt", "server.cpp", "pairing.cpp", "pairing.hpp",
            "protocol.cpp", "protocol.hpp",
        )
    )
    if max(service_sources) > service_binary.stat().st_mtime_ns:
        raise RuntimeError("local matchmaker binary predates service sources; rebuild it")
    return {
        "cmake": profile["cmake"],
        "generator": profile["generator"],
        "compiler": profile["compiler"],
        "rust_toolchain": profile["rust_toolchain"],
        "headless": profile["runtime_platform"] == "headless",
        "video_backend": profile["video_backend"],
        "audio_backend": profile["audio_backend"],
        "cmake_options_verified": True,
        "matchmaker_release_build_verified": True,
        "patched_build_source_verified": True,
        "bundled_client_matches_build_output": True,
        "matchmaker_source_freshness_verified": True,
    }


def _assert_no_owned_ports(ports: tuple[int, ...]) -> None:
    lsof = shutil.which("lsof")
    if not lsof:
        raise RuntimeError("lsof is required to verify released local ports")
    for port in ports:
        for protocol in ("TCP", "UDP"):
            result = subprocess.run(
                [lsof, "-nP", f"-i{protocol}:{port}"],
                check=False,
                capture_output=True,
                text=True,
                timeout=5,
            )
            if result.returncode not in (0, 1):
                raise RuntimeError("lsof could not verify local port release")
            if len(result.stdout.splitlines()) > 1:
                raise RuntimeError(f"local port {port} remains open after cleanup")


class PairRun:
    def __init__(self, *, root: Path, disc: Path, cycle: int, timeouts: dict[str, float],
                 pause_after_pair: bool = False, input_probe_only: bool = False):
        self.root = root
        self.disc = disc
        self.cycle = cycle
        self.timeouts = timeouts
        self.pause_after_pair = pause_after_pair
        self.input_probe_only = input_probe_only
        self.work = root / f"cycle-{cycle:02d}"
        self.work.mkdir(parents=True, mode=0o700, exist_ok=False)
        self.work.chmod(0o700)
        self.profile_root: Path | None = None
        self.supervisor = ProcessSupervisor(graceful_timeout=10, term_timeout=3)
        self.children: dict[str, OwnedProcess] = {}
        self.profiles: dict[str, ClientProfile] = {}
        self.watchers: dict[str, MemoryWatcher] = {}
        self.pads: dict[str, ControllerPipe] = {}
        self.assigned_pads: dict[str, ControllerPipe] = {}
        self.replay_baselines: dict[int, dict[str, set[Path]]] = {}
        self.network_observations: dict[str, list[dict[str, object]]] = {}
        self._run_started = time.monotonic()
        self._last_network_check = 0.0
        self.cleanup: list[dict] = []
        self.failure: str | None = None
        self.evidence = {
            "schema": "melee-web-local-slippi-run-v1",
            "cycle": cycle,
            "profile_policy": "fresh-private-profiles",
            "client": json.loads(LOCK_PATH.read_text(encoding="utf-8"))["client"],
            "game_modification_sha256": None,
            "disc_sha256": _sha256(disc),
            "clients": {},
            "pairing": [],
            "menu_observations": [],
            "direct_ticket_submissions": [],
            "games": [],
            "remote_input": {},
            "network_destinations": self.network_observations,
            "cleanup": self.cleanup,
            "result": "incomplete",
        }

    def run(self, *, disconnect_after_rematch: bool = False) -> dict:
        lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        mod = DEFAULT_WORK / MOD_RELATIVE
        service = DEFAULT_WORK / SERVICE_RELATIVE
        client = DEFAULT_WORK / CLIENT_RELATIVE
        if not client.is_file() or not os.access(client, os.X_OK):
            raise FileNotFoundError("pinned headless Slippi client is not built; follow setup documentation")
        if not service.is_file() or not os.access(service, os.X_OK):
            raise FileNotFoundError("local matchmaker is not built; follow setup documentation")
        if not mod.is_file():
            raise FileNotFoundError("pinned Slippi game modification is missing from the client bundle")
        expected_mod = lock["game_modifications"]["sha256"]
        if _sha256(mod) != expected_mod:
            raise ValueError("bundled Slippi game modification does not match client.lock.json")
        expected_disc = lock["owned_disc"]["sha256"]
        if _sha256(self.disc) != expected_disc:
            raise ValueError("--disc hash does not match the pinned owned Melee image")
        sys_root = client.parents[1] / "Resources" / "Sys"
        source_sys_root = ROOT / ".deps" / "slippi-dolphin-local" / "Data" / "Sys"
        required_resources = ("codehandler.bin", "totaldb.dsy", "GameSettings/GALE01r2.ini")
        missing_resources = [relative for relative in required_resources
                             if not (sys_root / relative).is_file()]
        if missing_resources:
            raise FileNotFoundError("headless client bundle is missing required Sys resources")
        if _tree_sha256(sys_root) != _tree_sha256(source_sys_root):
            raise RuntimeError("bundled Sys resources differ from the pinned Dolphin source tree")
        self.evidence["game_modification_sha256"] = expected_mod
        self.evidence["disc_sha256"] = expected_disc
        self.evidence["bundle_sys_sha256"] = _tree_sha256(sys_root)
        self.evidence["dependency_lock_sha256"] = _sha256(LOCK_PATH)
        self.evidence["rust_extensions"] = lock["rust_extensions"]
        self.evidence["game_modifications"] = lock["game_modifications"]
        self.evidence["downstream_patch_sha256"] = {
            patch: _sha256(HERE / patch) for patch in lock["patches"]
        }
        source_checkout = _verify_pinned_client_source(lock)
        self.evidence["source_checkout"] = source_checkout
        self.evidence["build_profile"] = _verify_build_profile(lock, source_checkout)
        self.evidence["client_binary_sha256"] = _sha256(client)
        self.evidence["matchmaker_sha256"] = _sha256(service)

        try:
            self._make_profiles(mod, expected_mod, lock["client"]["slippi_semver"])
            self._start_service(service)
            self._start_clients(client)
            self._enter_direct_mode()
            # Original Direct mode only opens connect-code entry after a
            # character is selected. Each isolated client controls its local
            # CSS through SI port 1; its online player slot is assigned
            # separately by the actual peer connection.
            self.evidence["css_character_preselection"] = []
            for name in ("p1", "p2"):
                cursor_state = self._select_mario(name, profile_port=1)
                player = self.watchers[name].css_player(1)
                if player is None or player["character_kind"] != CSS_MARIO_CHARACTER_KIND:
                    raise RuntimeError(
                        f"{name} did not source-confirm pre-matchmaking Mario on CSS port 1; "
                        f"cursor={cursor_state}, slot={player}"
                    )
                self.evidence["css_character_preselection"].append({
                    "client": name,
                    "local_css_port": 1,
                    "character_kind": player["character_kind"],
                    "selected_icon_index": cursor_state["target"],
                    "selected_character_kind": cursor_state["selected"],
                    "selected_icon_bounds": cursor_state["bounds"],
                    "source_frame": self.watchers[name].values.get("80479d58"),
                })
            self.replay_baselines[1] = self._replay_file_snapshot()
            self._submit_direct_tickets()
            self._wait_paired()
            self._play_and_rematch()
            if self.input_probe_only:
                self.evidence["result"] = "input_probe_complete"
            elif disconnect_after_rematch:
                self._disconnect_peer()
                self.evidence["result"] = "passed"
            else:
                self.evidence["result"] = "passed"
        except BaseException as error:
            self.failure = f"{type(error).__name__}: {error}"
            self.evidence["failure"] = self.failure
            self.evidence["failure_source_state"] = {
                name: {
                    "scene_kind": watcher.scene_kind(),
                    "online_scene_code": watcher.online_scene_code(),
                    "source_frame": watcher.values.get("80479d58"),
                    "menu": _menu_state_observation(watcher),
                    "selected_stage": watcher.selected_stage(),
                }
                for name, watcher in self.watchers.items()
            }
            self.evidence["result"] = (
                "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
            )
            raise
        finally:
            for watcher in self.watchers.values():
                watcher.close()
            try:
                self.cleanup.extend(self.supervisor.close())
            except BaseException as cleanup_error:
                self.evidence["cleanup_error"] = f"{type(cleanup_error).__name__}: {cleanup_error}"
                self.evidence["result"] = "failed-cleanup"
            self._save_evidence()
        return self.evidence

    def _make_profiles(self, mod: Path, expected_mod: str, latest_version: str) -> None:
        self.profile_root = Path(tempfile.mkdtemp(prefix="slp-", dir="/tmp"))
        self.profile_root.chmod(0o700)
        self.evidence["private_profile_temp_id"] = self.profile_root.name
        for index, name in enumerate(("p1", "p2")):
            port = 41301 + (self.cycle - 1) * 4 + index
            profile = create_client_profile(
                root=self.profile_root / ("a" if name == "p1" else "b"),
                replay_root=self.work / f"{name}-replays",
                identity=f"local-cycle-{self.cycle}-{name}",
                play_key=f"local-cycle-{self.cycle}-{name}-key",
                display_name=f"Local {name.upper()}",
                connect_code=name.upper(),
                opponent_code="P2" if name == "p1" else "P1",
                peer_port=port,
                spectator_port=port + 20,
                game_modification=mod,
                expected_modification_sha256=expected_mod,
                latest_version=latest_version,
            )
            self.profiles[name] = profile
            self.watchers[name] = MemoryWatcher(profile)
            menu_pipe_log = self.work / f"{name}-menu-pad-pipe.jsonl"
            match_pipe_log = self.work / f"{name}-matchplay-pad-pipe.jsonl"
            self.pads[name] = ControllerPipe(
                profile.pads[0], menu_pipe_log, f"{name}-menu"
            )
            gameplay_port = _gameplay_controller_port(name)
            self.assigned_pads[name] = ControllerPipe(
                profile.pads[gameplay_port - 1], match_pipe_log, f"{name}-matchplay"
            )
            self.evidence["clients"][name] = {
                "identity": profile.identity,
                "connect_code": profile.connect_code,
                "peer_port": profile.peer_port,
                "spectator_port": port + 20,
                "profile_config_sha256": _tree_sha256(profile.user_root / "Config"),
                "local_profile_version": latest_version,
                "configuration": {
                    "headless_platform": "headless",
                    "video_backend": "Null",
                    "audio_backend": "No Audio Output",
                    "spectators_enabled": False,
                    "replays_enabled": True,
                    "menu_controller_pipe": "Pipe/0/pad1",
                    "matchplay_controller_pipe": f"Pipe/0/pad{gameplay_port}",
                    "online_player_slot": index + 1,
                    "peer_port": port,
                    "spectator_port": port + 20,
                    "slippi_online_logging": True,
                },
                "profile_is_fresh": True,
            }

    def _start_service(self, service: Path) -> None:
        event_log = self.work / "matchmaker-events.jsonl"
        self.children["service"] = self.supervisor.start(
            "local-matchmaker",
            [str(service), "--event-log", str(event_log), "--ticket-timeout-seconds", "45"],
            log_path=self.work / "matchmaker.log",
            graceful_signal=signal.SIGTERM,
        )
        self._wait_until(
            lambda: any(row.get("event") == "service_started" for row in _json_lines(event_log)),
            timeout=15,
            description="loopback matchmaking service startup",
        )

    def _start_clients(self, client: Path) -> None:
        for name in ("p1", "p2"):
            profile = self.profiles[name]
            environment = os.environ.copy()
            environment["SLIPPI_LOCAL_PAD_OBSERVATION"] = str(self.work / f"{name}-remote-pad.jsonl")
            environment["SLIPPI_LOCAL_SI_OBSERVATION"] = str(self.work / f"{name}-serial-input.jsonl")
            environment["SLIPPI_LOCAL_PAD_SENT_OBSERVATION"] = str(
                self.work / f"{name}-local-pad-sent.jsonl"
            )
            self.children[name] = self.supervisor.start(
                f"client-{name}",
                [str(client), "-p", "headless", "-u", str(profile.user_root),
                 "-v", "Null", "-e", str(self.disc)],
                log_path=self.work / f"{name}-dolphin.log",
                env=environment,
                graceful_signal=signal.SIGINT,
            )
            self.evidence["clients"][name]["process_id"] = self.children[name].pid
        self._wait_until(
            lambda: all(self.watchers[name].scene_kind() == 1
                        and self.watchers[name].online_scene_code() == SCENE_CHOOSER
                        and _menu_state(self.watchers[name]) is not None
                        and _menu_state(self.watchers[name])[0] == 8
                        for name in ("p1", "p2")),
            timeout=self.timeouts["boot"],
            description="fresh clients at the original online mode chooser",
        )
        for name in ("p1", "p2"):
            self._record_menu_observation(name, "chooser_ready")

    def _record_menu_observation(self, name: str, phase: str) -> dict | None:
        observation = _menu_state_observation(self.watchers[name])
        if observation is None:
            return None
        record = {"client": name, "phase": phase, **observation}
        self.evidence["menu_observations"].append(record)
        return record

    def _enter_direct_mode(self) -> None:
        for name in ("p1", "p2"):
            watcher = self.watchers[name]
            state = _menu_state(watcher)
            if state is None or state[2] != 1:
                observed = _menu_state_observation(watcher)
                raise RuntimeError(
                    f"{name} did not boot at default online option 1; observed {observed}"
                )
            self._wait_until(
                lambda w=watcher: w.values.get("80479d58", 0) >= 50,
                timeout=10,
                description=f"{name} online chooser advancing to source frame 50",
            )
            self._record_menu_observation(name, "chooser_ready_for_input")
            self.pads[name].set_axis("MAIN", 0.5, 0.0)
            self._wait_frames(name, 10)
            self.pads[name].neutral()
            self._wait_frames(name, 12)
            self._record_menu_observation(name, "after_direct_input")
            try:
                self._wait_until(
                    lambda w=watcher: _menu_state(w) is not None and _menu_state(w)[2] == 2,
                    timeout=5,
                    description=f"{name} selecting the Direct online option",
                )
            except TimeoutError as error:
                observed = _menu_state_observation(watcher)
                raise TimeoutError(
                    f"{name} did not select Direct after ordinary Main Stick input; "
                    f"observed {observed}"
                ) from error
            self._record_menu_observation(name, "direct_selected")
            self._pulse(name, "A")
        self._wait_until(
            lambda: all(self.watchers[name].online_scene_code() == SCENE_CSS
                        for name in ("p1", "p2")),
            timeout=45,
            description="both clients entering Direct online CSS",
        )

    def _submit_direct_tickets(self) -> None:
        event_log = self.work / "matchmaker-events.jsonl"
        for name in ("p1", "p2"):
            watcher = self.watchers[name]
            self._pulse(name, "START")
            # Direct mode opens the original name-entry screen first. The
            # pinned game ASM loads the newest saved code as a suggestion;
            # ordinary Z commits that suggestion and moves the source menu
            # selection to its confirm item (57). A alone is rejected while
            # the committed-character count is zero.
            self._wait_frames(name, 2)
            self._pulse(name, "Z")
            try:
                self._wait_until(
                    lambda w=watcher: _menu_state(w) is not None
                    and _menu_state(w)[2] == 57,
                    timeout=8,
                    description=f"{name} committing its saved Direct Code suggestion",
                )
            except TimeoutError as error:
                observed = _menu_state_observation(watcher)
                raise TimeoutError(
                    f"{name} did not reach the source Direct Code confirm item after Z; "
                    f"observed {observed}"
                ) from error
            entry = self._record_menu_observation(name, "direct_code_committed")
            expected_target = self.profiles["p2" if name == "p1" else "p1"].connect_code
            self.evidence["direct_ticket_submissions"].append({
                "client": name,
                "expected_target_code": expected_target,
                "source_confirm_item": 57,
                "source_frame": None if entry is None else entry["source_frame"],
                "confirm_item_observed": True,
            })
            self._pulse(name, "A")
            uid = self.profiles[name].identity
            self._wait_until(
                lambda target=uid: _ticket_accepted(_json_lines(event_log), target),
                timeout=10,
                description=f"{name} local ENet create-ticket acknowledgement",
            )
            self.evidence["direct_ticket_submissions"][-1]["ticket_accepted"] = True

    def _replay_file_snapshot(self) -> dict[str, set[Path]]:
        return {
            name: set(self.profiles[name].replay_root.glob("*.slp"))
            for name in ("p1", "p2")
        }

    def _wait_paired(self) -> None:
        event_log = self.work / "matchmaker-events.jsonl"

        def paired():
            rows = _json_lines(event_log)
            accepted = [row for row in rows if row.get("event") == "ticket_accepted"]
            assignments = [row for row in rows if row.get("event") == "pair_assigned"]
            if assignments and not assignments[-1].get("both_assignments_sent", False):
                raise RuntimeError("local matchmaker did not send both real peer assignments")
            for name in ("p1", "p2"):
                log_parts = [self.children[name].log_path.read_text(
                    encoding="utf-8", errors="replace")]
                file_log = self.profiles[name].user_root / "Logs" / "dolphin.log"
                if file_log.is_file():
                    log_parts.append(file_log.read_text(encoding="utf-8", errors="replace"))
                log = "\n".join(log_parts)
                if "[Matchmaking] Connection attempt failed" in log:
                    raise RuntimeError(f"{name} Slippi peer connection attempt failed")
                if "Connection success!" not in log:
                    return False
            if len(accepted) >= 2 and assignments:
                self.evidence["pairing"] = [*accepted, assignments[-1]]
                self.evidence["post_pair_source_state"] = {}
                cursor_fields = (
                    "cursor", "cursor_port", "cursor_mode", "cursor_target",
                    "model", "selected", "held", "bounds",
                )
                for name in ("p1", "p2"):
                    watcher = self.watchers[name]
                    ports = {}
                    for port in (1, 2):
                        cursor = watcher.css_cursor(port)
                        ports[str(port)] = {
                            "player": watcher.css_player(port),
                            "cursor": None if cursor is None else {
                                field: cursor[field] for field in cursor_fields
                            },
                        }
                    self.evidence["post_pair_source_state"][name] = {
                        "scene_kind": watcher.scene_kind(),
                        "online_scene_code": watcher.online_scene_code(),
                        "menu": _menu_state_observation(watcher),
                        "selected_stage": watcher.selected_stage(),
                        "source_frame": watcher.values.get("80479d58"),
                        "ports": ports,
                    }
                self.evidence["local_api_policy"] = "127.0.0.1:43114; fail-closed HTTP 503"
                self.evidence["local_api_events"] = [
                    {key: row[key] for key in ("event", "source_loopback") if key in row}
                    for row in rows if row.get("event", "").startswith("local_api_")
                ]
                if any(row.get("source_loopback") is False
                       for row in self.evidence["local_api_events"]):
                    raise RuntimeError("local API sink observed a non-loopback caller")
                self._check_network()
                return True
            return False

        self._wait_until(paired, timeout=self.timeouts["pair"],
                         description="two local tickets and both peer connection success logs")
        if self.pause_after_pair:
            marker = self.work / "paired.ready"
            marker.write_text("both loopback peers connected\n", encoding="ascii")
            marker.chmod(0o600)
            while True:
                self._service_children()
                ready, _, _ = select.select(
                    [watcher.socket for watcher in self.watchers.values()], [], [], 0.25
                )
                for watcher in self.watchers.values():
                    if watcher.socket in ready:
                        watcher.receive(0)
        self._use_assigned_controller_ports()
        self._wait_until(
            lambda: all(self.watchers[name].online_scene_code() == SCENE_GAME
                        for name in ("p1", "p2")),
            timeout=self.timeouts["game"],
            description="both peers starting Direct mode's first random-stage game",
        )
        self.evidence["first_direct_match_source_start"] = {
            name: {
                "online_scene_code": self.watchers[name].online_scene_code(),
                "source_frame": self.watchers[name].values.get("80479d58"),
            }
            for name in ("p1", "p2")
        }

    def _use_assigned_controller_ports(self) -> None:
        self.pads = dict(self.assigned_pads)
        self.evidence["assigned_controller_routes"] = {
            name: {
                "pipe": f"Pipe/0/pad{_gameplay_controller_port(name)}",
                "online_player_slot": _gameplay_controller_port(name),
            }
            for name in ("p1", "p2")
        }

    def _select_mario(self, name: str, *, profile_port: int | None = None) -> dict:
        watcher = self.watchers[name]
        if profile_port is None:
            profile_port = LOCAL_CONTROLLER_PORT
        if profile_port not in (1, 2):
            raise ValueError("Mario selection requires CSS port 1 or 2")
        last_state = None
        for attempt in range(400):
            state = watcher.css_cursor(profile_port)
            if state is None:
                self._wait_frames(name, 1)
                continue
            last_state = state
            cursor = state["cursor"]
            model = state["model"]
            left, right, top, bottom = state["bounds"]
            if _mario_cursor_confirmed(state):
                self.pads[name].neutral()
                self._wait_frames(name, 6)
                player = watcher.css_player(profile_port)
                if player is None or player["character_kind"] != CSS_MARIO_CHARACTER_KIND:
                    raise RuntimeError(
                        f"{name} CSS did not confirm Mario on port {profile_port}; "
                        f"cursor={state}, slot={player}"
                    )
                return state
            if state["held"] >= 0 and state["held"] != profile_port - 1:
                raise RuntimeError(f"{name} CSS cursor holds a different player's token")
            if state["held"] == profile_port - 1:
                target_x = (left + right) / 2 - 2.7
                target_y = (top + bottom) / 2 + 2.0
            else:
                target_x = model[0] - 3.8
                target_y = model[1] + 2.6
                if state["selected"] < 0:
                    target_y = max(1.0, target_y)
            dx = target_x - cursor[0]
            dy = target_y - cursor[1]
            if abs(dx) < 0.7 and abs(dy) < 0.7:
                self.pads[name].neutral()
                self._wait_frames(name, 2)
                self._pulse(name, "A")
            else:
                self.pads[name].set_axis("MAIN", 1.0 if dx > 0.7 else 0.0 if dx < -0.7 else 0.5,
                                         1.0 if dy > 0.7 else 0.0 if dy < -0.7 else 0.5)
                self._wait_frames(name, 1)
        raise TimeoutError(
            f"{name} CSS could not select Mario on port {profile_port} before its "
            f"source-state budget; last observed cursor {last_state}"
        )

    def _select_final_destination(self, *, picker: str, game_number: int = 2) -> None:
        # The source SSS moves its cursor by 0.03 times stick input beyond the
        # 0x1e deadzone and clamps it to +/-27 by +/-19. Scan that full authored
        # region one source frame at a time so adjacent rows overlap even the
        # smallest stage-icon bounds. Only the hovered tile is committed with A.
        if picker not in ("p1", "p2"):
            raise ValueError(f"unknown Final Destination picker {picker!r}")
        watcher = self.watchers[picker]
        pad = self.pads[picker]
        entry_frame = watcher.values.get("80479d58")
        self._wait_frames(picker, SSS_ENTRY_SETTLE_FRAMES)
        if watcher.online_scene_code() != SCENE_SSS:
            raise RuntimeError("original SSS exited during its source-frame settle")
        trace = {
            "client": picker,
            "entry_source_frame": entry_frame,
            "settle_frames": SSS_ENTRY_SETTLE_FRAMES,
            "settled_source_frame": watcher.values.get("80479d58"),
            "steps": [],
        }
        self.evidence["stage_selection_trace"] = trace
        if watcher.selected_stage() is None:
            self._wait_until(lambda: watcher.selected_stage() is not None, timeout=20,
                             description="source SSS selected-stage table")
        trace["cursor_bounds"] = {
            "x": [-SSS_CURSOR_X_BOUND, SSS_CURSOR_X_BOUND],
            "y": [-SSS_CURSOR_Y_BOUND, SSS_CURSOR_Y_BOUND],
            "max_step_per_source_frame": SSS_CURSOR_MAX_STEP_PER_FRAME,
            "horizontal_frames": SSS_CURSOR_SWEEP_X_FRAMES,
            "vertical_rows": SSS_CURSOR_SWEEP_Y_ROWS,
        }
        step_number = 0

        def move_and_observe(x: float, y: float, frames: int, phase: str,
                             row: int | None = None) -> tuple[int, int] | None:
            nonlocal step_number
            pad.set_axis("MAIN", x, y)
            for frame_in_segment in range(frames):
                self._wait_frames(picker, 1)
                selected = watcher.selected_stage()
                trace["steps"].append({
                    "step": step_number,
                    "phase": phase,
                    "row": row,
                    "frame_in_segment": frame_in_segment,
                    "direction": [x, y],
                    "selected_stage": selected,
                    "source_frame": watcher.values.get("80479d58"),
                    "committed": False,
                })
                step_number += 1
                if selected is not None and selected[1] == 32:
                    return selected
            return None

        def commit_final_destination(selected: tuple[int, int]) -> None:
            pad.neutral()
            self._wait_frames(picker, 6)
            if watcher.selected_stage() != selected:
                raise RuntimeError("Final Destination selection changed while settling")
            trace["steps"].append({
                "step": step_number,
                "phase": "selected",
                "row": None,
                "frame_in_segment": 0,
                "direction": [0.5, 0.5],
                "selected_stage": selected,
                "source_frame": watcher.values.get("80479d58"),
                "committed": True,
            })
            trace["final_destination_reached"] = True
            self.replay_baselines[game_number] = self._replay_file_snapshot()
            self._pulse(picker, "A")
            self._wait_until(
                lambda: all(self.watchers[name].online_scene_code() == SCENE_GAME
                            for name in ("p1", "p2")),
                timeout=45,
                description="both peers starting the Final Destination rematch",
            )

        selected = move_and_observe(0.0, 0.0, 10, "position_lower_left")
        if selected is not None:
            commit_final_destination(selected)
            return
        selected = move_and_observe(0.0, 0.5, 1, "clamp_left_edge")
        if selected is not None:
            commit_final_destination(selected)
            return
        for row in range(SSS_CURSOR_SWEEP_Y_ROWS):
            horizontal = 1.0 if row % 2 == 0 else 0.0
            selected = move_and_observe(
                horizontal, 0.5, SSS_CURSOR_SWEEP_X_FRAMES, "scan_row", row
            )
            if selected is not None:
                commit_final_destination(selected)
                return
            if row + 1 < SSS_CURSOR_SWEEP_Y_ROWS:
                selected = move_and_observe(0.5, 1.0, 1, "advance_row", row)
                if selected is not None:
                    commit_final_destination(selected)
                    return
        pad.neutral()
        trace["final_destination_reached"] = False
        raise TimeoutError(
            "original SSS cursor sweep did not reach Final Destination; "
            f"last observed stage={watcher.selected_stage()}"
        )

    def _play_and_rematch(self) -> None:
        client_process_ids = {name: self.children[name].pid for name in ("p1", "p2")}
        for game_number in (1, 2):
            if game_number == 2:
                self._enter_rematch()
            self._run_game(game_number)
            if self.input_probe_only:
                return
            if {name: self.children[name].pid for name in ("p1", "p2")} != client_process_ids:
                raise RuntimeError("rematch replaced one of the running client processes")
        self._verify_remote_inputs()

    def _run_game(self, game_number: int) -> None:
        self._wait_until(
            lambda: all(self.watchers[name].online_scene_code() == SCENE_GAME
                        for name in ("p1", "p2")),
            timeout=30,
            description=f"game {game_number} source game-start state",
        )
        start_files = self.replay_baselines.get(game_number)
        if start_files is None:
            raise RuntimeError(f"game {game_number} has no pre-launch replay-file baseline")
        self.pads["p1"].set_axis("MAIN", 0.0, 0.5)
        initial_frame = {
            name: self.watchers[name].values.get("80479d58") for name in ("p1", "p2")
        }
        self.evidence["games"].append({
            "game": game_number,
            "client_process_ids": {name: self.children[name].pid for name in ("p1", "p2")},
            "source_game_start": {name: self.watchers[name].online_scene_code() for name in ("p1", "p2")},
            "source_frame_counter": initial_frame,
        })
        # The pinned ASM clears pad input until UNFREEZE_INPUTS_FRAME minus
        # Slippi delay. Start after that
        # sync window and hold across normal controller polling.
        self._wait_game_frames("p2", SCRIPTED_INPUT_START_FRAME)
        self.evidence["games"][-1]["p2_a_input_source_frame"] = (
            self.watchers["p2"].values.get("80479d58")
        )
        self.pads["p2"].set_button("A", True)
        self.pads["p2"].set_axis("MAIN", 1.0, 0.5)
        self._wait_game_frames("p2", 30)
        self.pads["p2"].set_button("A", False)
        self.pads["p2"].set_axis("MAIN", 0.5, 0.5)
        self.evidence["games"][-1]["p2_a_input_end_source_frame"] = (
            self.watchers["p2"].values.get("80479d58")
        )
        if self.input_probe_only:
            self._wait_game_frames("p1", 60)
            self.evidence["input_probe"] = {
                "scene_codes": {
                    name: self.watchers[name].online_scene_code() for name in ("p1", "p2")
                },
                "source_frames": {
                    name: self.watchers[name].values.get("80479d58") for name in ("p1", "p2")
                },
                "local_pad_sent": {},
                "remote_pad_observations": {},
            }
            for client in ("p1", "p2"):
                sent_rows = [row for row in _json_lines(
                    self.work / f"{client}-local-pad-sent.jsonl"
                ) if row.get("event") == "local_pad_sent"]
                nonzero_sent = [row for row in sent_rows
                                if isinstance(row.get("pad_hex"), str)
                                and any(bytes.fromhex(row["pad_hex"]))]
                self.evidence["input_probe"]["local_pad_sent"][client] = {
                    "sample_count": len(sent_rows),
                    "distinct_payloads": len({row.get("pad_hex") for row in sent_rows}),
                    "nonzero_samples": len(nonzero_sent),
                    "first_nonzero_frame": (
                        nonzero_sent[0]["frame"] if nonzero_sent else None
                    ),
                    "last_nonzero_frame": (
                        nonzero_sent[-1]["frame"] if nonzero_sent else None
                    ),
                }
                remote_port = 2 if client == "p1" else 1
                rows = [row for row in _json_lines(self.work / f"{client}-remote-pad.jsonl")
                        if row.get("event") == "remote_pad_consumed"
                        and row.get("game_sequence") == 0
                        and row.get("remote_port") == remote_port]
                nonzero = [row for row in rows
                           if isinstance(row.get("pad_hex"), str)
                           and any(bytes.fromhex(row["pad_hex"]))]
                self.evidence["input_probe"]["remote_pad_observations"][client] = {
                    "remote_port": remote_port,
                    "consumed_samples": len(rows),
                    "distinct_payloads": len({row.get("pad_hex") for row in rows}),
                    "nonzero_samples": len(nonzero),
                    "first_nonzero_frame": nonzero[0]["frame"] if nonzero else None,
                }
            return

        left_input_writes = 1
        last_left_input_frame = self.watchers["p1"].values.get("80479d58")

        def returned_to_css():
            nonlocal left_input_writes, last_left_input_frame
            scenes = {
                name: self.watchers[name].online_scene_code() for name in ("p1", "p2")
            }
            if all(code == SCENE_CSS for code in scenes.values()):
                return True
            frame = self.watchers["p1"].values.get("80479d58")
            if frame is not None and frame != last_left_input_frame:
                self.pads["p1"].set_axis("MAIN", 0.0, 0.5)
                left_input_writes += 1
                last_left_input_frame = frame
            return False

        self._wait_until(
            returned_to_css,
            timeout=self.timeouts["game"],
            description=f"game {game_number} returning both Direct clients to CSS",
        )
        self.evidence["games"][-1]["p1_left_state_writes"] = left_input_writes

        finished_files: dict[str, Path] = {}

        def replays_finished():
            self._service_children()
            observed = {}
            for name in ("p1", "p2"):
                files = set(self.profiles[name].replay_root.glob("*.slp")) - start_files[name]
                if len(files) > 1:
                    raise RuntimeError(f"{name} produced more than one replay for game {game_number}")
                if files:
                    observed[name] = next(iter(files))
            if len(observed) == 2:
                for name, path in observed.items():
                    try:
                        with path.open("rb") as stream:
                            slippi_format.read_timeline(stream)
                    except slippi_format.SlippiFormatError as error:
                        if str(error) in (
                            "truncated Slippi prefix",
                            "live/incomplete Slippi raw length is zero",
                            "truncated Slippi raw stream",
                        ):
                            return False
                        raise
                finished_files.update(observed)
                return True
            return False

        self._wait_until(
            replays_finished,
            timeout=15,
            description=f"game {game_number} writing both completed Slippi replays",
        )
        self.pads["p1"].neutral()
        finished = {}
        for name in ("p1", "p2"):
            finished[name] = self._replay_summary(name, finished_files[name], game_number)
        self.evidence["games"][-1]["replays"] = finished
        self.evidence["games"][-1]["post_replay_source_state"] = {
            name: {
                "online_scene_code": self.watchers[name].online_scene_code(),
                "source_frame": self.watchers[name].values.get("80479d58"),
            }
            for name in ("p1", "p2")
        }

    def _replay_summary(self, name: str, path: Path, game_number: int) -> dict:
        with path.open("rb") as stream:
            timeline = slippi_format.read_timeline(stream)
        header = timeline.header
        players = sorted(header.players, key=lambda item: item["port"])
        stage_id = header.record()["stage_id"]
        if (not _stage_is_valid_for_game(stage_id, game_number)
                or [p["port"] for p in players] != [1, 2]
                or not _slippi_players_are_mario_mario(players)):
            expected = ("the Direct first-match random-stage pool" if game_number == 1
                        else "Mario/Mario on Final Destination")
            raise RuntimeError(f"{name} replay is not {expected}")
        if [p["stocks"] for p in players] != [4, 4] or timeline.game_end_method is None:
            raise RuntimeError(f"{name} replay lacks the expected four-stock Game End event")
        latest = {}
        for frame in timeline.frames:
            for post in frame.expected:
                if not post.is_follower:
                    latest[post.port] = post.stocks
        if (set(latest) != {1, 2} or list(latest.values()).count(0) != 1
                or any(value is None or not 0 <= value <= 4 for value in latest.values())):
            raise RuntimeError(f"{name} replay did not end with exactly one winner")
        local_port = 1 if name == "p1" else 2
        remote_port = 3 - local_port
        local_inputs = [sample for frame in timeline.frames for sample in frame.inputs
                        if sample.port == local_port and not sample.is_follower]
        remote_inputs = [sample for frame in timeline.frames for sample in frame.inputs
                         if sample.port == remote_port and not sample.is_follower]
        if not local_inputs or not remote_inputs:
            raise RuntimeError(f"{name} replay has no local/remote port input samples")
        summary = {
            "game": game_number,
            "sha256": _sha256(path),
            "stage_id": stage_id,
            "character_ids": [p["character_id"] for p in players],
            "starting_stocks": [p["stocks"] for p in players],
            "game_end_method": timeline.game_end_method,
            "first_frame": timeline.frames[0].number,
            "last_frame": timeline.frames[-1].number,
            "frame_count": len(timeline.frames),
            "final_stocks": {str(port): stocks for port, stocks in sorted(latest.items())},
            "local_port": local_port,
            "remote_port": remote_port,
            "local_input_samples": len(local_inputs),
            "remote_input_samples": len(remote_inputs),
            "local_a_button_samples": sum(
                1 for sample in local_inputs
                if sample.physical_buttons is not None and sample.physical_buttons & BUTTON_BITS["A"]
            ),
            "remote_a_button_samples": sum(
                1 for sample in remote_inputs
                if sample.physical_buttons is not None and sample.physical_buttons & BUTTON_BITS["A"]
            ),
            "local_left_stick_samples": sum(
                1 for sample in local_inputs
                if sample.raw_stick[0] is not None and sample.raw_stick[0] < -96
            ),
            "remote_left_stick_samples": sum(
                1 for sample in remote_inputs
                if sample.raw_stick[0] is not None and sample.raw_stick[0] < -96
            ),
        }
        if name == "p1" and (summary["remote_a_button_samples"] == 0
                             or summary["local_left_stick_samples"] == 0):
            raise RuntimeError("P1 replay did not record P2 A and P1 left-stick inputs")
        if name == "p2" and (summary["local_a_button_samples"] == 0
                             or summary["remote_left_stick_samples"] == 0):
            raise RuntimeError("P2 replay did not record P2 A and remote P1 left-stick inputs")
        return summary

    def _enter_rematch(self) -> None:
        before = len([row for row in _json_lines(self.work / "matchmaker-events.jsonl")
                      if row.get("event") == "pair_assigned"])
        self._wait_until(
            lambda: all(self.watchers[name].online_scene_code() == SCENE_CSS
                        for name in ("p1", "p2")),
            timeout=self.timeouts["rematch"],
            description="Direct rematch clients returning to the original CSS",
        )
        last_game = self.evidence["games"][-1]
        p1_stocks = last_game["replays"]["p1"]["final_stocks"]
        if list(p1_stocks.values()).count(0) != 1:
            raise RuntimeError("game 1 replay does not identify exactly one rematch loser")
        loser_port = next(int(port) for port, stocks in p1_stocks.items() if stocks == 0)
        loser = "p1" if loser_port == 1 else "p2"
        winner = "p2" if loser == "p1" else "p1"
        before_lock = {
            name: self.watchers[name].online_scene_code() for name in ("p1", "p2")
        }
        if any(code != SCENE_CSS for code in before_lock.values()):
            raise RuntimeError(f"Direct rematch did not return both clients to CSS: {before_lock}")
        self._pulse(winner, "START")
        self._pulse(loser, "START")
        self._wait_until(
            lambda: {
                self.watchers[name].online_scene_code() for name in ("p1", "p2")
            } == {SCENE_CSS, SCENE_SSS},
            timeout=self.timeouts["rematch"],
            description="Direct rematch lock-in assigning CSS to winner and SSS to loser",
        )
        scene_by_client = {
            name: self.watchers[name].online_scene_code() for name in ("p1", "p2")
        }
        if scene_by_client[loser] != SCENE_SSS or scene_by_client[winner] != SCENE_CSS:
            raise RuntimeError(
                "original rematch scenes disagree with game 1's loser: "
                f"loser={loser}, scenes={scene_by_client}"
            )
        self.evidence["rematch_source_state"] = {
            "game_1_loser_port": loser_port,
            "stage_picker": loser,
            "winner": winner,
            "scene_by_client": scene_by_client,
            "source_frame_by_client": {
                name: self.watchers[name].values.get("80479d58") for name in ("p1", "p2")
            },
        }
        # The pinned Direct ASM sends the winner to SB_NOTSEL and only the
        # loser into SSS when both confirm from CSS with START.
        self._select_final_destination(picker=loser)
        after = len([row for row in _json_lines(self.work / "matchmaker-events.jsonl")
                     if row.get("event") == "pair_assigned"])
        if after < before:
            raise RuntimeError("matchmaker pairing evidence was lost during rematch")

    def _verify_remote_inputs(self) -> None:
        for name in ("p1", "p2"):
            path = self.work / f"{name}-remote-pad.jsonl"
            rows = _json_lines(path)
            remote = [row for row in rows if row.get("event") == "remote_pad_consumed"
                      and row.get("remote_port") == (2 if name == "p1" else 1)]
            sequences = sorted({row.get("game_sequence") for row in remote})
            if sequences != [0, 1]:
                raise RuntimeError(f"{name} did not observe remote pads from both games: {sequences}")
            if not any(isinstance(row.get("pad_hex"), str)
                       and set(row["pad_hex"]) != {"0"} for row in remote):
                raise RuntimeError(f"{name} observed no non-neutral remote PAD bytes")
            self.evidence["remote_input"][name] = {
                "local_port": 1 if name == "p1" else 2,
                "remote_port": 2 if name == "p1" else 1,
                "game_sequences": sequences,
                "consumed_samples": len(remote),
                "distinct_pad_payloads": len({row["pad_hex"] for row in remote}),
                "first_frame_by_game": {
                    str(sequence): min(row["frame"] for row in remote
                                        if row.get("game_sequence") == sequence)
                    for sequence in sequences
                },
            }
            if self.evidence["remote_input"][name]["distinct_pad_payloads"] < 2:
                raise RuntimeError(f"{name} remote PAD observer saw no input-state change")

    def _disconnect_peer(self) -> None:
        target = self.children["p1"]
        self.cleanup.append(self.supervisor.stop(target))
        del self.children["p1"]
        log = self.children["p2"].log_path
        self._wait_until(
            lambda: "Final disconnect received for a client" in
                    log.read_text(encoding="utf-8", errors="replace"),
            timeout=20,
            description="peer observing an intentional loopback disconnect",
        )
        self.evidence["peer_disconnect"] = {
            "stopped_client": "p1",
            "peer_log": "p2-dolphin.log",
            "peer_log_observed_disconnect": True,
            "surviving_peer_process_id": self.children["p2"].pid,
        }

    def _wait_frames(self, name: str, count: int) -> None:
        self._service_children()
        watcher = self.watchers[name]
        key = "80479d58"
        if key not in watcher.values:
            self._wait_until(lambda: key in watcher.values, timeout=10,
                             description=f"{name} first source frame counter")
        initial = watcher.values[key]
        self._wait_until(
            lambda: key in watcher.values and ((watcher.values[key] - initial) & 0xFFFFFFFF) >= count,
            timeout=max(10, count / 20),
            description=f"{name} advancing {count} original source frames",
        )

    def _wait_game_frames(self, name: str, count: int) -> None:
        watcher = self.watchers[name]
        key = "80479d58"
        previous = watcher.values.get(key)
        if previous is None:
            self._wait_until(lambda: key in watcher.values, timeout=10,
                             description=f"{name} first game frame counter")
            previous = watcher.values[key]
        progressed = 0

        def advanced():
            nonlocal previous, progressed
            self._service_children()
            current = watcher.values.get(key)
            if current is not None and current != previous:
                progressed += _game_frame_delta(previous, current)
                previous = current
            return progressed >= count

        self._wait_until(advanced, timeout=30,
                         description=f"{name} advancing {count} game source frames")

    def _pulse(self, name: str, button: str) -> None:
        self.pads[name].set_button(button, True)
        self._wait_frames(name, 3)
        self.pads[name].set_button(button, False)
        self._wait_frames(name, 7)

    def _wait_until(self, predicate, *, timeout: float, description: str) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._service_children()
            if predicate():
                return
            now = time.monotonic()
            if now - self._last_network_check >= 2.0:
                self._check_network()
            sockets = [watcher.socket for watcher in self.watchers.values()]
            if sockets:
                ready, _, _ = select.select(sockets, [], [], min(0.1, max(0, deadline - now)))
                for watcher in self.watchers.values():
                    if watcher.socket in ready:
                        watcher.receive(0)
            else:
                time.sleep(min(0.1, max(0, deadline - now)))
        raise TimeoutError(f"timed out waiting for {description}")

    def _service_children(self) -> None:
        for name, child in self.children.items():
            if child.process.poll() is not None:
                raise RuntimeError(f"owned {name} process exited with status {child.process.returncode}")

    def _check_network(self) -> None:
        for name, child in self.children.items():
            sample = {
                "sampled_after_start_ms": int((time.monotonic() - self._run_started) * 1000),
                "sockets": _lsof_destinations(child.pid),
            }
            self.network_observations.setdefault(name, []).append(sample)
        self._last_network_check = time.monotonic()

    def _save_evidence(self) -> None:
        self.evidence["network_destinations"] = self.network_observations
        self.evidence["cleanup"] = self.cleanup
        try:
            first_peer = 41301 + (self.cycle - 1) * 4
            _assert_no_owned_ports((43113, 43114, first_peer, first_peer + 1,
                                    first_peer + 20, first_peer + 21))
            self.evidence["ports_released"] = True
        except BaseException as error:
            self.evidence["ports_released"] = False
            self.evidence["port_release_error"] = f"{type(error).__name__}: {error}"
            self.evidence["result"] = "failed-cleanup"
        output = self.work / "evidence.json"
        if (self.evidence.get("result") in ("passed", "interrupted", "input_probe_complete")
                and self.evidence.get("ports_released") is True
                and self.profile_root is not None):
            try:
                shutil.rmtree(self.profile_root)
                self.evidence["private_profiles_removed_after_cleanup"] = True
            except BaseException as error:
                self.evidence["private_profiles_removed_after_cleanup"] = False
                self.evidence["profile_cleanup_error"] = f"{type(error).__name__}: {error}"
                self.evidence["result"] = "failed-cleanup"
        elif self.profile_root is not None:
            self.evidence["private_profiles_removed_after_cleanup"] = False
        temporary = output.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(self.evidence, sort_keys=True, indent=2) + "\n",
                              encoding="utf-8")
        temporary.chmod(0o600)
        temporary.replace(output)


def _arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True,
                        help="operator-owned Melee disc image; never copied into the checkout")
    parser.add_argument("--run-root", type=Path,
                        help="new, empty evidence directory; defaults to a unique work/ directory")
    parser.add_argument("--repeat", type=int, choices=(1, 2), default=2,
                        help="run from one or two newly created private profile pairs")
    parser.add_argument("--boot-timeout", type=float, default=60)
    parser.add_argument("--pair-timeout", type=float, default=90)
    parser.add_argument("--game-timeout", type=float, default=420)
    parser.add_argument("--rematch-timeout", type=float, default=90)
    parser.add_argument("--pause-after-pair", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--input-probe-only", action="store_true", help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def _run_interruption_probe(*, disc: Path, parent_root: Path, timeouts: dict[str, float]) -> dict:
    child_root = parent_root / "interruption-child"
    supervisor = ProcessSupervisor(graceful_timeout=timeouts["pair"] + 30, term_timeout=5)
    child = supervisor.start(
        "harness-interruption-probe",
        [sys.executable, str(HERE / "run_local.py"), "--disc", str(disc),
         "--run-root", str(child_root), "--repeat", "1",
         "--boot-timeout", str(timeouts["boot"]), "--pair-timeout", str(timeouts["pair"]),
         "--game-timeout", str(timeouts["game"]), "--rematch-timeout", str(timeouts["rematch"]),
         "--pause-after-pair"],
        log_path=parent_root / "interruption-harness.log",
        graceful_signal=signal.SIGINT,
    )
    try:
        marker = child_root / "cycle-01" / "paired.ready"
        deadline = time.monotonic() + timeouts["boot"] + timeouts["pair"] + 30
        while time.monotonic() < deadline and not marker.is_file():
            if child.process.poll() is not None:
                raise RuntimeError("interruption probe exited before its paired checkpoint")
            time.sleep(0.1)
        if not marker.is_file():
            raise TimeoutError("interruption probe did not reach its paired checkpoint")
        stopped = supervisor.stop(child)
        evidence_path = child_root / "cycle-01" / "evidence.json"
        if not evidence_path.is_file():
            raise RuntimeError("interrupted child did not retain its cleanup evidence")
        evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
        valid = (
            evidence.get("result") == "interrupted"
            and evidence.get("ports_released") is True
            and not evidence.get("cleanup_error")
            and all(row.get("process_group_released") is True for row in evidence.get("cleanup", []))
            and stopped.get("process_group_released") is True
            and stopped.get("returncode") == 130
        )
        if not valid:
            raise RuntimeError("interruption probe did not prove bounded child cleanup")
        receipt = {
            "result": "passed",
            "checkpoint": "two clients paired over loopback before gameplay",
            "child_exit": stopped,
            "nested_cleanup": evidence.get("cleanup"),
            "ports_released": evidence.get("ports_released"),
            "evidence": "interruption-child/cycle-01/evidence.json",
        }
        receipt_path = parent_root / "interruption.json"
        receipt_path.write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n",
                                encoding="utf-8")
        receipt_path.chmod(0o600)
        return receipt
    finally:
        supervisor.close()


def main(argv=None) -> int:
    args = _arguments(argv)
    disc = args.disc.expanduser().resolve(strict=True)
    if not disc.is_file():
        raise ValueError("--disc must identify a regular owned disc image")
    if args.run_root is None:
        run_name = time.strftime("%Y%m%d-%H%M%S", time.gmtime()) + "-" + uuid.uuid4().hex[:8]
        root = DEFAULT_WORK / "runs" / run_name
    else:
        root = args.run_root.expanduser().resolve()
    root.mkdir(parents=True, mode=0o700, exist_ok=False)
    root.chmod(0o700)
    timeouts = {"boot": args.boot_timeout, "pair": args.pair_timeout,
                "game": args.game_timeout, "rematch": args.rematch_timeout}
    for cycle in range(1, args.repeat + 1):
        run = PairRun(root=root, disc=disc, cycle=cycle, timeouts=timeouts,
                      pause_after_pair=args.pause_after_pair,
                      input_probe_only=args.input_probe_only)
        print(f"cycle {cycle}/{args.repeat}: starting fresh local pair", flush=True)
        evidence = run.run(disconnect_after_rematch=(cycle == args.repeat))
        if args.pause_after_pair:
            return 0
        if args.input_probe_only:
            if evidence["result"] != "input_probe_complete":
                raise RuntimeError(f"input probe cycle {cycle} ended with {evidence['result']}")
            print(f"cycle {cycle}/{args.repeat}: input probe complete; evidence retained", flush=True)
            continue
        if evidence["result"] != "passed":
            raise RuntimeError(f"cycle {cycle} ended with {evidence['result']}")
        print(f"cycle {cycle}/{args.repeat}: passed; private evidence retained", flush=True)
    if args.repeat > 0 and not args.pause_after_pair and not args.input_probe_only:
        print("harness interruption probe: waiting for a fresh paired checkpoint", flush=True)
        receipt = _run_interruption_probe(disc=disc, parent_root=root, timeouts=timeouts)
        if receipt["result"] != "passed":
            raise RuntimeError("harness interruption probe failed")
        print("harness interruption probe: passed; child processes and ports released", flush=True)
        report = {
            "schema": "melee-web-local-slippi-integration-v1",
            "result": "passed",
            "scope": "Two adapted, pinned desktop Slippi clients completed a match and rematch through our local pairing service, with verified bidirectional peer input exchange and bounded cleanup.",
            "fresh_profile_cycles": [f"cycle-{cycle:02d}/evidence.json"
                                     for cycle in range(1, args.repeat + 1)],
            "harness_interruption": "interruption.json",
            "peer_disconnect_cycle": f"cycle-{args.repeat:02d}/evidence.json",
            "rollback_correctness_claimed": False,
            "browser_cross_play_claimed": False,
            "public_internet_claimed": False,
            "official_service_claimed": False,
        }
        report_path = root / "integration.json"
        report_path.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n",
                               encoding="utf-8")
        report_path.chmod(0o600)
        print("integration passed; private evidence retained", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("interrupted; owned process cleanup was attempted", file=sys.stderr)
        raise SystemExit(130)
