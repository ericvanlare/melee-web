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


class RetailRecipeRuntimeTests(unittest.TestCase):
    def test_native_decoder_rejects_before_asset_or_match_construction(self):
        target = ROOT / 'build/browser/gameplay_retail_trace.js'
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
            (valid_size[:4] + struct.pack('>I', 4) + valid_size[8:], 'input version'),
            (valid_size[:-1], 'frame count'),
            (valid_size + b'\0', 'frame count'),
            (callback, 'callback/data pointer'),
            (ratio, 'nonfinite rules'),
            (struct.pack('>4sIII', b'MWRC', 2, 0, 1) + setup + bytes(822 + 44), 'PAD'),
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
