"""Exercise malformed transport rejection in the real shared Wasm decoder."""
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


class RetailRecipeRuntimeTests(unittest.TestCase):
    def test_native_decoder_rejects_before_asset_or_match_construction(self):
        target = ROOT / 'build/browser-release/gameplay_retail_trace.js'
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
            (valid_size[:4] + struct.pack('>I', 8) + valid_size[8:], 'input version'),
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
            # Whole-session v7 keeps the v4 envelope and appends a span table.
            # These cases carry a decodable PAD snapshot so the decoder reaches
            # the span table instead of rejecting the transport earlier.
            (struct.pack('>4sIIIHH', b'MWRC', 7, 0, 1, 0x7ff, 0x1c0) +
             setup + PAD_SNAPSHOT + bytes(44), 'Whole-session input size'),
            (struct.pack('>4sIIIHH', b'MWRC', 7, 0, 1, 0x7ff, 0x1c0) +
             setup + PAD_SNAPSHOT + bytes(44) + struct.pack('>H', 0), 'span count'),
            (struct.pack('>4sIIIHH', b'MWRC', 7, 0, 1, 0x7ff, 0x1c0) +
             setup + PAD_SNAPSHOT + bytes(44) + struct.pack('>H', 1) +
             struct.pack('>BBHII', 1, 0, 0, 0, 0) + b'\0', 'span table'),
            (struct.pack('>4sIIIHH', b'MWRC', 7, 0, 2, 0x7ff, 0x1c0) +
             setup + PAD_SNAPSHOT + bytes(88) + struct.pack('>H', 1) +
             struct.pack('>BBHII', 6, 0, 0, 0, 1), 'admitted scene'),
            (struct.pack('>4sIIIHH', b'MWRC', 7, 0, 3, 0x7ff, 0x1c0) +
             setup + PAD_SNAPSHOT + bytes(132) + struct.pack('>H', 2) +
             struct.pack('>BBHII', 1, 0, 0, 0, 0) + struct.pack('>BBHII', 2, 0, 0, 2, 2),
             'ordered and contiguous'),
            (struct.pack('>4sIIIHH', b'MWRC', 7, 0, 3, 0x7ff, 0x1c0) +
             setup + PAD_SNAPSHOT + bytes(132) + struct.pack('>H', 1) +
             struct.pack('>BBHII', 1, 0, 0, 0, 1), 'cover every input frame'),
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
        target = ROOT / 'build/browser-release/gameplay_retail_trace.js'
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
        # The decoder admits the whole-session transport and its span table; the
        # run then fails only because the owned asset directories are absent.
        self.assertNotIn('Whole-session', result.stderr)
        self.assertIn('absent-menu', result.stderr)
