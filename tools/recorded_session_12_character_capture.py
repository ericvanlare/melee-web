#!/usr/bin/env python3
"""Capture bounded original-game VS sessions.

The driver launches the pinned passive reference Dolphin, navigates original
CSS/SSS with ordinary Pipe controllers, and records the source-consumed input
and observer stream. It does not read or write guest memory. ``--readiness-only``
drives all three CPU9 lineups through bounded CSS/SSS cancel loops without
starting a match; that mode is route evidence only and cannot produce a replay
candidate. ``--team-route`` captures three cycles of the two-player Mario Teams
route through No Contest Results and the retained CSS settings.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))

import reference_versus_sequence_capture as capture  # noqa: E402
import whole_session_replay as replay  # noqa: E402
from retail_setup_validation import _decode_setup  # noqa: E402
from dolphin_audio import dolphin_audio_options  # noqa: E402


DISC_SHA256 = "b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c"
DOL_SHA256 = "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646"
DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
DOLPHIN_COMMIT = "c77bbaa0f372c3f72281602a8b087206706542cb"
PROFILE_GCI = "01-GALE-SuperSmashBros0110290334.gci"
PROFILE_GCI_SHA256 = "39171db67ec4e6b85837107f7cd7e1231402e239ec7f74bfc9efa35d5a2879ce"
PROFILE_SRAM_SHA256 = "49dee06d38cb76aaa8f90bfb2e937face180243d1a170078b4e13226b012f82c"
CAPTURE_ID_PATTERN = re.compile(r"[A-Za-z0-9_.-]{1,128}\Z")

# Target points stay inside the authored icon hitboxes and account for source
# cursor limits at the right roster edge. The selected kind is checked after
# every placement, so a reachable point cannot silently select another icon.
ROSTER: dict[str, tuple[int, tuple[float, float]]] = {
    "MARIO": (8, (-20.9, 16.5)),
    "FOX": (2, (-20.9, 9.5)),
    "FALCO": (20, (-32.2, 9.5)),
    "MARTH": (9, (14.1, 2.5)),
    "DR_MARIO": (22, (-27.2, 16.5)),
    "ROY": (23, (20.6, 2.5)),
    "LINK": (6, (21.0, 9.5)),
    "YOUNG_LINK": (21, (26.0, 9.5)),
    "CAPTAIN_FALCON": (0, (21.0, 16.5)),
    "GANONDORF": (25, (26.0, 16.5)),
    "LUIGI": (7, (-13.9, 16.5)),
    "PIKACHU": (13, (-13.9, 2.5)),
}
ROSTER_ICON: dict[str, int] = {
    "MARIO": 1,
    "FOX": 10,
    "FALCO": 9,
    "MARTH": 23,
    "DR_MARIO": 0,
    "ROY": 24,
    "LINK": 16,
    "YOUNG_LINK": 17,
    "CAPTAIN_FALCON": 7,
    "GANONDORF": 8,
    "LUIGI": 2,
    "PIKACHU": 19,
}
LINEUPS: tuple[tuple[str, ...], ...] = (
    ("MARIO", "FOX", "FALCO", "MARTH"),
    ("DR_MARIO", "ROY", "LINK", "YOUNG_LINK"),
    ("CAPTAIN_FALCON", "GANONDORF", "LUIGI", "PIKACHU"),
)
EXPECTED_ROSTER = tuple(tuple(ROSTER[name][0] for name in lineup) for lineup in LINEUPS)
# The original team's color for Mario comes from
# ``gm_801692BC(8) -> lbl_803D51A0[8].x2``.  The pinned table row is
# ``{ 0x05, 0x00, 0x03, 0x04 }``: team 0 uses x1 (0), and team 1 uses x2
# (3).  Keep this source mapping explicit so a permissive costume check cannot
# admit a visually similar but source-invalid team setup.
MARIO_TEAM_COSTUMES = {0: 0, 1: 3}
# Source Items cursor 0 is authored preference bit 5
# (``mnItemSw_803ED438[0]``), which ``lbl_803B7844`` maps to
# StartMeleeData item-mask bit 18.  Clearing that bit is the committed
# row-zero mask emitted by the original Team match.
TEAM_ROUTE_ITEM_MASK = "fffffffffffbffff"
# CSSData keeps the source ``StartMeleeData`` at +0x10, but it is the
# pre-entry copy.  ``gmVsMelee_EnterVs`` sets StartMeleeRules.is_vs only when
# entering the match, so the retained CSS copy has the source byte 0x83 rather
# than the ordinary match-entry value with that bit set.
CSS_RETAINED_PROFILE_BYTE = 0x83


class CaptureFailure(RuntimeError):
    """A source setup or ordinary-input step failed its declared boundary."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _validate_setup(raw_hex: str, match_index: int) -> dict[str, Any]:
    try:
        setup = _decode_setup(raw_hex)
    except (KeyError, TypeError, ValueError) as error:
        raise CaptureFailure(f"match {match_index} source setup is unsupported: {error}") from error
    if not 0 <= match_index < len(EXPECTED_ROSTER):
        raise CaptureFailure(f"unexpected source match index {match_index}")
    if {key: value for key, value in setup.items() if key != "players"} != replay.V9_MILESTONE_RULES:
        raise CaptureFailure(f"match {match_index} rules differ from the accepted stock-match profile")
    players = setup.get("players")
    if not isinstance(players, list) or len(players) != 4:
        raise CaptureFailure(f"match {match_index} does not have four source players")
    actual: list[int] = []
    for slot, player in enumerate(players):
        expected = {
            "port": slot + 1,
            "character_kind": EXPECTED_ROSTER[match_index][slot],
            "costume": slot,
            "stocks": 4,
            "player_type": 1,
            "rumble_enabled": False,
            "cpu_kind": 4,
            "cpu_level": 9,
        }
        if any(player.get(key) != value for key, value in expected.items()):
            raise CaptureFailure(
                f"match {match_index} port {slot + 1} source setup mismatch: "
                f"expected {expected}, received {player}"
            )
        actual.append(player["character_kind"])
    if tuple(actual) != EXPECTED_ROSTER[match_index]:
        raise CaptureFailure(f"match {match_index} source roster mismatch")
    return setup


def _validate_team_setup(raw_hex: str, match_index: int) -> dict[str, Any]:
    if type(match_index) is not int or not 0 <= match_index < len(EXPECTED_ROSTER):
        raise CaptureFailure(f"unexpected source team-match index {match_index}")
    try:
        setup = _decode_setup(raw_hex)
    except (KeyError, TypeError, ValueError) as error:
        raise CaptureFailure(f"source team setup is unsupported: {error}") from error
    if (setup["stage"] != 0x20 or setup["match_kind"] != 1 or
            setup["timer_enabled"] or setup["time_limit_seconds"] != 0 or
            not setup["is_stock"] or setup["disable_pausing"] or
            not setup["is_teams"] or setup["item_frequency"] != -1):
        raise CaptureFailure(f"source team rules differ from the declared route: {setup}")
    players = setup["players"]
    if (len(players) != 2 or [player["port"] for player in players] != [1, 2] or
            [player["player_type"] for player in players] != [0, 1] or
            any(player["character_kind"] != ROSTER["MARIO"][0] or
                player["stocks"] != 3 for player in players) or
            players[1].get("cpu_kind") != 4 or players[1].get("cpu_level") != 1):
        raise CaptureFailure(
            f"source team setup did not contain a three-stock Mario and level-1 CPU: {players}")
    raw = bytes.fromhex(raw_hex)
    teams = [raw[0x69], raw[0x8D]]
    if any(team > 2 for team in teams) or teams[0] == teams[1]:
        raise CaptureFailure(f"source team setup did not contain opposing authored teams: {teams}")
    expected_costumes = [MARIO_TEAM_COSTUMES.get(team) for team in teams]
    actual_costumes = [player["costume"] for player in players]
    if actual_costumes != expected_costumes:
        raise CaptureFailure(
            "source team setup did not contain source-derived Mario team colors: "
            f"expected {expected_costumes} for teams {teams}, received {actual_costumes}")
    setup["player_teams"] = teams
    if setup["item_mask_hex"] != TEAM_ROUTE_ITEM_MASK:
        raise CaptureFailure(
            "source Team match did not receive the Rules/Items route's row-zero item mask")
    return setup


def _decode_css_start_data(raw_hex: str) -> dict[str, Any]:
    """Decode the retained CSSData StartMeleeData with its source profile.

    The ordinary setup decoder intentionally requires ``is_vs`` because that
    bit is installed by ``gmVsMelee_EnterVs`` at match entry.  CSSData is the
    source's pre-entry copy, so it must retain the authored pre-entry byte and
    cannot be checked with that match-entry identity requirement.  All other
    setup validation remains shared with the ordinary decoder.
    """
    if not isinstance(raw_hex, str):
        raise CaptureFailure("source CSS retained state is not encoded as bytes")
    try:
        raw = bytearray.fromhex(raw_hex)
    except ValueError as error:
        raise CaptureFailure(f"source CSS retained state is not valid hex: {error}") from error
    if len(raw) != 0x138:
        raise CaptureFailure(
            f"source CSS retained state is not a complete 0x138-byte StartMeleeData: {len(raw)}")
    if raw[4] != CSS_RETAINED_PROFILE_BYTE:
        raise CaptureFailure(
            "source CSS retained state has an unexpected pre-entry VS profile byte: "
            f"expected {CSS_RETAINED_PROFILE_BYTE:#x}, received {raw[4]:#x}")
    # Reuse every ordinary setup check and decoder field while locally
    # supplying the entry-only is_vs bit that the source adds after CSS.
    raw[4] |= 0x40
    try:
        return _decode_setup(raw.hex())
    except (KeyError, TypeError, ValueError) as error:
        raise CaptureFailure(f"source CSS retained state is unsupported: {error}") from error


def _decode_team_result(raw_hex: str) -> dict[str, Any]:
    if not isinstance(raw_hex, str) or len(raw_hex) != 0x28 * 2:
        raise CaptureFailure("original Team Results omitted its complete result slice")
    try:
        raw = bytes.fromhex(raw_hex)
    except ValueError as error:
        raise CaptureFailure("original Team Results exposed malformed result bytes") from error
    winner_count = raw[0x0D]
    if winner_count > 2:
        raise CaptureFailure(f"original Team Results has invalid winner count {winner_count}")
    result = {"outcome": raw[4],
              "winners": list(raw[0x10:0x10 + winner_count])}
    if (len(set(result["winners"])) != winner_count or
            any(winner not in (0, 1) for winner in result["winners"])):
        raise CaptureFailure(f"original Team Results has invalid winner slots: {result}")
    if result["outcome"] != 7:
        raise CaptureFailure(
            f"original Teams pause chord did not publish No Contest outcome 7: {result}")
    return result


SUCCESS_RESULTS = frozenset({
    "source_css_sss_route_readiness_complete",
    "three_match_source_capture_complete",
    "original_vs_team_results_css_capture_complete",
})


def _capture_exit_code(result: Any) -> int:
    return 0 if result in SUCCESS_RESULTS else 1


class Driver:
    def __init__(self, controller: capture.DualPipeController,
                 latest: dict[str, Any], stop: threading.Event,
                 *, readiness_only: bool, team_route_only: bool = False) -> None:
        self.controller = controller
        self.latest = latest
        self.stop = stop
        self.readiness_only = readiness_only
        self.team_route_only = team_route_only
        self.steps: list[dict[str, Any]] = []

    def wait(self, predicate: Callable[[], bool], label: str,
             seconds: float = 12.0) -> None:
        deadline = time.monotonic() + seconds
        while not predicate():
            if self.stop.wait(0.01):
                raise CaptureFailure(f"capture stopped while waiting for {label}")
            if time.monotonic() >= deadline:
                summary = {key: self.latest.get(key) for key in
                           ("scene_kind", "main_kind", "main_selection", "players",
                            "doors", "cursors", "models", "boundaries")}
                raise CaptureFailure(f"{label}; latest source state={summary}")

    @staticmethod
    def _expected_button(pad: str) -> int:
        return int.from_bytes(bytes.fromhex(pad)[:2], "big")

    def tap(self, port: int, button: str, *, label: str,
            second_port_button: str | None = None) -> None:
        first = capture.raw_pad(buttons=[button]) if port == 0 else capture.NEUTRAL_PAD
        second = capture.raw_pad(buttons=[button]) if port == 1 else capture.NEUTRAL_PAD
        if second_port_button is not None:
            first = capture.raw_pad(buttons=[button])
            second = capture.raw_pad(buttons=[second_port_button])
        before = self.latest.get("consume_seq", -1)
        expected = (self._expected_button(first), self._expected_button(second))
        self.controller.set_both(first, second, action=label)
        deadline = time.monotonic() + 10.0
        while not any(row["seq"] > before and row["buttons"] == expected
                      for row in self.latest.get("consumed_history", [])):
            if self.stop.wait(0.005):
                raise CaptureFailure(f"capture stopped while pressing {label}")
            if time.monotonic() >= deadline:
                raise CaptureFailure(f"no source-consumed button edge for {label}")
        release_after = self.latest.get("consume_seq", before)
        self.controller.set_both(capture.NEUTRAL_PAD, capture.NEUTRAL_PAD,
                                 action=label + ":release")
        while not any(row["seq"] > release_after and row["buttons"] == (0, 0)
                      for row in self.latest.get("consumed_history", [])):
            if self.stop.wait(0.005):
                raise CaptureFailure(f"capture stopped while releasing {label}")
            if time.monotonic() >= deadline:
                raise CaptureFailure(f"no source-consumed neutral after {label}")

    def tap_both(self, button: str, *, label: str) -> None:
        self.tap(0, button, label=label, second_port_button=button)

    def move(self, x: float, y: float, label: str, port: int = 0) -> None:
        deadline = time.monotonic() + 8.0
        last_axes: tuple[int, int] | None = None

        def neutralize() -> None:
            before = self.latest.get("consume_seq", -1)
            self.controller.write(port + 1, capture.NEUTRAL_PAD, action=label + ":neutral")
            self.wait(
                lambda: self.latest.get("consume_seq", -1) > before and
                self.latest.get("consumed_axes", [(1, 1)] * 4)[port] == (0, 0),
                "source-consumed neutral stick " + label,
            )
            revision = self.latest.get("cursor_revisions", {}).get(port, 0)
            previous = self.latest["cursors"][port]
            stable = 0
            settle_deadline = min(deadline, time.monotonic() + 1.5)
            while stable < 2:
                if self.stop.wait(0.004):
                    raise CaptureFailure(f"capture stopped while settling {label}")
                current_revision = self.latest.get("cursor_revisions", {}).get(port, 0)
                if current_revision > revision:
                    current = self.latest["cursors"][port]
                    stable = (stable + 1 if abs(current["x"] - previous["x"]) < 0.02 and
                              abs(current["y"] - previous["y"]) < 0.02 else 0)
                    previous = current
                    revision = current_revision
                if time.monotonic() >= settle_deadline:
                    raise CaptureFailure(f"CSS cursor did not settle after {label}")

        while True:
            if self.stop.is_set():
                raise CaptureFailure(f"capture stopped while moving for {label}")
            cursor = self.latest["cursors"][port]
            dx, dy = x - cursor["x"], y - cursor["y"]
            if abs(dx) < 0.6 and abs(dy) < 0.6:
                neutralize()
                cursor = self.latest["cursors"][port]
                if abs(x - cursor["x"]) < 0.6 and abs(y - cursor["y"]) < 0.6:
                    return
                last_axes = None
                continue
            if time.monotonic() >= deadline:
                raise CaptureFailure(f"cursor movement timed out for {label}: {cursor} to {(x, y)}")

            def axis(delta: float) -> int:
                return 0 if abs(delta) < 0.5 else int(math.copysign(70 if abs(delta) > 5 else 35, delta))

            axes = (axis(dx), axis(dy))
            if axes != last_axes:
                self.controller.write(port + 1,
                                      capture.raw_pad(x=axes[0], y=axes[1]), action=label)
                last_axes = axes
            time.sleep(0.012)

    def state(self, label: str) -> None:
        row = copy.deepcopy({
            "label": label,
            "source_sequence": self.latest.get("consume_seq"),
            "players": self.latest.get("players", [])[:4],
            "doors": self.latest.get("doors", []),
            "cursors": self.latest.get("cursors", {}),
            "models": self.latest.get("models", {}),
            "sliders": self.latest.get("sliders", {}),
            "team_state": self.latest.get("team_state"),
        })
        self.steps.append(row)
        print(json.dumps(row, sort_keys=True), flush=True)

    def _door_input(self, slot: int) -> None:
        door = self.latest["doors"][slot]
        self.move((door["left"] + door["right"]) / 2, -2.2,
                  f"door-{slot}-toggle")
        before = self.latest["players"][slot]["kind"]
        self.tap(0, "A", label=f"door-{slot}-toggle")
        self.wait(lambda: self.latest["players"][slot]["kind"] != before,
                  f"door {slot} source kind transition")

    def set_door_kind(self, slot: int, wanted: int) -> None:
        for attempt in range(3):
            current = self.latest["players"][slot]["kind"]
            if current == wanted:
                return
            self._door_input(slot)
            self.state(f"door-{slot}-kind-{self.latest['players'][slot]['kind']}-step-{attempt}")
        if self.latest["players"][slot]["kind"] != wanted:
            raise CaptureFailure(f"door {slot} did not reach source kind {wanted}")

    def set_costume(self, slot: int, name: str, port: int) -> None:
        expected_icon = ROSTER_ICON[name]
        self.wait(lambda: self.latest["doors"][slot]["icon"] == expected_icon,
                  f"source hover confirmed {name} in slot {slot}")
        for attempt in range(8):
            current = self.latest["doors"][slot]["costume"]
            if current == slot:
                return
            self.tap(port, "X", label=f"set-costume-{slot}-slot-{slot}")
            self.wait(lambda: self.latest["doors"][slot]["costume"] != current,
                      f"source costume change for {name} in slot {slot}", seconds=2.0)
        if self.latest["doors"][slot]["costume"] != slot:
            raise CaptureFailure(
                f"source did not reach costume {slot} for {name} in slot {slot}; "
                f"observed {self.latest['doors'][slot]['costume']} after 8 changes"
            )

    def select_human(self, slot: int, name: str, *, initial: bool) -> None:
        character, point = ROSTER[name]
        if not initial:
            # CPU → off → human; each state is source-observed before continuing.
            self.set_door_kind(slot, 0)
            model = self.latest["models"][slot]
            port = slot
            self.move(model["x"] - 2.0, model["y"] + 1.6,
                      f"pickup-human-slot-{slot}", port)
            self.tap(port, "A", label=f"pickup-human-slot-{slot}")
            self.wait(lambda: self.latest["cursors"][port]["state"] == 1,
                      f"human slot {slot} puck pickup")
            self.state(f"human-slot-{slot}-picked-up")
        else:
            port = slot
        if initial:
            self.move(*point, f"select-{name}-slot-{slot}", port)
            self.tap(port, "A", label=f"place-{name}-slot-{slot}")
            self.wait(lambda: self.latest["players"][slot]["character"] == character,
                      f"source confirmed {name} in slot {slot}")
            if slot == 0:
                self.state(f"selected-{name}-slot-{slot}")
                return
            if slot > 0:
                model = self.latest["models"][slot]
                self.move(model["x"] - 2.0, model["y"] + 1.6,
                          f"pickup-human-slot-{slot}", port)
                self.tap(port, "A", label=f"pickup-human-slot-{slot}")
                self.wait(lambda: self.latest["cursors"][port]["state"] == 1,
                          f"human slot {slot} puck pickup for costume")
        self.move(*point, f"select-{name}-slot-{slot}", port)
        self.set_costume(slot, name, port)
        self.tap(port, "A", label=f"place-{name}-slot-{slot}")
        if (self.latest["cursors"][port]["state"] == 1 and
                self.latest["cursors"][port]["held"] == slot):
            door = self.latest["doors"][slot]
            self.move((door["left"] + door["right"]) / 2, -2.2,
                      f"drop-human-slot-{slot}-door", port)
            self.tap(port, "A", label=f"drop-human-slot-{slot}-door")
        self.wait(lambda: self.latest["cursors"][port]["state"] != 1,
                  f"human slot {slot} puck placed")
        self.wait(lambda: self.latest["players"][slot]["character"] == character,
                  f"source confirmed {name} in slot {slot}")
        if not initial:
            self.set_door_kind(slot, 1)
        self.state(f"selected-{name}-slot-{slot}")

    def select_cpu(self, slot: int, name: str) -> None:
        character, point = ROSTER[name]
        if self.latest["players"][slot]["kind"] != 1:
            self.set_door_kind(slot, 1)
        model = self.latest["models"][slot]
        self.move(model["x"] - 3.8, model["y"] + 2.6,
                  f"pickup-cpu-slot-{slot}")
        self.tap(0, "A", label=f"pickup-cpu-slot-{slot}")
        self.wait(lambda: self.latest["cursors"][0]["state"] == 1 and
                  self.latest["cursors"][0]["held"] == slot,
                  f"CPU slot {slot} puck pickup")
        self.move(*point, f"select-{name}-slot-{slot}")
        self.set_costume(slot, name, 0)
        self.tap(0, "A", label=f"place-{name}-slot-{slot}")
        if (self.latest["cursors"][0]["state"] == 1 and
                self.latest["cursors"][0]["held"] == slot):
            door = self.latest["doors"][slot]
            self.move((door["left"] + door["right"]) / 2, -2.2,
                      f"drop-cpu-slot-{slot}-door")
            self.tap(0, "A", label=f"drop-cpu-slot-{slot}-door")
        self.wait(lambda: self.latest["cursors"][0]["state"] != 1,
                  f"CPU slot {slot} puck placed")
        self.wait(lambda: self.latest["players"][slot]["character"] == character,
                  f"source confirmed {name} in CPU slot {slot}")
        self.state(f"selected-{name}-slot-{slot}")

    def set_cpu9(self, slot: int) -> None:
        if self.latest["players"][slot]["cpu"] == 9:
            return
        slider = self.latest["sliders"][slot * 2]
        if slider["dirty"]:
            raise CaptureFailure(f"CPU slider {slot} was dirty before adjustment")
        self.move(slider["x"] - 2.9, slider["y"] + 1.7,
                  f"grab-cpu-slider-{slot}")
        self.tap(0, "A", label=f"grab-cpu-slider-{slot}")
        self.wait(lambda: self.latest["cursors"][0]["state"] == 1 and
                  self.latest["cursors"][0]["held"] == slot + 4,
                  f"CPU slider {slot} grabbed")
        self.controller.write(1, capture.raw_pad(x=80), action=f"set-cpu9-{slot}")
        self.wait(lambda: self.latest["players"][slot]["cpu"] == 9,
                  f"source CPU slot {slot} level nine")
        self.controller.write(1, capture.NEUTRAL_PAD, action=f"set-cpu9-{slot}:neutral")
        before = self.latest.get("consume_seq", -1)
        # The previous write is source-consumed before the release click.
        self.wait(lambda: self.latest.get("consume_seq", -1) > before,
                  f"source consumed CPU slot {slot} neutral")
        self.tap(0, "A", label=f"release-cpu-slider-{slot}")
        self.wait(lambda: self.latest["cursors"][0]["state"] != 1,
                  f"CPU slider {slot} released")

    def configure_lineup(self, match_index: int, *, initial: bool) -> None:
        lineup = LINEUPS[match_index]
        self.latest["expected_match_index"] = 0 if self.readiness_only else match_index
        self.wait(lambda: len(self.latest["models"]) == 4 and
                  self.latest.get("css_polls", 0) >= 240,
                  f"CSS readiness for lineup {match_index}")
        if initial:
            self.select_human(0, lineup[0], initial=True)
            self.select_human(1, lineup[1], initial=True)
            for slot in range(4):
                self.set_door_kind(slot, 1)
            self.select_cpu(2, lineup[2])
            self.select_cpu(3, lineup[3])
        else:
            self.select_human(0, lineup[0], initial=False)
            self.select_human(1, lineup[1], initial=False)
            self.select_cpu(2, lineup[2])
            self.select_cpu(3, lineup[3])
        for slot in range(4):
            self.set_cpu9(slot)
        expected = EXPECTED_ROSTER[match_index]
        current = self.latest["players"][:4]
        if any(row["kind"] != 1 or row["cpu"] != 9 or row["character"] != expected[index]
               for index, row in enumerate(current)):
            raise CaptureFailure(f"CSS lineup {match_index} did not source-confirm all four CPU9s")
        costumes = [door["costume"] for door in self.latest["doors"]]
        if costumes != [0, 1, 2, 3]:
            raise CaptureFailure(f"CSS lineup {match_index} costume colors mismatch: {costumes}")
        self.state(f"ready-lineup-{match_index}")

    def enter_fd(self, *, choose_stage: bool, before: int) -> None:
        self.latest["css_idle_after"] = self.latest.get("polls", 0) + 12
        self.wait(lambda: self.latest.get("polls", 0) >= self.latest.get("css_idle_after", 0),
                  "CSS input cooldown")
        self.tap(0, "START", label="CSS-start-SSS")
        self.wait(lambda: self.latest["boundaries"].get("sss_enter", 0) > before,
                  "original SSS entry")
        stage_polls = self.latest.get("polls", 0)
        self.wait(lambda: self.latest.get("polls", 0) > stage_polls + 120,
                  "stage select initialization")
        if self.latest.get("stage_kind") != 32:
            self.controller.write(1, capture.raw_pad(x=40), action="toward-FD-column")
            self.wait(lambda: self.latest.get("polls", 0) > stage_polls + 138,
                      "stage cursor horizontal movement")
            self.controller.write(1, capture.NEUTRAL_PAD, action="stage-column-neutral")
            time.sleep(0.06)
            self.controller.write(1, capture.raw_pad(y=40), action="find-FD-up")
            self.wait(lambda: self.latest.get("stage_kind") == 32,
                      "source highlighted Final Destination", seconds=3.0)
            self.controller.write(1, capture.NEUTRAL_PAD, action="stage-scan-neutral")
        if choose_stage:
            time.sleep(0.1)
            self.tap(0, "A", label="choose-Final-Destination")
        else:
            before_css = self.latest.get("css_entry_count", 0)
            self.tap(0, "B", label="cancel-SSS-to-CSS")
            self.wait(lambda: self.latest.get("scene_kind") == "08" and
                      self.latest.get("css_entry_count", 0) > before_css,
                      "SSS cancel returned to CSS", seconds=20.0)
            css_start = self.latest.get("polls", 0)
            self.wait(lambda: self.latest.get("polls", 0) >= css_start + 240,
                      "CSS after SSS cancel")

    def boot_and_drive(self) -> None:
        try:
            self._boot_menus()
            if self.team_route_only:
                self.play_team_battle()
                self.latest["driver_complete"] = True
                return
            for match_index in range(3):
                self.configure_lineup(match_index, initial=(match_index == 0))
                if self.readiness_only:
                    self.enter_fd(choose_stage=False,
                                  before=self.latest["boundaries"].get("sss_enter", 0))
                    self.state(f"readiness-cycled-lineup-{match_index}")
                    continue
                self._play_match(match_index)
            self.latest["driver_complete"] = True
        except Exception as error:
            self.latest["driver_error"] = str(error)
            self.stop.set()

    def _boot_menus(self) -> None:
        pulse = 0
        deadline = time.monotonic() + 45.0
        while self.latest.get("scene_kind") != "01":
            if self.stop.wait(0.1):
                raise CaptureFailure("capture stopped during boot scene gates")
            if time.monotonic() > deadline:
                raise CaptureFailure("main menu was not observed through startup gates")
            kind = self.latest.get("scene_kind")
            if kind in ("1c", "00", "27"):
                self.tap(0, "START" if kind == "00" else "A",
                         label=f"startup-{kind}-{pulse}")
                pulse += 1

        def ready(kind: int) -> bool:
            return self.latest.get("main_kind") == kind and self.latest.get("main_cooldown") == 0 and \
                self.latest.get("main_polls", 0) >= 60

        self.wait(lambda: ready(0), "initial main menu readiness")
        if self.latest.get("main_selection") != 0:
            raise CaptureFailure("unexpected initial main menu selection")
        self.tap(0, "D_DOWN", label="main-menu-down-to-VS")
        self.wait(lambda: ready(0) and self.latest.get("main_selection") == 1,
                  "main menu selected VS")
        self.tap(0, "A", label="main-menu-select-VS")
        self.wait(lambda: ready(2) and self.latest.get("main_selection") == 0,
                  "VS menu ready at Melee")

        def menu_tap(button: str, label: str) -> None:
            self.wait(lambda: self.latest.get("main_cooldown") == 0,
                      "VS Rules input ready " + label)
            self.tap(0, button, label=label)
            time.sleep(0.15)

        for wanted in (1, 2, 3):
            menu_tap("D_DOWN", f"VS-rules-{wanted}")
            self.wait(lambda wanted=wanted: self.latest.get("main_selection") == wanted,
                      "VS Rules selection")
        menu_tap("A", "open-Rules")
        self.wait(lambda: ready(13) and self.latest.get("main_selection") == 0,
                  "Rules menu")
        if self.latest.get("main_confirmed") != 0:
            raise CaptureFailure("expected original Time mode before changing to Stock")
        menu_tap("D_RIGHT", "select-Stock-mode")
        self.wait(lambda: self.latest.get("main_confirmed") == 1, "Stock mode selected")
        menu_tap("D_DOWN", "stock-count-row")
        self.wait(lambda: self.latest.get("main_selection") == 1, "stock count row")
        if self.latest.get("main_confirmed") != 3:
            raise CaptureFailure("expected three stocks after selecting Stock mode")
        if not self.team_route_only:
            menu_tap("D_RIGHT", "four-stocks")
            self.wait(lambda: self.latest.get("main_confirmed") == 4, "four stocks selected")
        for wanted in (2, 3, 4, 5):
            menu_tap("D_DOWN", f"Rules-item-switch-{wanted}")
            self.wait(lambda wanted=wanted: self.latest.get("main_selection") == wanted,
                      "item-switch row")
        menu_tap("A", "open-item-switch")
        self.wait(lambda: ready(16) and self.latest.get("main_selection") == 0,
                  "item switch ready")
        item_entry_poll = self.latest.get("polls", 0)
        self.wait(lambda: self.latest.get("polls", 0) >= item_entry_poll + 60,
                  "original Items transition animation")
        menu_tap("A", "toggle-source-item-row-zero")
        menu_tap("D_UP", "item-frequency-row")
        self.wait(lambda: self.latest.get("main_selection") == 31, "item frequency row")
        if self.latest.get("main_confirmed") != 3:
            raise CaptureFailure("expected original medium item frequency")
        for desired in (2, 1, 0):
            menu_tap("D_RIGHT", f"item-frequency-{desired}")
            self.wait(lambda desired=desired: self.latest.get("main_confirmed") == desired,
                      "item frequency value")
        menu_tap("B", "return-to-Rules")
        self.wait(lambda: ready(13), "Rules returned")
        menu_tap("START", "Rules-start-Melee")
        self.wait(lambda: len(self.latest["models"]) == 4 and
                  self.latest.get("css_polls", 0) > 240,
                  "four original CSS doors initialized")

    def _play_match(self, match_index: int) -> None:
        idle = self.latest.get("polls", 0) + 12
        self.latest["css_idle_after"] = idle
        before = self.latest["boundaries"].get("sss_enter", 0)
        self.enter_fd(choose_stage=True, before=before)
        self.wait(lambda: len(self.latest.get("setup_records", [])) > match_index,
                  f"source setup record for match {match_index}", seconds=30.0)
        self.wait(lambda: self.latest["boundaries"].get("setup", 0) > before,
                  f"source match setup boundary {match_index}", seconds=30.0)
        self.state(f"source-setup-match-{match_index}")
        result_before = self.latest["boundaries"].get("results_gobj", 0)
        self.wait(lambda: self.latest["boundaries"].get("results_gobj", 0) > result_before,
                  f"natural Results for match {match_index}", seconds=360.0)
        self.state(f"natural-results-match-{match_index}")
        return_before = self.latest["boundaries"].get("return_css", 0)
        for pulse in range(10):
            if self.latest["boundaries"].get("return_css", 0) > return_before:
                break
            self.tap_both("START", label=f"results-start-{match_index}-{pulse}")
            for _ in range(20):
                if self.stop.wait(0.05):
                    raise CaptureFailure("capture stopped in Results")
                if self.latest["boundaries"].get("return_css", 0) > return_before:
                    break
        self.wait(lambda: self.latest["boundaries"].get("return_css", 0) > return_before,
                  f"natural CSS return for match {match_index}", seconds=30.0)
        if match_index < 2:
            base = self.latest.get("polls", 0)
            self.wait(lambda: self.latest.get("scene_kind") == "08" and
                      self.latest.get("polls", 0) > base + 240,
                      "returned CSS initialization")

    def configure_team_battle(self) -> None:
        self.wait(lambda: len(self.latest["models"]) == 4 and
                  self.latest.get("css_polls", 0) >= 240,
                  "CSS readiness for original Teams setup")
        self.select_human(0, "MARIO", initial=True)
        self.select_cpu(1, "MARIO")
        self.wait(lambda: [player["kind"] for player in self.latest["players"][:2]] == [0, 1],
                  "original human and CPU CSS doors")
        if self.latest["players"][1]["cpu"] != 1:
            raise CaptureFailure("original Mario CPU did not retain the route's level-one setting")
        if self.latest.get("team_state") != {"is_teams": 0, "player_teams": [0, 0]}:
            raise CaptureFailure(
                f"fresh original CSS team defaults differ from the route: {self.latest.get('team_state')}")
        self.move(-30.0, 23.3, "CSS-Teams-toggle")
        self.state("teams-toggle-before")
        self.tap(0, "A", label="enable-source-Teams")
        self.wait(lambda: self.latest.get("team_state") ==
                  {"is_teams": 1, "player_teams": [0, 0]},
                  "original CSS Teams enabled")
        door = self.latest["doors"][1]
        self.move((door["team_left"] + door["team_right"]) / 2, -3.4,
                  "CSS-P2-team-color")
        self.state("p2-team-color-before")
        self.tap(0, "A", label="set-source-P2-team-color")
        self.wait(lambda: self.latest.get("team_state") ==
                  {"is_teams": 1, "player_teams": [0, 1]},
                  "original CSS opposing player teams")
        self.state("team-battle-configured")

    def play_team_battle(self) -> None:
        self.configure_team_battle()
        first_sss = self.latest["boundaries"].get("sss_enter", 0)
        self.enter_fd(choose_stage=False, before=first_sss)
        if self.latest.get("team_state") != {"is_teams": 1, "player_teams": [0, 1]}:
            raise CaptureFailure("original CSS lost Teams or team colors after SSS cancellation")
        self.state("css-after-team-sss-cancel")
        for match_index in range(3):
            before_sss = self.latest["boundaries"].get("sss_enter", 0)
            setup_boundary_before = self.latest["boundaries"].get("setup", 0)
            self.enter_fd(choose_stage=True, before=before_sss)
            self.wait(lambda match_index=match_index:
                      len(self.latest.get("setup_records", [])) > match_index,
                      f"source Teams StartMeleeData {match_index}", seconds=30.0)
            self.wait(lambda setup_boundary_before=setup_boundary_before:
                      self.latest["boundaries"].get("setup", 0) > setup_boundary_before,
                      f"source team match setup boundary {match_index}", seconds=30.0)
            setup = self.latest["setup_records"][match_index]["decoded"]
            self.latest.setdefault("team_match_setups", []).append(setup)
            self.state(f"source-team-match-setup-{match_index}")

            before_polls = self.latest.get("polls", 0)
            self.wait(lambda before_polls=before_polls:
                      self.latest.get("polls", 0) >= before_polls + 180,
                      f"source team match {match_index} reaches 180 PAD polls", seconds=45.0)
            self.tap(0, "START", label=f"team-match-{match_index}-pause")
            self.wait(lambda before_polls=before_polls:
                      self.latest.get("polls", 0) >= before_polls + 225,
                      f"team pause menu {match_index} accepts the No Contest chord",
                      seconds=10.0)
            result_before = self.latest["boundaries"].get("results_gobj", 0)
            chord = capture.raw_pad(buttons=["L", "R", "A", "START"])
            self.controller.set_both(
                chord, capture.NEUTRAL_PAD,
                action=f"team-match-{match_index}-No-Contest-LRAS-Start")
            expected = (self._expected_button(chord), 0)
            press_seq = self.latest.get("consume_seq", -1)
            self.wait(lambda press_seq=press_seq, expected=expected:
                      any(row["seq"] > press_seq and row["buttons"] == expected
                          for row in self.latest.get("consumed_history", [])),
                      f"original source consumed No Contest chord {match_index}", seconds=10.0)
            self.wait(lambda result_before=result_before:
                      self.latest["boundaries"].get("results_gobj", 0) > result_before,
                      f"original Team Results {match_index}", seconds=30.0)
            self.controller.set_both(capture.NEUTRAL_PAD, capture.NEUTRAL_PAD,
                                     action=f"team-match-{match_index}-No-Contest-release")
            self.state(f"original-team-results-{match_index}")

            results_polls = self.latest.get("polls", 0)
            self.wait(lambda results_polls=results_polls:
                      self.latest.get("polls", 0) >= results_polls + 270,
                      f"source Results {match_index} presentation", seconds=20.0)
            return_before = self.latest["boundaries"].get("return_css", 0)
            for pulse in range(8):
                if self.latest["boundaries"].get("return_css", 0) > return_before:
                    break
                self.tap(0, "START", label=f"team-results-start-{match_index}-{pulse}")
                for _ in range(28):
                    if self.stop.wait(0.05):
                        raise CaptureFailure("capture stopped in Team Results")
                    if self.latest["boundaries"].get("return_css", 0) > return_before:
                        break
            self.wait(lambda return_before=return_before:
                      self.latest["boundaries"].get("return_css", 0) > return_before,
                      f"original Team Results {match_index} return to CSS", seconds=30.0)
            self.wait(lambda self=self:
                      self.latest.get("scene_kind") == "08" and
                      self.latest.get("team_state") ==
                      {"is_teams": 1, "player_teams": [0, 1]},
                      f"original CSS retains Teams after Results {match_index}", seconds=30.0)
            retained_hex = self.latest.get("css_start_data", "")
            retained = _decode_css_start_data(retained_hex)
            retained_raw = bytes.fromhex(retained_hex)
            if (retained["is_teams"] is not True or
                    retained["item_frequency"] != -1 or
                    retained["item_mask_hex"] != "fffffffeffffffff" or
                    len(retained["players"]) != 2 or
                    [player["player_type"] for player in retained["players"]] != [0, 1] or
                    retained["players"][1].get("cpu_level") != 1 or
                    any(player["stocks"] != 3 for player in retained["players"]) or
                    [retained_raw[0x69], retained_raw[0x8D]] != [0, 1]):
                raise CaptureFailure(
                    f"original CSS did not retain Team Rules/Items state after match {match_index}: {retained}")
            self.latest.setdefault("team_css_returns", []).append({
                "match_index": match_index,
                "source_sequence": self.latest["boundaries"]["return_css"],
                "start_data": retained,
                "team_state": self.latest.get("team_state"),
            })
            self.state(f"css-after-team-results-settings-retained-{match_index}")


def _consume_row(row: dict[str, Any], latest: dict[str, Any],
                 report: dict[str, Any]) -> None:
    payload = row.get("payload", {})
    boundary = None
    if row.get("event") == "boundary":
        try:
            identity = capture.ObserverTail.boundary(row)
        except capture.CaptureError as error:
            raise CaptureFailure(f"invalid source boundary metadata: {error}") from error
        if identity is None:
            raise CaptureFailure("source boundary event has no validated identity")
        boundary, match_index = identity
        expected_match_index = latest.get("expected_match_index", 0)
        if match_index != expected_match_index:
            raise CaptureFailure(
                f"source boundary {boundary} belongs to match {match_index}; "
                f"expected match {expected_match_index}"
            )
        latest.setdefault("boundary_match_indices", {})[boundary] = match_index
        latest.setdefault("boundaries", {})[boundary] = row["seq"]
        if boundary == "return_css":
            latest["expected_match_index"] = min(
                match_index + 1, len(EXPECTED_ROSTER) - 1
            )
    if boundary == "sss_enter":
        latest["stage_kind"] = None
    if boundary == "pad_poll":
        latest["polls"] = latest.get("polls", 0) + 1
        if latest.get("scene_kind") == "08":
            latest["css_polls"] = latest.get("css_polls", 0) + 1
    slices = {item["name"]: item for item in payload.get("slices", [])}
    for name in ("scene_kind", "scene_routing"):
        if name in slices:
            latest[name] = slices[name]["hex"]
    if "menu_main_flow" in slices:
        raw = bytes.fromhex(slices["menu_main_flow"]["hex"])
        latest.update(main_kind=raw[0], main_confirmed=raw[4],
                      main_selection=int.from_bytes(raw[2:4], "big"),
                      main_polls=latest.get("main_polls", 0) + 1)
    if "menu_main_input" in slices:
        latest["main_cooldown"] = int.from_bytes(
            bytes.fromhex(slices["menu_main_input"]["hex"])[:2], "big")
    if boundary == "pad_consume" and "pad_slot" in slices:
        raw = bytes.fromhex(slices["pad_slot"]["hex"])
        buttons = (int.from_bytes(raw[:2], "big"), int.from_bytes(raw[12:14], "big"))
        history = latest.setdefault("consumed_history", [])
        history.append({"seq": row["seq"], "buttons": buttons})
        if len(history) > 256:
            del history[:-256]
        latest.update(consume_seq=row["seq"], consumed_buttons=buttons,
                      consumed_axes=[(raw[index * 12 + 2], raw[index * 12 + 3])
                                     for index in range(4)])
    for item in payload.get("slices", []):
        name = item["name"]
        raw = bytes.fromhex(item["hex"])
        slot = item.get("flags", 0)
        if name == "menu_css_cursor":
            x, y = struct.unpack(">ff", raw[12:20])
            latest["cursors"][slot] = {"state": raw[5], "held": raw[6], "x": x, "y": y}
            latest["cursor_revisions"][slot] = latest["cursor_revisions"].get(slot, 0) + 1
        elif name == "menu_css_model":
            x, y = struct.unpack(">ff", raw[8:16])
            latest["models"][slot] = {"state": raw[5], "x": x, "y": y}
        elif name == "menu_css_slider":
            latest["sliders"][slot] = {
                "dirty": bool(int.from_bytes(raw[20:24], "big") & 64),
                "x": struct.unpack(">f", raw[80:84])[0],
                "y": struct.unpack(">f", raw[96:100])[0],
            }
        elif name == "menu_css_doors":
            latest["doors"] = [{
                "kind": raw[index * 36 + 11],
                "costume": raw[index * 36 + 13],
                "icon": raw[index * 36 + 14],
                # CSSDoor stores Human/CPU toggle bounds at 0x14/0x18 and
                # team-color bounds at 0x1C/0x20. Keep both controls distinct.
                "left": struct.unpack(">f", raw[index * 36 + 20:index * 36 + 24])[0],
                "right": struct.unpack(">f", raw[index * 36 + 24:index * 36 + 28])[0],
                "team_left": struct.unpack(">f", raw[index * 36 + 28:index * 36 + 32])[0],
                "team_right": struct.unpack(">f", raw[index * 36 + 32:index * 36 + 36])[0],
            } for index in range(4)]
        elif name == "menu_css_live_state":
            latest["css_start_data"] = raw[0x10:0x148].hex()
            latest["team_state"] = {
                "is_teams": raw[0x18],
                "player_teams": [raw[0x79], raw[0x9D]],
            }
            latest["players"] = [{
                "character": raw[112 + index * 36],
                "kind": raw[113 + index * 36],
                "stocks": raw[114 + index * 36],
                "cpu": raw[127 + index * 36],
            } for index in range(6)]
        elif name == "menu_css_context":
            # The return_css boundary carries this same source CSSData owner
            # after Results, while no live-state sample is emitted during the
            # transition. Use its typed +0x10 StartMeleeData copy so the
            # retained-settings check observes the returned CSS state.
            if len(raw) != 0x148:
                raise CaptureFailure(
                    f"source CSS context has unexpected size {len(raw)}")
            latest["css_start_data"] = raw[0x10:0x148].hex()
        elif name == "stage_select_index":
            stage = slices.get("stage_select_kind")
            latest["stage_kind"] = int(stage["hex"], 16) if stage else None
    if boundary in {"css_enter", "css_cancel_enter", "return_css"}:
        latest["css_polls"] = 0
        report.setdefault("css_entries", []).append(row)
        latest["css_entry_count"] = len(report["css_entries"])
        if boundary == "css_enter" and "css_entry" not in report:
            report["css_entry"] = row
            if latest.get("scene_routing") != "020201000000":
                raise CaptureFailure("first CSS was not the original VS route")
    if boundary == "entry":
        match_index = payload.get("match_index")
        setup_slice = slices.get("match_setup")
        if type(match_index) is not int or setup_slice is None:
            raise CaptureFailure("source match entry omitted its indexed StartMeleeData")
        setup_hex = setup_slice["hex"]
        normalized = (_validate_team_setup(setup_hex, match_index)
                      if latest.get("team_route") else
                      _validate_setup(setup_hex, match_index))
        latest.setdefault("setup_records", []).append({
            "match_index": match_index,
            "source_sequence": row["seq"],
            "raw_hex": setup_hex,
            "decoded": normalized,
        })
    if boundary == "results_gobj" and latest.get("team_route"):
        result_slice = slices.get("result")
        if result_slice is None:
            raise CaptureFailure("Team Results GObj omitted the source result slice")
        match_index = payload.get("match_index")
        results = latest.setdefault("team_results", [])
        if type(match_index) is not int or not 0 <= match_index < 3:
            raise CaptureFailure(
                f"Team Results GObj has an unexpected source match index: {match_index}")
        decoded_result = _decode_team_result(result_slice["hex"])
        if match_index == len(results):
            results.append({"match_index": match_index,
                            "source_sequence": row["seq"],
                            "result": decoded_result})
        elif (match_index != len(results) - 1 or
              results[-1]["result"] != decoded_result):
            raise CaptureFailure(
                f"Team Results GObj order or published result changed: {match_index}")
        result_rows = latest.setdefault("team_result_gobj_rows", [])
        if not any(result_row["payload"].get("match_index") == match_index
                   for result_row in result_rows):
            result_rows.append(row)
    if row.get("event") == "error":
        raise CaptureFailure(f"reference observer error: {payload}")


def _args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dolphin", required=True, type=Path,
                        help="fresh pinned passive reference Dolphin executable")
    parser.add_argument("--build-manifest", required=True, type=Path,
                        help="the exact build receipt for --dolphin")
    parser.add_argument("--disc", required=True, type=Path,
                        help="owned GALE01 revision-2 disc image")
    parser.add_argument("--profile-source", required=True, type=Path,
                        help="read-only source profile containing the accepted GCI and SRAM")
    parser.add_argument("--out", required=True, type=Path,
                        help="new evidence directory beneath this checkout's ignored work/")
    parser.add_argument("--capture-id", required=True,
                        help="unique MWRO identity using letters, numbers, dot, underscore, hyphen")
    parser.add_argument("--readiness-only", action="store_true",
                        help="cycle three source-confirmed CPU9 lineups through CSS/SSS without matches")
    parser.add_argument("--team-route", action="store_true",
                        help="capture three original VS Rules/Items Teams matches through Results/CSS returns")
    return parser.parse_args()


def main() -> int:
    args = _args()
    if args.readiness_only and args.team_route:
        raise CaptureFailure("--readiness-only and --team-route are separate capture modes")
    out = args.out.expanduser().resolve()
    if ROOT / "work" not in out.parents:
        raise CaptureFailure("--out must be inside this checkout's ignored work/ directory")
    if out.exists():
        raise CaptureFailure(f"evidence output already exists: {out}")
    if not CAPTURE_ID_PATTERN.fullmatch(args.capture_id):
        raise CaptureFailure("--capture-id contains characters outside the observer identity grammar")
    dolphin = args.dolphin.expanduser().resolve()
    manifest_path = args.build_manifest.expanduser().resolve()
    disc = args.disc.expanduser().resolve()
    profile_source = args.profile_source.expanduser().resolve()
    if not all(path.is_file() for path in (dolphin, manifest_path, disc)):
        raise CaptureFailure("Dolphin, build manifest, and disc must be regular files")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    expected_identity = {
        "game_revision": "GALE01r2",
        "dol_sha1": DOL_SHA1,
        "dol_sha256": DOL_SHA256,
        "cpu": "JITARM64",
        "writes_guest_memory": False,
    }
    if (manifest.get("schema") != "melee-web-reference-dolphin-build" or
            manifest.get("version") != 1 or manifest.get("target") != "dolphin-nogui" or
            manifest.get("dolphin_commit") != DOLPHIN_COMMIT or
            manifest.get("observer_identity") != expected_identity or
            Path(manifest.get("binary", "")).expanduser().resolve() != dolphin or
            manifest.get("binary_sha256") != _sha256(dolphin)):
        raise CaptureFailure("build receipt does not match the pinned passive GALE01 JITARM64 binary")
    if _sha256(disc) != DISC_SHA256:
        raise CaptureFailure("disc image does not match the verified GALE01 revision-2 input")
    profile = profile_source / "GC" / "USA" / "Card A" / PROFILE_GCI
    sram = profile_source / "GC" / "SRAM.raw"
    if not (profile_source / "Config").is_dir() or not profile.is_file() or not sram.is_file():
        raise CaptureFailure("profile source is missing Config, the accepted GCI, or SRAM")
    if _sha256(profile) != PROFILE_GCI_SHA256 or _sha256(sram) != PROFILE_SRAM_SHA256:
        raise CaptureFailure("profile source does not match the accepted read-only source fixture")

    out.mkdir(parents=False)
    user = out / "user"
    user.mkdir(mode=0o700)
    shutil.copytree(profile_source / "Config", user / "Config")
    card = user / "GC" / "USA" / "Card A"
    card.mkdir(parents=True)
    shutil.copy2(profile, card / PROFILE_GCI)
    gc = user / "GC"
    gc.mkdir(exist_ok=True)
    shutil.copy2(sram, gc / "SRAM.raw")
    p1, p2 = capture.prepare_dual_pipe(user)

    stream = out / "capture.mwro"
    status = out / "status.json"
    input_stream = out / "controller-input.mwri"
    input_status = out / "input-status.json"
    log_path = out / "dolphin.log"
    report_path = out / "report.json"
    env = os.environ.copy()
    env.update({
        "MWRC_ENABLE": "1",
        "MWRC_OUTPUT": str(stream),
        "MWRC_STATUS": str(status),
        "MWRC_DOL_SHA256": DOL_SHA256,
        "MWRC_CPU": "JITARM64",
        "MWRC_SOURCE_REV": "GALE01r2",
        "MWRC_WHOLE_SESSION_MATCHES": "3",
        "MWRC_CAPTURE_ID": args.capture_id,
        "MWRC_SEQUENCE_ID": args.capture_id,
        "MWRC_INPUT_RECORD": str(input_stream),
        "MWRC_INPUT_STATUS": str(input_status),
        "LANG": "en_US.UTF-8",
    })
    command = [str(dolphin), "-p", "headless", "-v", "Null", "-u", str(user), "-e", str(disc)]
    settings = (
        "Dolphin.Interface.ConfirmStop=False",
        "Dolphin.Input.BackgroundInput=True",
        "Dolphin.Core.CPUCore=4",
        "Dolphin.Core.CPUThread=False",
        "Dolphin.Core.EnableCheats=False",
        "Dolphin.Core.EnableCustomRTC=True",
        "Dolphin.Core.CustomRTCValue=1704067200",
        "Dolphin.Core.EmulationSpeed=2",
        "Session.Core.SaveDataWritable=False",
        f"Dolphin.Core.GCIFolderAPathOverride={card}",
    )
    for setting in settings:
        command.extend(("-C", setting))
    command.extend(dolphin_audio_options())
    version = subprocess.run([str(dolphin), "--version"], capture_output=True,
                             text=True, check=True).stdout.strip()
    log = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    stop = threading.Event()
    controller = capture.DualPipeController(p1, p2, out / "input-intentions.jsonl")
    latest: dict[str, Any] = {
        "cursors": {}, "cursor_revisions": {}, "models": {}, "sliders": {},
        "doors": [], "players": [], "boundaries": {}, "boundary_match_indices": {},
        "expected_match_index": 0, "setup_records": [], "polls": 0,
        "team_route": args.team_route,
    }
    report: dict[str, Any] = {
        "schema": "melee-web-recorded-session-12-character-capture-v1",
        "capture_id": args.capture_id,
        "scope": ("three original CSS/SSS Team match cycles after the committed row-zero item toggle and None frequency; each is Mario vs level-1 Mario CPU, source No Contest Results and retained CSS; Null video; no pixel, PCM, physical-input, or performance claim"
                  if args.team_route else
                  "three-lineup CSS/SSS route readiness only; no matches started"
                  if args.readiness_only else
                  "one continuous three-match original CPU9 session; Null video; no pixels, PCM, or performance claim"),
        "route": "team-battle-results-css-roundtrips" if args.team_route else "three-lineup-cpu9",
        "lineups": ([ ["MARIO", "MARIO"] for _ in range(3)] if args.team_route else
                    [list(lineup) for lineup in LINEUPS]),
        "dolphin_executable_sha256": _sha256(dolphin),
        "build_manifest_sha256": _sha256(manifest_path),
        "disc_sha256": DISC_SHA256,
        "profile_fixture_sha256": {
            PROFILE_GCI: PROFILE_GCI_SHA256,
            "GC/SRAM.raw": PROFILE_SRAM_SHA256,
        },
        "dolphin_version": version,
        "input_mode": "ordinary-controller-pipe-record",
        "readiness_only": args.readiness_only,
        "team_route": args.team_route,
        "audio_policy": "host output muted; DSP generation retained",
    }
    driver = Driver(controller, latest, stop, readiness_only=args.readiness_only,
                    team_route_only=args.team_route)
    ended = False
    try:
        timeout = time.monotonic() + (900 if args.readiness_only or args.team_route else 2400)
        announcements: list[dict[str, Any]] = []

        def retain_announcement(row: dict[str, Any]) -> None:
            if row.get("event") in {"handshake", "start"}:
                announcements.append(row)

        with capture.ObserverTail(stream, status, sink=retain_announcement) as observer:
            observer.require_announcements(3, timeout, capture_id=args.capture_id,
                                            sequence_id=args.capture_id)
            identity = observer.identity or {}
            handshake = announcements[0]["payload"] if announcements else {}
            if (identity.get("capture_id") != args.capture_id or
                    identity.get("sequence_id") != args.capture_id or
                    handshake.get("schema") != "melee-web-passive-dolphin-observer" or
                    handshake.get("dolphin_commit") != DOLPHIN_COMMIT or
                    handshake.get("dol_sha256") != DOL_SHA256 or
                    handshake.get("writes_guest_memory") is not False):
                raise CaptureFailure("observer handshake does not match requested capture identity")
            report["observer_identity"] = identity
            report["observer_handshake"] = handshake
            thread = threading.Thread(target=driver.boot_and_drive, name="original-css-driver")
            thread.start()
            while time.monotonic() < timeout:
                if latest.get("driver_error"):
                    raise CaptureFailure(latest["driver_error"])
                if args.readiness_only and latest.get("driver_complete"):
                    report["result"] = "source_css_sss_route_readiness_complete"
                    break
                row = observer.next(timeout)
                _consume_row(row, latest, report)
                if row.get("event") == "end":
                    observer.require_completed_status()
                    status_data = json.loads(status.read_text(encoding="utf-8"))
                    input_data = json.loads(input_status.read_text(encoding="utf-8"))
                    if (not input_data.get("complete") or input_data.get("invalid") or
                            input_data.get("error")):
                        raise CaptureFailure(f"source-consumed input record is incomplete: {input_data}")
                    expected_setups = 3
                    if len(latest.get("setup_records", [])) != expected_setups:
                        raise CaptureFailure(
                            f"completed source session omitted setups: expected {expected_setups}")
                    if not args.team_route:
                        replay.validate_milestone_setups(
                            [entry["raw_hex"] for entry in latest["setup_records"]])
                    else:
                        result_rows = latest.get("team_result_gobj_rows", [])
                        team_results = latest.get("team_results", [])
                        team_setups = latest.get("team_match_setups", [])
                        css_returns = latest.get("team_css_returns", [])
                        if any(len(rows) != 3 for rows in
                               (result_rows, team_results, team_setups, css_returns)):
                            raise CaptureFailure(
                                "three-match Team Results/CSS route omitted a source boundary")
                        report["team_result_gobjs"] = result_rows
                        report["team_results"] = team_results
                        report["team_match_setups"] = team_setups
                        report["team_css_returns"] = css_returns
                        report["css_team_state_after_results"] = latest.get("team_state")
                        report["css_rules_after_results"] = css_returns[-1]["start_data"]
                    report["observer_status"] = status_data
                    report["input_status"] = input_data
                    report["result"] = ("original_vs_team_results_css_capture_complete"
                                         if args.team_route else
                                         "three_match_source_capture_complete")
                    report["end"] = row
                    ended = True
                    break
            else:
                raise CaptureFailure("source capture exceeded its absolute deadline")
        if "thread" in locals():
            thread.join(10)
            if thread.is_alive():
                raise CaptureFailure("ordinary-input driver did not stop after the declared outcome")
        report["steps"] = driver.steps
        report["setup_records"] = latest.get("setup_records", [])
        report["observer_counts"] = observer.counts
        report["latest_state"] = {
            key: latest.get(key) for key in
            ("scene_kind", "scene_routing", "players", "doors", "team_state",
             "setup_records", "boundaries")
        }
        if not args.readiness_only and not ended:
            raise CaptureFailure("full source capture did not reach the observer end record")
    except Exception as error:
        report["result"] = "fail"
        report["error"] = str(error)
        report["steps"] = driver.steps
        report["setup_records"] = latest.get("setup_records", [])
        report["latest_state"] = {
            key: latest.get(key) for key in
            ("scene_kind", "scene_routing", "players", "doors", "team_state",
             "setup_records", "boundaries")
        }
        print(f"recorded-session source capture failed: {error}", file=sys.stderr)
    finally:
        stop.set()
        if "thread" in locals():
            thread.join(2)
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        log.close()
        report["process_exit"] = proc.returncode
        report["raw_observer_sha256"] = _sha256(stream) if stream.exists() else None
        report["raw_input_sha256"] = _sha256(input_stream) if input_stream.exists() else None
        if status.exists():
            report["final_observer_status"] = json.loads(status.read_text(encoding="utf-8"))
        if input_status.exists():
            report["final_input_status"] = json.loads(input_status.read_text(encoding="utf-8"))
        _write_json(report_path, report)
    print(json.dumps({"result": report.get("result"), "error": report.get("error"),
                      "out": str(out)}, sort_keys=True))
    return _capture_exit_code(report.get("result"))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CaptureFailure as error:
        print(f"recorded-session source capture failed: {error}", file=sys.stderr)
        raise SystemExit(2)
