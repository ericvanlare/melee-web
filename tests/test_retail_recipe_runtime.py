"""Exercise malformed transport rejection in the real shared Wasm decoder."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime

# Canonical source defaults followed by twelve independent histories: the same
# decodable PAD snapshot tests/test_pad_state.py builds.
PAD_SNAPSHOT = (bytes.fromhex('0000002d00000008001e0000000000007f0000ff0000ff007fffff000000')
                + bytes(12 * 66))
assert len(PAD_SNAPSHOT) == 822


def _native_target() -> Path:
    for candidate in (ROOT / 'build/browser-release/gameplay_retail_trace.js',
                      ROOT / 'build/browser/gameplay_retail_trace.js'):
        if candidate.is_file():
            return candidate
    return ROOT / 'build/browser-release/gameplay_retail_trace.js'


def _whole_session_v8_fixture() -> bytes:
    """Build a parser-only v8 input with four ordinary level-9 CPUs."""
    setup = bytearray(0x138)
    for player in range(6):
        base = 0x60 + player * 0x24
        if player >= 4:
            setup[base + 1] = 3  # Gm_PKind_NA
            continue
        setup[base:base + 5] = bytes((8, 1, 4, player, 0))
        setup[base + 14] = 4  # ordinary VS CPU kind
        setup[base + 15] = 9
        for offset in (0x18, 0x1C, 0x20):
            struct.pack_into('>f', setup, base + offset, 1.0)

    characters, stages = 0x07FF, 0x01C0
    context = (struct.pack('>HHI', 2, 0, 0x574E) +
               bytes(0x18) + struct.pack('>HH', characters, stages) +
               bytes(0x55E8 - 4) + bytes(0x148) + bytes(6))
    frame_count = 32
    spans = struct.pack('>H', 32) + b''.join(
        struct.pack('>BBHII', 1 + index % 5, 0, 0, index, index)
        for index in range(frame_count))
    return (struct.pack('>4sIIIHH', b'MWRC', 8, 0x12345678, frame_count,
                        characters, stages) + context + bytes(setup) +
            PAD_SNAPSHOT + bytes(44 * frame_count) + spans)


class RetailRecipeRuntimeTests(unittest.TestCase):
    def test_native_decoder_rejects_before_asset_or_match_construction(self):
        target = _native_target()
        if not target.is_file():
            self.skipTest('Build gameplay_retail_trace to test the shared decoder')
        setup = bytearray(0x138)
        setup[0x62] = setup[0x86] = 4
        for player in range(2, 6): setup[0x61 + player * 0x24] = 3
        prefix = struct.pack('>4sIII', b'MWRC', 1, 0, 1)
        valid_size = prefix + setup + bytes(44)
        callback = bytearray(valid_size); callback[16 + 0x38] = 1
        ratio = bytearray(valid_size); ratio[16 + 0x2c:16 + 0x30] = bytes.fromhex('7f800000')
        examples = [
            (b'', 'size is outside'),
            (b'NOPE' + valid_size[4:], 'input format'),
            (valid_size[:4] + struct.pack('>I', 9) + valid_size[8:], 'input version'),
            (valid_size[:-1], 'frame count'),
            (valid_size + b'\0', 'frame count'),
            (callback, 'callback/data pointer'),
            (ratio, 'nonfinite rules'),
            (struct.pack('>4sIII', b'MWRC', 2, 0, 1) + setup + bytes(822 + 44), 'PAD'),
            (struct.pack('>4sIIIHH', b'MWRC', 4, 0, 1, 0x7ff, 0x1c0) +
             setup + bytes(822 + 44), 'PAD'),
            (struct.pack('>4sIIIHH', b'MWRC', 4, 0, 1, 0x7ff, 0x1c0) +
             setup + bytes(822 + 43), 'frame count'),
            (struct.pack('>4sIIIHH', b'MWRC', 5, 0, 2, 0x7ff, 0x1c0) +
             struct.pack('>QQQQII',8100000,8108100,3388044,6732987,2,0) +
             setup + bytes(822+88), 'PAD'),
            (struct.pack('>4sIIIHH', b'MWRC', 5, 0, 2, 0x7ff, 0x1c0) +
             struct.pack('>QQQQII',8100000,8108100,3388044,6732987,3,0) +
             setup + bytes(822+88), 'Unsupported captured PAD/VI'),
            (struct.pack('>4sIIIHHIIQB', b'MWRC', 6, 0, 2, 0x7ff, 0x1c0, 1, 0, 0, 2) +
             setup + bytes(822+88), 'PAD'),
            (struct.pack('>4sIIIHHIIQB', b'MWRC', 6, 0, 2, 0x7ff, 0x1c0, 1, 0, 1, 2) +
             setup + bytes(822+88), 'Invalid recorded input-queue event'),
            (struct.pack('>4sIIIHHIIQB', b'MWRC', 6, 0, 2, 0x7ff, 0x1c0, 1, 0, 0, 1) +
             setup + bytes(822+88), 'does not cover all samples'),
            # Whole-session v7 is the provisional envelope and must be rejected
            # before any declared PAD/span payload can become runtime input.
            (struct.pack('>4sIIIHH', b'MWRC', 7, 0, 1, 0x7ff, 0x1c0) +
             setup + PAD_SNAPSHOT + bytes(44), 'MWRC v7 is unsupported'),
        ]
        with tempfile.TemporaryDirectory(prefix='melee-recipe-') as directory:
            root = Path(directory)
            for data, message in examples:
                path = root / 'invalid.mwrc'; path.write_bytes(data)
                result = subprocess.run([str(node_runtime()), str(target),
                    str(root / 'absent-menu'), str(root / 'absent-game'), str(path)],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn(message, result.stderr)
                self.assertNotIn('directory_iterator', result.stderr)

    def test_whole_session_transport_is_admitted_before_asset_loading(self):
        target = _native_target()
        if not target.is_file():
            self.skipTest('Build gameplay_retail_trace to test the shared decoder')
        setup = bytearray(0x138)
        setup[0x62] = setup[0x86] = 4
        for player in range(2, 6): setup[0x61 + player * 0x24] = 3
        payload = (struct.pack('>4sIIIHH', b'MWRC', 7, 0, 3, 0x7ff, 0x1c0) + setup +
                   PAD_SNAPSHOT + bytes(132) + struct.pack('>H', 2) +
                   struct.pack('>BBHII', 1, 0, 0, 0, 0) +
                   struct.pack('>BBHII', 2, 0, 0, 1, 2))
        with tempfile.TemporaryDirectory(prefix='melee-recipe-') as directory:
            root = Path(directory)
            path = root / 'whole-session.mwrc'; path.write_bytes(payload)
            result = subprocess.run([str(node_runtime()), str(target),
                str(root / 'absent-menu'), str(root / 'absent-game'), str(path)],
                capture_output=True, text=True, timeout=30)
        # The old v7 transport is rejected before it can reach asset loading.
        self.assertIn('v7 is unsupported', result.stderr)

    def test_native_v8_decoder_preserves_four_cpu9_rows_and_max_spans(self):
        target = _native_target()
        if not target.is_file():
            self.skipTest('Build gameplay_retail_trace to test the shared decoder')
        with tempfile.TemporaryDirectory(prefix='melee-recipe-v8-') as directory:
            path = Path(directory) / 'whole-session.mwrc'
            path.write_bytes(_whole_session_v8_fixture())
            result = subprocess.run([str(node_runtime()), str(target),
                str(Path(directory) / 'absent-menu'),
                str(Path(directory) / 'absent-game'), str(path), '--decode-only'],
                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        decoded = json.loads(result.stdout)
        self.assertEqual(decoded['schema'], 'melee-web-retail-replay-decode')
        self.assertEqual(decoded['version'], 8)
        self.assertEqual(decoded['frame_count'], 32)
        self.assertEqual(decoded['span_count'], 32)
        self.assertTrue(decoded['initial_css_context'])
        self.assertEqual(len(decoded['players']), 4)
        self.assertTrue(all(player['slot_type'] == 1 and
                            player['cpu_kind'] == 4 and
                            player['cpu_level'] == 9
                            for player in decoded['players']))
