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

    def test_three_pulse_prefix_is_source_bracketed_before_css_continuation(self):
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text(encoding='utf-8')
        self.assertIn("resultsInputMode==='keyboard-three-prefix'", harness)
        self.assertNotIn('keyboard-three-prefix is a one-match diagnostic mode', harness)
        self.assertIn('const inputEventStart=await page.evaluate(()=>', harness)
        self.assertIn('(report.results_input_events||[]).slice(inputEventStart).filter(row=>', harness)
        self.assertIn('const initialPulseLimit=resultsInputMode===\'keyboard-three-prefix\'?3:48;',
                      harness)
        prefix_loop = harness.index('const initialPulseLimit=')
        capture = harness.index('await captureThreePulsePrefix();', prefix_loop)
        continuation = harness.index('for(let pulse=3;pulse<48&&state.phase!==1;pulse++)', prefix_loop)
        self.assertLess(prefix_loop, capture)
        self.assertLess(capture, continuation)
        self.assertIn('waitForResultsFrame(560', harness)
        self.assertIn('resultsSourceFrameAtEvent<=560', harness)
        self.assertIn('row.hold_ms===160&&row.release_ms===120', harness)
        self.assertIn('source_state_after_tick_560:sample560?.results_state_after_tick', harness)
        self.assertIn('pads_at_source_frame_560:sample560?.pads', harness)
        pulse_sender = harness[harness.index('const sendOrdinaryKeyboardPulse='):
                               harness.index("const initialPulseLimit=", harness.index('const sendOrdinaryKeyboardPulse='))]
        self.assertIn('resumeResultsIfPaused(await diagnostic())', pulse_sender,
                      'Continuation input must use live pause state, not a stale prior sample')
        self.assertIn('waitForResultsFrame(sourceFrameBefore+1', pulse_sender,
                      'Post-checkpoint ordinary input must be dispatched only after source advancement')
        self.assertIn('Exactly three ordinary keyboard intentions must precede the cursor-560 checkpoint',
                      harness)
        self.assertIn('P1/P2-connected, CPU-P3/P4-disconnected profile', harness)

    def test_three_pulse_prefix_can_cover_both_natural_matches(self):
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text(encoding='utf-8')
        self.assertIn("['url','disc','out','lineup','playwright','build-dir','results-input']", harness)
        self.assertIn("if(![1,2].includes(matchCount))throw Error('--matches must be 1 or 2');", harness)
        self.assertIn('for(let pulse=3;pulse<48&&state.phase!==1;pulse++)', harness)
        self.assertIn('natural Results→CSS→second match', harness)

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
        self.assertIn('const expectedPhase=sourceTickThreePulse?1:2;', harness)
        self.assertIn('The first P1 Start edge must be consumed from original Results phase ${expectedPhase}', harness)
        self.assertIn('findResultsStartRunAtOrAfter(sourcePadSummary.p1_start_runs,', harness)
        self.assertIn('pageCheck.confirmation_source_frame=confirmationRun.first_source_frame;', harness)
        self.assertNotIn('const confirmationFrame=report.results_page_transition_checks.find(', harness)
        self.assertNotIn('const confirmationFrame=result.results_page_transition_checks.find(', harness)
        self.assertIn("const sourceTickThreePulse=resultsInputMode==='source-tick-three-pulse';", harness)
        self.assertIn('await queueSourceStartAtExactTick(360,`results-${matchIndex}-source-start-2`)', harness)
        self.assertIn('scheduleResultsP1StartSequence', harness)
        run_match = harness.index('async function runMatch(matchIndex,expected){')
        schedule = harness.index('scheduleResultsP1StartSequence,{events}', run_match)
        match_launch = harness.index('await chooseFinalDestination();', run_match)
        self.assertLess(schedule, match_launch,
                        'Exact Results inputs must be queued before SSS/match source callbacks')
        self.assertNotIn('__meleeWebExactSourceTickScheduler', harness)
        self.assertIn("assert.equal(first,180,", harness)
        self.assertIn("assert.equal(second,360,", harness)
        self.assertIn("assert.equal(confirmation,600,", harness)
        self.assertIn('assert.deepEqual(pulses,[180,360,600]', harness)
        self.assertIn('Both disconnected CPU pages must auto-advance after the tick-360 pulse and before tick-600 confirmation', harness)
        page_gate = harness.index('await waitForCpuPagesBeforeSourceFrame(600,')
        confirmation_queue = harness.index(
            'confirmation=await queueSourceStartAtExactTick(600,', page_gate)
        self.assertLess(page_gate, confirmation_queue,
                        'Both CPU auto-pages must be observed before the final P1 Start is queued')
        self.assertIn("row.status==='queued-awaiting-consumed-trace'", harness)
        self.assertIn("pageCheck.status='pass';", harness)
        runtime = (ROOT / 'src/gameplay_menu_browser.cpp').read_text(encoding='utf-8')
        scheduler = runtime.index('int melee_web_native_menu_results_pad_schedule(')
        source_step = runtime.index('activate_scheduled_results_pad(results->source_frames(),input->raw)')
        pad_override = runtime.index('if(diagnostic_pad_remaining){')
        self.assertLess(source_step, pad_override,
                        'Scheduled events must reach the established PAD sample before Results tick')
        self.assertIn('Missed scheduled Results PAD source tick; refusing late input', runtime)
        self.assertIn('scheduled_results_pad.all_consumed()', runtime)
        self.assertIn('scheduled_results_pad.enqueue({source_frame,port,buttons,duration})', runtime)
        self.assertIn('HSD_PadCopyStatus[port]', runtime)
        self.assertIn('source_consumed_pads', runtime)
        reducer = (ROOT / 'tests/gameplay_results_scene_trace.cpp').read_text(encoding='utf-8')
        self.assertIn('pad_schedule.before_tick(source_frame, event)', reducer)
        self.assertIn('statistics_control.prepare(session.source_frames(), pads)', reducer)
        self.assertIn('const unsigned source_frame = completed_frames - 1;', reducer)
        trace_helpers = (ROOT / 'tests/results_source_pad_trace.mjs').read_text(encoding='utf-8')
        self.assertIn("source_p1_trigger_frames", trace_helpers)
        self.assertIn("source_p1_release_frames", trace_helpers)
        self.assertIn("source_consumed_pad_rows", trace_helpers)
        self.assertGreater(scheduler, runtime.index('int melee_web_native_menu_pad_sample(unsigned'))
        cmake = (ROOT / 'cmake/FighterRuntime.cmake').read_text(encoding='utf-8')
        self.assertIn('_melee_web_native_menu_results_pad_schedule', cmake)
        public_exports = cmake.split('# The public player', 1)[1].split(
            '# Shared typed scene/model tables', 1
        )[0]
        self.assertNotIn('_melee_web_native_menu_results_pad_schedule', public_exports)
        self.assertIn('window.__meleeWebAudioDiagnostics={snapshot};', harness)
        self.assertIn('report.failure.audio_diagnostics=await readAudioDiagnostics();', harness)
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
        validation_start = harness.index("if(resultsInputMode==='keyboard-gated'){",
                                         harness.index('const sourcePadTraceRecord='))
        validation_end = harness.index('}else if(sourceTickMode){', validation_start)
        validation = harness[validation_start:validation_end]
        self.assertIn('findConsumedResultsStartKeyboardAttempt(', validation)
        self.assertIn('pageCheck.post_page_confirmation_attempts=postPageConfirmation.attempts;', validation)
        self.assertIn('assert(postPageConfirmation.accepted,', validation)
        source_pad_summary = validation.index('const sourcePadSummary=sourcePadTraceRecord?.summary;')
        self.assertGreaterEqual(validation.index('findConsumedResultsStartKeyboardAttempt(', source_pad_summary),
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
