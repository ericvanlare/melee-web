"""Deterministic synthetic per-tick input script for the networked-session
determinism checks (Track A1).

The script is open loop: every frame is a pure function of the tick index and
the script seed, so two peers (or a peer and a negative control) can be given
byte-identical input without any observation of the game. It holds two human
ports; ports 3 and 4 report no controller. It is a workload for the agreed-input
seam, never expected state, and carries no retail-equivalence claim.

Route (original scenes in order): character select, where both human cursors
move and port 1 presses Start; stage select, where port 1 moves to Final
Destination and presses A; a human-versus-human stock match with
pseudo-random inputs on both ports; Results, where both humans press Start at
the calibrated Results ticks until the original return to character select;
then neutral ticks in that final CSS.

The CSS and SSS timings were calibrated once with closed-loop observation of
the real owners (cursor geometry and Final Destination's tile bounds). The
calibration only produced the constants below; the shipped script has no
feedback. The harness reports the scene each tick actually ran in, so a route
that does not reach the expected scenes fails visibly.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

MAGIC = b"MWNI"
VERSION = 1
HEADER = struct.Struct(">4sIII")
FRAME_BYTES = 44
PORT_BYTES = 11
PORTS = 4
SCHEMA = "melee-web-net-input-script-v1"
MAX_FRAMES = 216000

# GameCube PAD button bits.
BUTTON_LEFT, BUTTON_RIGHT, BUTTON_DOWN, BUTTON_UP = 0x0001, 0x0002, 0x0004, 0x0008
BUTTON_Z, BUTTON_R, BUTTON_L = 0x0010, 0x0020, 0x0040
BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_START = 0x0100, 0x0200, 0x0400, 0x0800, 0x1000
BUTTON_MASK = 0x1F7F

PAD_ERR_NONE = 0
PAD_ERR_NO_CONTROLLER = -1

# Calibrated route constants (global tick indexes, see module docstring).
CSS_START_PRESS_TICK = 150
CSS_START_PRESS_TICKS = 3
SSS_FIRST_TICK = 152
SSS_NEUTRAL_TICKS = 30
SSS_MOVES_FIRST_TICK = CSS_START_PRESS_TICK + CSS_START_PRESS_TICKS + SSS_NEUTRAL_TICKS
# (ticks, stickX, stickY) for the SSS cursor, then the final approach to Final
# Destination's tile. The closed-loop calibration produced the last two rows.
SSS_CURSOR_MOVES = ((14, 80, 0), (5, 0, 80), (4, -80, -80), (7, -80, 0))
SSS_CONFIRM_TICKS = 3
MATCH_FIRST_TICK = 304
# Both humans press Start for RESULTS_START_TICKS at these Results-relative
# ticks (the original page advances); the second CSS then begins
# RESULTS_EXIT_TICKS after Results began (calibrated with closed-loop
# observation: the last pulse is consumed at tick 600).
RESULTS_START_PULSES = (180, 360, 600)
RESULTS_START_TICKS = 10
RESULTS_EXIT_TICKS = 612

# CSS cursor wandering before Start: (first tick, ticks, port, stickX, stickY).
CSS_WANDER = (
    (20, 25, 0, 80, 0), (50, 12, 0, 0, 80), (66, 20, 0, -80, -40), (90, 10, 0, 40, 0),
    (24, 20, 1, -80, 20), (48, 14, 1, 0, 80), (66, 22, 1, 60, 0), (92, 12, 1, 0, -60),
)

MASK64 = (1 << 64) - 1


class SplitMix64:
    """Platform-independent generator; the script never uses `random`."""

    def __init__(self, seed: int) -> None:
        self.state = seed & MASK64

    def next(self) -> int:
        self.state = (self.state + 0x9E3779B97F4A7C15) & MASK64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
        return z ^ (z >> 31)

    def below(self, bound: int) -> int:
        return self.next() % bound

    def pick(self, weighted):
        total = sum(weight for _, weight in weighted)
        roll = self.below(total)
        for item, weight in weighted:
            if roll < weight:
                return item
            roll -= weight
        raise AssertionError("unreachable")


@dataclass(frozen=True)
class Pad:
    buttons: int = 0
    stick_x: int = 0
    stick_y: int = 0
    sub_x: int = 0
    sub_y: int = 0
    trigger_l: int = 0
    trigger_r: int = 0
    error: int = PAD_ERR_NONE

    def encode(self) -> bytes:
        if self.buttons & ~BUTTON_MASK:
            raise ValueError("button bit outside the PAD mask")
        for value in (self.stick_x, self.stick_y, self.sub_x, self.sub_y):
            if not -128 <= value <= 127:
                raise ValueError("stick value outside signed byte range")
        if self.error == PAD_ERR_NO_CONTROLLER and any(
                (self.buttons, self.stick_x, self.stick_y, self.sub_x, self.sub_y,
                 self.trigger_l, self.trigger_r)):
            raise ValueError("a disconnected port must carry neutral input")
        return struct.pack(">HbbbbBBBBb", self.buttons, self.stick_x, self.stick_y,
                           self.sub_x, self.sub_y, self.trigger_l, self.trigger_r,
                           0, 0, self.error)


NEUTRAL = Pad()
ABSENT = Pad(error=PAD_ERR_NO_CONTROLLER)


def frame(port0: Pad = NEUTRAL, port1: Pad = NEUTRAL) -> bytes:
    data = port0.encode() + port1.encode() + ABSENT.encode() + ABSENT.encode()
    assert len(data) == FRAME_BYTES
    return data


# Match actions: (name, weight). Each action expands to per-tick pads below.
MATCH_ACTIONS = (
    ("idle", 6), ("walk_left", 7), ("walk_right", 7), ("dash_left", 6),
    ("dash_right", 6), ("jump", 6), ("jump_left", 3), ("jump_right", 3),
    ("attack", 8), ("tilt", 5), ("special", 6), ("special_up", 3),
    ("special_down", 2), ("shield", 4), ("grab", 3), ("c_stick", 5),
    ("crouch", 3),
)


def _action_pads(rng: SplitMix64, action: str, ticks: int) -> list[Pad]:
    hold = 1 + rng.below(6)
    offset = rng.below(4)
    if action == "c_stick":
        direction = ((80, 0), (-80, 0), (0, 80), (0, -80))[rng.below(4)]
        return [Pad(sub_x=direction[0], sub_y=direction[1]) if offset <= tick < offset + hold
                else NEUTRAL for tick in range(ticks)]
    magnitude = (40, 64, 80)[rng.below(3)]
    pads = []
    for tick in range(ticks):
        pressed = offset <= tick < offset + hold
        kwargs = {}
        if action == "walk_left":
            kwargs = {"stick_x": -magnitude}
        elif action == "walk_right":
            kwargs = {"stick_x": magnitude}
        elif action == "dash_left":
            kwargs = {"stick_x": -80 if tick % 6 < 4 else 0}
        elif action == "dash_right":
            kwargs = {"stick_x": 80 if tick % 6 < 4 else 0}
        elif action == "crouch":
            kwargs = {"stick_y": -80}
        elif action in ("jump", "jump_left", "jump_right"):
            direction = {"jump": 0, "jump_left": -magnitude, "jump_right": magnitude}[action]
            kwargs = {"stick_x": direction}
            if pressed:
                kwargs["buttons"] = BUTTON_X if hold % 2 else BUTTON_Y
        elif action == "attack":
            kwargs = {"stick_x": (-1, 0, 1)[hold % 3] * magnitude}
            if pressed:
                kwargs["buttons"] = BUTTON_A
        elif action == "tilt":
            kwargs = {"stick_x": (-1, 1)[offset % 2] * 40, "stick_y": (-1, 0, 1)[hold % 3] * 40}
            if pressed:
                kwargs["buttons"] = BUTTON_A
        elif action in ("special", "special_up", "special_down"):
            kwargs = {"stick_x": (-1, 0, 1)[offset % 3] * magnitude}
            if action == "special_up":
                kwargs["stick_y"] = 80
            elif action == "special_down":
                kwargs["stick_y"] = -80
            if pressed:
                kwargs["buttons"] = BUTTON_B
        elif action == "shield":
            if pressed or tick > offset + hold:
                kwargs = {"buttons": BUTTON_R, "trigger_r": 200 if magnitude == 80 else 70}
        elif action == "grab":
            if pressed:
                kwargs = {"buttons": BUTTON_Z}
        pads.append(Pad(**kwargs))
    return pads


def match_stream(seed: int, port: int, ticks: int) -> list[Pad]:
    rng = SplitMix64((seed << 8 | (port + 1)) ^ 0xA1A1A1A1A1A1A1A1)
    pads: list[Pad] = []
    while len(pads) < ticks:
        action = rng.pick(MATCH_ACTIONS)
        duration = 4 + rng.below(36)
        pads.extend(_action_pads(rng, action, duration))
    return pads[:ticks]


@dataclass
class ScriptParameters:
    seed: int
    match_end_tick: int
    tail_ticks: int = 180

    def validate(self) -> None:
        if not 0 <= self.seed < 1 << 32:
            raise ValueError("seed must be a u32")
        if self.match_end_tick <= MATCH_FIRST_TICK:
            raise ValueError("match_end_tick must follow the calibrated match entry tick")
        if self.tail_ticks < 1:
            raise ValueError("the final character-select phase needs at least one tick")


def generate(params: ScriptParameters) -> tuple[bytes, dict]:
    params.validate()
    frames: list[bytes] = []
    phases = []

    def phase(name: str, count: int) -> None:
        phases.append({"name": name, "first_tick": len(frames) - count, "ticks": count})

    # Character select, then stage select, as one tick-indexed table. The
    # scene boundary below is the calibrated tick at which Start was consumed.
    premap: list[tuple[Pad, Pad]] = [(NEUTRAL, NEUTRAL)] * MATCH_FIRST_TICK
    for first, ticks, port, x, y in CSS_WANDER:
        for tick in range(first, first + ticks):
            pair = list(premap[tick])
            pair[port] = Pad(stick_x=x, stick_y=y)
            premap[tick] = (pair[0], pair[1])
    for tick in range(CSS_START_PRESS_TICK, CSS_START_PRESS_TICK + CSS_START_PRESS_TICKS):
        premap[tick] = (Pad(buttons=BUTTON_START), NEUTRAL)
    tick = SSS_MOVES_FIRST_TICK
    for ticks, x, y in SSS_CURSOR_MOVES:
        for _ in range(ticks):
            premap[tick] = (Pad(stick_x=x, stick_y=y), NEUTRAL)
            tick += 1
    for _ in range(SSS_CONFIRM_TICKS):
        premap[tick] = (Pad(buttons=BUTTON_A), NEUTRAL)
        tick += 1
    if tick > MATCH_FIRST_TICK:
        raise AssertionError("stage-select moves overrun the calibrated match entry")
    frames.extend(frame(*pair) for pair in premap)
    phases.append({"name": "css", "first_tick": 0, "ticks": SSS_FIRST_TICK})
    phases.append({"name": "sss", "first_tick": SSS_FIRST_TICK,
                   "ticks": MATCH_FIRST_TICK - SSS_FIRST_TICK})

    # Human-versus-human match with pseudo-random inputs on ports 1 and 2.
    count = params.match_end_tick - MATCH_FIRST_TICK
    port0 = match_stream(params.seed, 0, count)
    port1 = match_stream(params.seed, 1, count)
    frames.extend(frame(a, b) for a, b in zip(port0, port1))
    phase("match", count)

    # Results: both humans press Start at the calibrated Results ticks.
    for tick in range(RESULTS_EXIT_TICKS):
        pressed = any(first <= tick < first + RESULTS_START_TICKS for first in RESULTS_START_PULSES)
        pad = Pad(buttons=BUTTON_START) if pressed else NEUTRAL
        frames.append(frame(pad, pad))
    phase("results", RESULTS_EXIT_TICKS)

    # Final character select.
    frames.extend(frame() for _ in range(params.tail_ticks))
    phase("css-return", params.tail_ticks)

    if len(frames) > MAX_FRAMES:
        raise ValueError("script exceeds the networked frame bound")
    body = b"".join(frames)
    manifest = {
        "schema": SCHEMA,
        "version": VERSION,
        "seed": params.seed,
        "frames": len(frames),
        "frame_bytes": FRAME_BYTES,
        "human_ports": [0, 1],
        "phases": phases,
        "expected_scene_starts": [
            {"scene": 1, "first_tick": 0}, {"scene": 2, "first_tick": SSS_FIRST_TICK},
            {"scene": 3, "first_tick": MATCH_FIRST_TICK},
            {"scene": 4, "first_tick": params.match_end_tick},
            {"scene": 1, "first_tick": params.match_end_tick + RESULTS_EXIT_TICKS}],
        "match_end_tick": params.match_end_tick,
        "results_start_pulses": list(RESULTS_START_PULSES),
        "frames_sha256": hashlib.sha256(body).hexdigest(),
        "scope": "Synthetic open-loop input workload; not expected state and not retail input.",
    }
    return body, manifest


def encode_script(body: bytes) -> bytes:
    if len(body) % FRAME_BYTES or not body:
        raise ValueError("script body must hold whole 44-byte frames")
    return HEADER.pack(MAGIC, VERSION, len(body) // FRAME_BYTES, 0) + body


def decode_script(data: bytes) -> bytes:
    if len(data) < HEADER.size:
        raise ValueError("script is shorter than its header")
    magic, version, count, reserved = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION or reserved != 0:
        raise ValueError("not an MWNI v1 script")
    body = data[HEADER.size:]
    if len(body) != count * FRAME_BYTES or not 0 < count <= MAX_FRAMES:
        raise ValueError("script frame count disagrees with its length")
    for index in range(count):
        base = index * FRAME_BYTES
        for port in range(PORTS):
            record = body[base + port * PORT_BYTES:base + (port + 1) * PORT_BYTES]
            buttons = int.from_bytes(record[:2], "big")
            error = int.from_bytes(record[10:11], "big", signed=True)
            if buttons & ~BUTTON_MASK or error not in (PAD_ERR_NONE, PAD_ERR_NO_CONTROLLER):
                raise ValueError(f"invalid PAD record at frame {index} port {port}")
            if error == PAD_ERR_NO_CONTROLLER and any(record[:10]):
                raise ValueError(f"disconnected port carries input at frame {index} port {port}")
    return body


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seed", type=lambda text: int(text, 0), required=True)
    parser.add_argument("--match-end-tick", type=int, required=True,
                        help="global tick where the original Results scene begins (calibrated)")
    parser.add_argument("--tail-ticks", type=int, default=180)
    parser.add_argument("--out", type=Path, required=True, help="new .mwni script path")
    parser.add_argument("--manifest", type=Path, help="new JSON manifest path")
    args = parser.parse_args(argv)
    params = ScriptParameters(seed=args.seed, match_end_tick=args.match_end_tick,
                              tail_ticks=args.tail_ticks)
    body, manifest = generate(params)
    encoded = encode_script(body)
    manifest["script_sha256"] = hashlib.sha256(encoded).hexdigest()
    with args.out.open("xb") as handle:
        handle.write(encoded)
    if args.manifest:
        with args.manifest.open("x", encoding="utf-8") as handle:
            json.dump(manifest, handle, indent=2)
            handle.write("\n")
    json.dump(manifest, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
