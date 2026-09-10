"""Focused normalization checks independent of the corpus byte assets."""

import struct
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from slippi_format import (  # noqa: E402
    FIRST_FRAME,
    FRAME_BOOKEND,
    GAME_END,
    GAME_START,
    PRE_FRAME,
    POST_FRAME,
    RAW_PREFIX,
    SlippiFormatError,
    decode_timeline,
)


def _game_start(*, game_mode=8, players=(2, 18), version=(3, 19, 0, 0)):
    payload_size = 0x2F9 if version >= (3, 17, 0, 0) else 0x1A4
    event = bytearray(1 + payload_size)
    event[0] = GAME_START
    event[1:5] = bytes(version)
    info = memoryview(event)[5:5 + 0x138]
    struct.pack_into(">H", info, 0xE, 31)
    for index, character in enumerate(players):
        offset = 0x60 + 0x24 * index
        info[offset:offset + 4] = bytes((character, 0, 4, index))
    for index in range(len(players), 4):
        info[0x61 + 0x24 * index] = 3
    struct.pack_into(">I", event, 0x13D, 0x12345678)
    for index in range(4):
        struct.pack_into(">II", event, 0x141 + 8 * index, 0, 0)
    event[0x1A1] = 0
    event[0x1A2] = 0
    event[0x1A3] = 2
    event[0x1A4] = game_mode
    return bytes(event)


def _pre(frame, port, *, buttons=0x100, payload_size=0x42):
    event = bytearray(1 + payload_size)
    event[0] = PRE_FRAME
    struct.pack_into(">iBBI", event, 1, frame, port, 0,
                     0xA0000000 + frame)
    struct.pack_into(">H", event, 0xB, 14)
    for offset, value in zip(
            (0xD, 0x11, 0x15, 0x19, 0x1D, 0x21, 0x25, 0x29),
            (1.25, -2.5, 1.0, 0.5, -0.25, 1.0, -1.0, 0.3)):
        struct.pack_into(">f", event, offset, value)
    struct.pack_into(">IHff", event, 0x2D, buttons, buttons & 0xFFFF,
                     0.25, 0.75)
    if len(event) > 0x3B:
        event[0x3B] = 64
    if len(event) >= 0x40:
        struct.pack_into(">f", event, 0x3C, 12.0)
    for offset, value in ((0x40, -32), (0x41, 127), (0x42, -127)):
        if len(event) > offset:
            struct.pack_into("b", event, offset, value)
    return bytes(event)


def _post(frame, port, *, action=14, payload_size=0x54):
    event = bytearray(1 + payload_size)
    event[0] = POST_FRAME
    struct.pack_into(">iBBBH", event, 1, frame, port, 0,
                     2 if port == 0 else 8, action)
    for offset, value in zip((0xA, 0xE, 0x12, 0x16, 0x1A),
                             (1.5, -2.0, 1.0, 12.0, 60.0)):
        struct.pack_into(">f", event, offset, value)
    if len(event) > 0x21:
        event[0x21] = 4
    if len(event) >= 0x2B:
        event[0x26:0x2B] = bytes((1, 2, 3, 4, 5))
    if len(event) > 0x2F:
        event[0x2F] = 1
    if len(event) >= 0x32:
        struct.pack_into(">H", event, 0x30, 7)
    if len(event) > 0x32:
        event[0x32] = 1
    if len(event) > 0x33:
        event[0x33] = 2
    if len(event) > 0x34:
        event[0x34] = 3
    return bytes(event)


def _bookend(frame, finalized):
    event = bytearray(9)
    event[0] = FRAME_BOOKEND
    struct.pack_into(">ii", event, 1, frame, finalized)
    return bytes(event)


def _replay(events, *, game_mode=8, pre_size=0x42, post_size=0x54):
    game_start = _game_start(game_mode=game_mode)
    sizes = ((GAME_START, len(game_start) - 1),
             (PRE_FRAME, pre_size), (POST_FRAME, post_size),
             (GAME_END, 6), (0x3A, 12), (FRAME_BOOKEND, 8))
    table = bytearray((0x35, 1 + 3 * len(sizes)))
    for command, size in sizes:
        table += bytes((command, size >> 8, size & 0xFF))
    raw = bytes(table) + game_start + b"".join(events)
    return RAW_PREFIX + struct.pack(">I", len(raw)) + raw + b"metadata"


def _two_player_frame(frame, *, action=14, post_size=0x54):
    return (_pre(frame, 0), _pre(frame, 1),
            _post(frame, 0, action=action, payload_size=post_size),
            _post(frame, 1, action=action, payload_size=post_size))


class SlippiTimelineTests(unittest.TestCase):
    def test_bookend_rollback_revisions_materialize_as_one_coherent_frame(self):
        events = [*_two_player_frame(FIRST_FRAME),
                  _bookend(FIRST_FRAME, FIRST_FRAME),
                  *_two_player_frame(-122, action=14),
                  _bookend(-122, FIRST_FRAME),
                  *_two_player_frame(-122, action=15),
                  _bookend(-122, -122),
                  bytes((GAME_END, 7, 0, 0, 0, 0, 0))]
        timeline = decode_timeline(_replay(events))
        self.assertEqual([frame.number for frame in timeline.frames],
                         [FIRST_FRAME, -122])
        self.assertEqual(timeline.frames[1].expected[0].action_state, 15)
        self.assertEqual(timeline.duplicate_updates, 4)
        self.assertEqual(timeline.finalized_through, -122)

    def test_game_end_finalizes_trailing_frame_after_bookend_watermark(self):
        events = [*_two_player_frame(FIRST_FRAME),
                  _bookend(FIRST_FRAME, FIRST_FRAME),
                  *_two_player_frame(-122),
                  _bookend(-122, FIRST_FRAME),
                  *_two_player_frame(-121),
                  bytes((GAME_END, 7, 0, 0, 0, 0, 0))]
        timeline = decode_timeline(_replay(events))
        self.assertEqual(timeline.frames[-1].number, -121)
        self.assertEqual(timeline.finalized_through, FIRST_FRAME)

    def test_post_fields_are_available_independently_of_hurtbox_byte(self):
        events = [*_two_player_frame(FIRST_FRAME, post_size=0x33),
                  bytes((GAME_END, 7, 0, 0, 0, 0, 0))]
        timeline = decode_timeline(_replay(events, post_size=0x33))
        post = timeline.frames[0].expected[0]
        self.assertEqual(post.state_flags, (1, 2, 3, 4, 5))
        self.assertEqual((post.ground_or_air, post.last_ground_id,
                          post.jumps_remaining, post.l_cancel_status),
                         (1, 7, 1, 2))
        self.assertIsNone(post.hurtbox_state)

    def test_missing_post_state_at_finalization_is_explicit(self):
        events = [_pre(FIRST_FRAME, 0), _pre(FIRST_FRAME, 1),
                  _post(FIRST_FRAME, 0), _bookend(FIRST_FRAME, FIRST_FRAME)]
        with self.assertRaisesRegex(SlippiFormatError,
                                    "frame -123 is missing port 2 post-state"):
            decode_timeline(_replay(events))

    def test_declared_raw_stream_truncation_is_explicit(self):
        events = [*_two_player_frame(FIRST_FRAME),
                  bytes((GAME_END, 7, 0, 0, 0, 0, 0))]
        replay = _replay(events)
        raw_length = struct.unpack_from(">I", replay, len(RAW_PREFIX))[0]
        replay = replay[:len(RAW_PREFIX) + 4 + raw_length - 1]
        with self.assertRaisesRegex(SlippiFormatError, "raw stream needs"):
            decode_timeline(replay)

    def test_short_declared_pre_payload_is_a_format_error(self):
        # A declared event can be shorter than the fields used to identify it;
        # this must remain a SlippiFormatError rather than leaking IndexError.
        tiny_pre = bytes((PRE_FRAME,)) + bytes(5)
        replay = _replay([tiny_pre], pre_size=5)
        with self.assertRaisesRegex(SlippiFormatError,
                                    "Pre-Frame Update event is missing field"):
            decode_timeline(replay)

    def test_events_after_game_end_are_rejected(self):
        events = [*_two_player_frame(FIRST_FRAME),
                  bytes((GAME_END, 7, 0, 0, 0, 0, 0)),
                  _pre(-122, 0)]
        with self.assertRaisesRegex(SlippiFormatError,
                                    "appears after Game End"):
            decode_timeline(_replay(events))

    def test_game_end_requires_complete_current_frame(self):
        events = [*_two_player_frame(FIRST_FRAME),
                  _pre(-122, 0), _pre(-122, 1),
                  bytes((GAME_END, 7, 0, 0, 0, 0, 0))]
        with self.assertRaisesRegex(SlippiFormatError,
                                    "frame -122 is missing port 1 post-state"):
            decode_timeline(_replay(events))

    def test_each_port_revision_must_start_with_pre_and_alternate(self):
        with self.assertRaisesRegex(SlippiFormatError,
                                    "port 1 has Post-Frame Update before"):
            decode_timeline(_replay([_post(FIRST_FRAME, 0)]))

        with self.assertRaisesRegex(SlippiFormatError,
                                    "port 1 has Pre-Frame Update before"):
            decode_timeline(_replay([_pre(FIRST_FRAME, 0),
                                     _pre(FIRST_FRAME, 0)]))

    @unittest.skipUnless(
        (ROOT / "work/slippi-js-9.1.3/slp/finalizedFrame.slp").exists(),
        "pinned slippi-js fixtures are not checked out")
    def test_official_finalized_fixture(self):
        replay = (ROOT / "work/slippi-js-9.1.3/slp/finalizedFrame.slp").read_bytes()
        timeline = decode_timeline(replay)
        self.assertEqual((len(timeline.frames), timeline.frames[-1].number),
                         (4666, 4542))
        self.assertEqual(timeline.finalized_through, 4542)
        self.assertGreater(timeline.duplicate_updates, 0)

    @unittest.skipUnless(
        (ROOT / "work/slippi-js-9.1.3/slp/rollbackFrameTest.slp").exists(),
        "pinned slippi-js fixtures are not checked out")
    def test_official_rollback_fixture_includes_game_end_frame(self):
        replay = (ROOT / "work/slippi-js-9.1.3/slp/rollbackFrameTest.slp").read_bytes()
        timeline = decode_timeline(replay)
        self.assertEqual((len(timeline.frames), timeline.frames[-1].number),
                         (9277, 9153))
        self.assertEqual(timeline.finalized_through, 9152)
        self.assertGreater(timeline.duplicate_updates, 0)


if __name__ == "__main__":
    unittest.main()
