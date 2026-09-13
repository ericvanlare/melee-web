"""Fixed-width source-input workload transport for the Wasm runner.

The transport deliberately contains no expected post-frame state. A Slippi
file is useful here as a repeatable input workload and source identity record;
it is not an accuracy oracle or a trusted source-match initializer.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import struct

from slippi_format import SlippiFormatError, SlippiTimeline


MAGIC = b"MWRP"
SCHEMA_VERSION = 2

# v2 flags describe the payload, rather than enabling comparison. Keep the old
# bit as an explicitly unsupported marker so a hand-edited comparison payload
# cannot be treated as a successful workload.
FLAG_PHYSICAL_INPUT = 1 << 0
FLAG_DERIVED_SETUP = 1 << 1
FLAG_EXPECTED_STATE = 1 << 15  # legacy/unsupported; never emitted

# magic, schema, flags, Slippi version, frame count, first/last frame,
# initial seed, workload stage, source stage, player count, reserved, SHA-256.
HEADER = struct.Struct(">4sHH4sIiiIHHBB32s")
# source port, source character, workload character, source player type,
# source stocks, derived workload stocks, costume, reserved.
PLAYER = struct.Struct(">BBBBBBBx")
FRAME = struct.Struct(">i")
PAD = struct.Struct(">HbbbbBBBBH")

GAME_INFO_SIZE = 0x138
MAX_FRAME_COUNT = 36000


def _digest(source_sha256: str) -> bytes:
    if len(source_sha256) != 64:
        raise ValueError("source SHA-256 must contain 64 hexadecimal characters")
    try:
        value = bytes.fromhex(source_sha256)
    except ValueError as error:
        raise ValueError("source SHA-256 is not hexadecimal") from error
    if len(value) != 32:
        raise ValueError("source SHA-256 must decode to 32 bytes")
    if not any(value):
        raise ValueError("source SHA-256 cannot be all zeroes")
    return value


def _selected_characters(timeline: SlippiTimeline,
                         characters: tuple[int, int] | None):
    players = timeline.header.players
    selected = tuple(player["character_id"] for player in players) \
        if characters is None else tuple(characters)
    if len(selected) != 2 or any(not 0 <= value <= 255 for value in selected):
        raise ValueError("exactly two byte-sized character IDs are required")
    return selected


def encode_transport(timeline: SlippiTimeline, source_sha256: str, *,
                     characters: tuple[int, int] | None = None,
                     stage: int | None = None):
    """Encode a source-input-only v2 workload.

    Character and stage arguments describe the workload setup. They are
    independent of the source Game Info values, which remain in the normalized
    JSON and in the transport identity section. Stock count, timer and item
    rules are always derived by the runtime and are intentionally not copied
    from the source replay.
    """
    players = timeline.header.players
    if len(players) != 2 or timeline.header.record()["is_teams"]:
        raise SlippiFormatError("replay transport currently requires two-player singles")
    if tuple(player["port"] for player in players) != (1, 2):
        raise SlippiFormatError(
            "replay transport only supports source ports 1 and 2 mapped to runtime slots 0 and 1")
    if any(value.is_follower for frame in timeline.frames for value in frame.inputs):
        raise SlippiFormatError("replay transport does not yet support follower inputs")
    if not timeline.frames or len(timeline.frames) > MAX_FRAME_COUNT:
        raise SlippiFormatError(
            f"replay transport frame count must be between 1 and {MAX_FRAME_COUNT}")

    digest = _digest(source_sha256)
    source_record = timeline.header.record()
    source_stage = source_record["stage_id"]
    if not 0 <= source_stage <= 0xFFFF:
        raise ValueError("source stage ID must fit uint16")
    selected_stage = source_stage if stage is None else stage
    if not 0 <= selected_stage <= 0xFFFF:
        raise ValueError("workload stage ID must fit uint16")
    selected_characters = _selected_characters(timeline, characters)

    game_info = bytes.fromhex(source_record["game_info_hex"])
    if len(game_info) != GAME_INFO_SIZE:
        raise SlippiFormatError(
            f"source Game Info must contain exactly 0x{GAME_INFO_SIZE:x} bytes")
    if int.from_bytes(game_info[0xE:0x10], "big") != source_stage:
        raise SlippiFormatError("source Game Info stage does not match source header")

    first_frame = timeline.frames[0].number
    last_frame = timeline.frames[-1].number
    span = last_frame - first_frame + 1
    if span != len(timeline.frames):
        raise SlippiFormatError("replay transport source frames are not contiguous")

    # Every v2 transport is a derived workload. In particular, never infer
    # comparison eligibility from a UCF-off header or from absent overrides.
    flags = FLAG_PHYSICAL_INPUT | FLAG_DERIVED_SETUP
    output = bytearray(HEADER.pack(
        MAGIC, SCHEMA_VERSION, flags, bytes(timeline.header.version),
        len(timeline.frames), first_frame, last_frame,
        timeline.header.random_seed, selected_stage, source_stage,
        len(players), 0, digest))
    output += game_info
    for index, player in enumerate(players):
        output += PLAYER.pack(
            player["port"], player["character_id"], selected_characters[index],
            player["player_type"], player["stocks"], 4, player["costume"])
    for frame in timeline.frames:
        output += FRAME.pack(frame.number)
        inputs = {(value.port, value.is_follower): value for value in frame.inputs}
        for source_port in (1, 2):
            value = inputs.get((source_port, False))
            if value is None:
                raise SlippiFormatError(
                    f"frame {frame.number} is missing source port {source_port} input")
            pad = value.reconstructed_pad()
            output += PAD.pack(
                pad["buttons"], *pad["stick"], *pad["cstick"],
                *pad["triggers"], 0, 0, 0)
    return bytes(output)


def source_digest(path: Path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()
