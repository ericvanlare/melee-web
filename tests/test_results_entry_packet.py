"""Entry-only observer regression; no gameplay, browser timing or replay claim."""
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


class ResultsEntryPacketTests(unittest.TestCase):
    def test_wasm_copy_retains_pre_teardown_pad_and_full_typed_payload(self):
        compiler = ROOT / '.deps/emsdk/upstream/emscripten/em++'
        emcc = compiler.with_name('emcc')
        source = ROOT / 'build/gameplay-source/src'
        if not compiler.is_file() or not (source / 'melee/gm/types.h').is_file():
            self.skipTest('Pinned Emscripten and prepared gameplay headers required')
        includes = ['-I' + str(p) for p in (ROOT / 'src', source, ROOT / '.deps/aurora/include')]
        common = [*includes, '-DTARGET_PC', '-DAURORA', '-O1']
        with tempfile.TemporaryDirectory(prefix='results-entry-packet-') as directory:
            out = Path(directory)
            commands = [
                [emcc, *common, '-include', ROOT / 'src/gameplay_compat.h', '-c',
                 ROOT / 'src/gameplay_pad_state.c', '-o', out / 'pad.o'],
                [compiler, *common, '-std=c++20', ROOT / 'tests/gameplay_results_entry_packet_test.cpp',
                 out / 'pad.o', '-sENVIRONMENT=node', '-sEXIT_RUNTIME=1', '-sASSERTIONS=2',
                 '-o', out / 'packet.js'],
            ]
            for command in commands:
                result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(node_runtime()), str(out / 'packet.js')],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        first, second = map(json.loads, result.stdout.splitlines())
        self.assertEqual(first['abi'], {'target': 'wasm32', 'byte_order': 'little-endian', 'pointer_bytes': 4})
        self.assertEqual(first['entry_seed'], 0xfedcba98)
        self.assertEqual(first['summary']['players'][2]['ckind'], 19)
        self.assertEqual(first['summary']['players'][2]['ftkind'], 7)
        self.assertEqual(first['summary']['players'][2]['stocks'], -1)
        pad = bytes.fromhex(first['pad']['hex'])
        self.assertEqual(len(pad), 822)
        self.assertEqual(struct.unpack_from('>I', pad)[0], 45)  # not restored 99
        for bank in range(3):
            for port in range(4):
                offset = 30 + (bank * 4 + port) * 66
                self.assertEqual(struct.unpack_from('>I', pad, offset)[0], 0x1000 | (bank * 4 + port))
                self.assertEqual(pad[offset + 24], 219)  # signed -37
                self.assertEqual(pad[offset + 32:offset + 36].hex(), 'bf000000')
        terminal = bytes.fromhex(first['terminal_hex'])
        results = bytes.fromhex(first['results_info_hex'])
        self.assertEqual(len(terminal), first['sizeof']['MatchExitInfo'])
        self.assertEqual(len(results), first['sizeof']['ResultsMatchInfo'])
        self.assertEqual(struct.unpack_from('<iIi', terminal), (-9, 0x12345678, 7))
        for name, data in [('MatchExitInfo', terminal), ('ResultsMatchInfo', results)]:
            base = first['offsetof'][name + '.match_end']
            self.assertEqual(struct.unpack_from('<I', data, base + 8)[0], 11725)
            self.assertEqual(data[base + first['sizeof']['MatchEnd'] - 1], 0xa7)
        self.assertEqual((second['match_index'], second['entry_seed']), (2, 123))
        self.assertEqual(bytes.fromhex(second['pad']['hex']), bytes(822))

    def test_capture_site_uses_existing_buffer_before_deferred_assets_return(self):
        source = (ROOT / 'src/gameplay_menu_browser.cpp').read_text()
        start = source.index('if(match){\n  terminal_match_observation=')
        body = source[start:source.index('results_input.reset();', start)]
        order = ['melee_web_pad_state_capture(final_input)', 'match->close()',
                 'melee_web_menu_host_results_begin(', 'results_seed=seed;',
                 'results_entry_packet.capture(completed_matches,terminal,results_info,results_seed,final_input)',
                 'request_assets(AssetDestination::Results)']
        positions = [body.index(token) for token in order]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(body.count('melee_web_pad_state_capture('), 1)
        self.assertEqual(source.count('results_entry_packet.capture('), 1)
        self.assertIn('EMSCRIPTEN_KEEPALIVE const char* melee_web_native_menu_results_entry_packet()', source)
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text()
        self.assertIn('await retainResultsEntry(`match-${matchIndex}-results-entry`)', harness)
        self.assertIn("await retainResultsEntry('failure-latest-entry')", harness)
        self.assertIn('await installResultsInputObserver()', harness)
        self.assertIn('resultsSourceFrameAtEvent', harness)
        self.assertIn('results_input_events=await page.evaluate', harness)

    def test_read_only_harness_observation_and_build_binding(self):
        result = subprocess.run([str(node_runtime()), str(ROOT / 'tests/results_entry_packet_test.mjs')],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
