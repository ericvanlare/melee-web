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
        self.assertIn("const continuationScope=matchCount===1?'natural Results→CSS only':",
                      harness)
        self.assertIn("'natural Results→CSS→second match';", harness)

    def test_read_only_harness_observation_and_build_binding(self):
        result = subprocess.run([str(node_runtime()), str(ROOT / 'tests/results_entry_packet_test.mjs')],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_results_pad_trace_is_bounded_and_development_only(self):
        source = (ROOT / 'src/gameplay_menu_browser.cpp').read_text(encoding='utf-8')
        self.assertIn('kResultsPadTraceCapacity=8192', source)
        self.assertIn('results->source_frames(),sample', source)
        self.assertIn('extern ResultsData lbl_8046DBE8', source)
        self.assertIn('retain_results_state_after_tick', source)
        self.assertIn('results_state_after_tick', source)
        self.assertIn('melee_web_native_menu_results_pad_trace()', source)
        cmake = (ROOT / 'cmake/FighterRuntime.cmake').read_text(encoding='utf-8')
        development = cmake.split('add_executable(gameplay_menu_browser', 1)[1].split(
            '# The public player', 1
        )[0]
        public = cmake.split('# The public player', 1)[1].split(
            '# Shared typed scene/model tables', 1
        )[0]
        self.assertIn('_melee_web_native_menu_results_pad_trace', development)
        self.assertNotIn('_melee_web_native_menu_results_pad_trace', public)
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text(encoding='utf-8')
        self.assertIn('results_page_transitions', harness)
        self.assertIn('const sourcePadTraceRecord=await retainResultsSourcePadTrace(', harness)
        self.assertIn('const sourcePadTrace=sourcePadTraceRecord?.trace;', harness)
        self.assertIn('const sourcePadSummary=sourcePadTraceRecord?.summary;', harness)
        self.assertIn('results_source_pad_trace_helper_sha256', harness)
        self.assertIn('browser_driver_helper_sha256', harness)
        self.assertIn('before P1 confirmation', harness)
        self.assertIn('waitForResultsInternalPhase(180,2', harness)
        self.assertIn('source-confirm-after-auto-page', harness)
        self.assertIn('first P1 Start edge must be consumed from original Results phase 2', harness)
        self.assertIn("'keyboard-gated'", harness)
        gated_start = harness.index("}else if(resultsInputMode==='keyboard-gated'){")
        gated_end = harness.index("\n  }else{\n    await writeProgress(`match-${matchIndex}-natural-results`)",
                                  gated_start)
        gated = harness[gated_start:gated_end]
        first_enter = gated.index("driver.pressChord(['Enter'],{holdMs:160,releaseMs:120})")
        self.assertIn('const initialEnterTargets=[198,296,394,509]', gated)
        self.assertLess(gated.index('const initialEnterTargets=[198,296,394,509]'), first_enter)
        phase_gate = gated.index('waitForResultsInternalPhase(180,2,')
        self.assertLess(phase_gate, gated.index('for(const targetFrame of initialEnterTargets)'))
        self.assertLess(phase_gate, first_enter)
        self.assertLess(first_enter, gated.index('summarizeResultsPadTrace(trace).p1_start_runs.length>0'))
        self.assertIn('if(initialStartObserved)break;', gated)
        self.assertIn('returned to CSS before the disconnected CPU auto-page gate', gated)
        self.assertIn('returned to CSS before both disconnected CPU pages auto-advanced', gated)
        self.assertIn('cannot pass without the observed disconnected CPU auto-page gate', harness)
        self.assertIn('cannot pass without a retained post-page keyboard confirmation', harness)
        self.assertIn('lastCpuPageTransition+1', gated)
        self.assertIn('confirmationLowerBound>lastCpuPageTransition', gated)
        self.assertIn('results_keyboard_page_gate', gated)
        self.assertIn('retainResultsInputEvents()', gated)
        self.assertIn("pageCheck.status='post-page-keyboard-dispatched';", gated)
        self.assertIn("row.status==='post-page-keyboard-dispatched'", harness)
        source_pad_summary = harness.index('const sourcePadSummary=sourcePadTraceRecord?.summary;')
        self.assertGreaterEqual(harness.index('sourcePadSummary.p1_start_runs.find(', source_pad_summary),
                                source_pad_summary)
        self.assertIn("pageCheck.status='pass-input-dispatched-after-pages';", harness)
        trace_helper = (ROOT / 'tests/results_source_pad_trace.mjs').read_text(encoding='utf-8')
        self.assertIn('assertResultsCpuPagesAfterInitialP1Keyboard', trace_helper)
        self.assertIn('first P1 Start edge must be consumed from original Results phase 2', trace_helper)
        input_helper = (ROOT / 'tests/results_source_pad_input.mjs').read_text(encoding='utf-8')
        self.assertLess(input_helper.index('Module._melee_web_native_menu_running()'),
                        input_helper.index("status.startsWith('Paused after a timing disruption')"))
        self.assertLess(input_helper.index("status.startsWith('Paused after a timing disruption')"),
                        input_helper.index('Module._melee_web_native_menu_pause(0)'))
        self.assertLess(input_helper.index('Module._melee_web_native_menu_pause(0)'),
                        input_helper.index('Module._melee_web_native_menu_pad_sample'))

    def test_results_trace_resets_before_scoped_asset_early_return(self):
        source = (ROOT / 'src/gameplay_menu_browser.cpp').read_text(encoding='utf-8')
        start = source.index('if(match){\n  terminal_match_observation=')
        end = source.index('results=std::make_unique<melee_web::GameplayResultsSession>', start)
        body = source[start:end]
        trace_reset = body.index('results_pad_trace_count=0;results_pad_trace_attempts=0;')
        camera_reset = body.index('results_camera_entry_snapshot={};')
        scoped_return = body.index('if(scoped_assets){pending=false;request_assets(AssetDestination::Results);return;}')
        self.assertLess(trace_reset, scoped_return)
        self.assertLess(camera_reset, scoped_return)
        handoff = source[source.index('bool finish_asset_handoff()'):]
        constructor = handoff.index('results=std::make_unique<melee_web::GameplayResultsSession>')
        snapshot = handoff.index('results_camera_entry_snapshot=results->camera_entry_snapshot();')
        release_input = handoff.index('results_input.reset();', constructor)
        self.assertLess(constructor, snapshot)
        self.assertLess(snapshot, release_input)

    def test_camera_entry_observer_brackets_source_onenter_without_changing_guards(self):
        context = (ROOT / 'src/gameplay_results_context.c').read_text(encoding='utf-8')
        begin = context.index('MeleeWebResultsContext* melee_web_results_context_begin(')
        end = context.index('\nint melee_web_results_context_tick(', begin)
        body = context[begin:end]
        boundaries = [
            'context->camera_pool_before_onenter = cm_804D645C;',
            'context->owner_camera_pool_before_onenter = owner_camera_pool;',
            'gm_Scene_Results_OnEnter(&context->match);',
            'context->camera_pool_after_onenter = cm_804D645C;',
            'melee_web_collision_adopt_dummy(error,error_size)',
            'context->camera_pool_after_collision_adoption = cm_804D645C;',
            'context->camera_pool = cm_804D645C;',
            'owner_camera_pool = context->camera_pool;',
        ]
        positions = [body.index(boundary) for boundary in boundaries]
        self.assertEqual(positions, sorted(positions))
        self.assertIn('camera_pool_owned(context, "tick entry"', context)
        self.assertIn('camera_pool_owned(context, "source draw"', context)
        self.assertIn('camera_pool_owned(context, "scene exit entry"', context)
        self.assertIn('camera_pool_owned(context, "close entry"', context)

        browser = (ROOT / 'src/gameplay_menu_browser.cpp').read_text(encoding='utf-8')
        reset = browser.index('results_camera_entry_snapshot={};')
        construct = browser.index('results=std::make_unique<melee_web::GameplayResultsSession>')
        capture = browser.index('results_camera_entry_snapshot=results->camera_entry_snapshot();')
        self.assertLess(reset, construct)
        self.assertLess(construct, capture)
        trace = browser.index('melee_web_native_menu_results_pad_trace()')
        serialize = browser.index('append_camera_entry_json(json);', trace)
        self.assertGreater(serialize, trace)
        self.assertIn('source_pool_before_onenter', browser)
        self.assertIn('source_pool_after_onenter', browser)
        self.assertIn('source_pool_after_collision_adoption', browser)


if __name__ == '__main__':
    unittest.main()
