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
        self.assertIn('const continuationScope=resultsObserveAfterConfirmation?', harness)
        self.assertIn("matchCount===1?'natural Results→CSS only':", harness)
        self.assertIn('natural Results→CSS→${matchCount-1} subsequent match', harness)

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
        self.assertIn('inputServiceStatusAtEvent:inputStatus', harness)
        self.assertIn("inputServiceStatusScope:'read-only snapshot at DOM dispatch", harness)
        self.assertIn('target:{tagName:event.target?.tagName??null', harness)
        self.assertIn('activeElement:{tagName:document.activeElement?.tagName??null', harness)
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
        self.assertIn("['url','disc','out','lineup','playwright','build-dir','results-input','cpu-levels']", harness)
        self.assertIn("values['cpu-levels']===undefined?[9,9,9,9]", harness)
        self.assertIn("if(![1,2,3,4].includes(matchCount))throw Error('--matches must be 1, 2, 3 or 4');", harness)
        self.assertIn('for(let pulse=3;pulse<48&&state.phase!==1;pulse++)', harness)
        self.assertIn('for(let matchIndex=2;matchIndex<=matchCount;matchIndex++)', harness)
        self.assertIn('assertResultsCpuPagesAfterP1KeyboardPrefix(trace,[192,363]', harness)
        self.assertIn('source ticks 192/363; wait for disconnected CPU auto-pages, then send the third P1 confirmation', harness)

    def test_historical_two_pulse_prefix_waits_for_cpu_pages_before_third_enter(self):
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text(encoding='utf-8')
        self.assertIn("const keyboardPrefixGatedMode=resultsInputMode==='keyboard-gated-two-prefix'||", harness)
        self.assertIn('keyboardSourceTickConfirmMode;', harness)
        self.assertIn('const initialEnterTargets=keyboardPrefixGatedMode?[192,363]:[198,296,394,509];', harness)
        self.assertIn("initialKeydowns.map(row=>row.resultsSourceFrameAtEvent),[192,363]", harness)
        self.assertIn("initialKeyups.map(row=>row.resultsSourceFrameAtEvent),[202,373]", harness)
        gate = harness.index('gate=await waitForCpuPagesBeforeKeyboard(')
        confirmation = harness.index('const lastCpuPageTransition=Math.max(', gate)
        third_enter = harness.index("await driver.pressChord(['Enter'],{holdMs:160,releaseMs:120});", confirmation)
        self.assertLess(gate, confirmation)
        self.assertLess(confirmation, third_enter,
                        'The historical third Enter must not dispatch before source-observed CPU page transitions')

    def test_optional_cpu_level_profile_is_source_confirmed_per_door(self):
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text(encoding='utf-8')
        self.assertIn("--cpu-levels must contain exactly four comma-separated integer levels from 1 through 9", harness)
        self.assertIn("players[door].cpu_level,cpuLevels[door]", harness)
        self.assertIn("const direction=targetLevel>currentLevel?80:-80", harness)
        self.assertIn("cpu:cpuLevels[door]", harness)

    def test_historical_prefix_has_exact_source_tick_keyboard_confirmation_mode(self):
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text(encoding='utf-8')
        self.assertIn("const keyboardSourceTickConfirmMode=resultsInputMode==='keyboard-gated-two-prefix-source-tick';",
                      harness)
        self.assertIn("(keyboardSourceTickConfirmMode?560:600)", harness)
        self.assertIn('waitForCpuPagesBeforeSourceFrame(resultsConfirmFrame,', harness)
        self.assertIn('Both disconnected CPU pages must advance before source tick ${resultsConfirmFrame}',
                      harness)
        self.assertIn('The source-tick keyboard pause boundary ${resultsConfirmFrame} was missed',
                      harness)
        self.assertIn('The trusted P1 Enter keydowns must occur at source ticks 192, 363 and the exact confirmation tick',
                      harness)
        self.assertIn('The source-consumed P1 confirmation must begin at the exact requested source tick',
                      harness)
        self.assertIn('pageCheck.confirmation_source_frame=firstThreeStarts[2]?.first_source_frame??null;',
                      harness)
        self.assertLess(
            harness.index('pageCheck.confirmation_source_frame=firstThreeStarts[2]?.first_source_frame??null;'),
            harness.index('assert.equal(pageCheck.confirmation_source_frame,resultsConfirmFrame,'))
        self.assertIn('Each ten-source-tick held P1 Enter must retain its distinct keyup edge',
                      harness)
        self.assertIn('P1/P2-connected split keyboard, CPU ports P3/P4 disconnected; trusted Enter down/up edges at source cursors 192/202 and 363/373; assert automatic CPU page transitions before source tick ${resultsConfirmFrame}',
                      harness)
        exact_gate_start = harness.index('let gate;', harness.index('const initialEnterTargets='))
        exact_gate = harness.index('await waitForResultsPauseAtSourceFrame(resultsConfirmFrame,',
                                   exact_gate_start)
        exact_trace = harness.index('const exactPrefixTrace=await readResultsSourcePadTrace();',
                                    exact_gate)
        exact_pause = harness.index('state=await waitForResultsPauseAtSourceFrame(resultsConfirmFrame,',
                                    exact_trace)
        self.assertIn('assertResultsCpuPagesAfterP1KeyboardPrefix(exactPrefixTrace,[192,363]', harness)
        exact_input = harness.index("await page.keyboard.down('Enter');", exact_pause)
        exact_resume = harness.index('Module._melee_web_native_menu_pause(0)', exact_input)
        prefix_input = harness.index("await page.keyboard.down('Enter');", harness.index('const initialEnterTargets='))
        prefix_resume = harness.index('Module._melee_web_native_menu_pause(0)', prefix_input)
        prefix_release = harness.index('waitForResultsPauseAtSourceFrame(targetFrame+10', prefix_input)
        release_pause = harness.index('state=await waitForResultsPauseAtSourceFrame(resultsConfirmFrame+10,',
                                      exact_input)
        exact_release = harness.index("await page.keyboard.up('Enter');", release_pause)
        self.assertLess(exact_gate, exact_trace)
        self.assertLess(exact_trace, exact_pause)
        self.assertLess(exact_pause, exact_input)
        self.assertLess(exact_input, release_pause)
        self.assertLess(exact_input, exact_resume)
        self.assertLess(exact_resume, release_pause)
        self.assertLess(release_pause, exact_release)
        self.assertLess(prefix_input, prefix_resume)
        self.assertLess(prefix_resume, prefix_release,
                        'The source must resume after keydown to consume the exact ten-sample hold')
        self.assertIn('resultsSourceFrameAtEvent===resultsConfirmFrame', harness)
        self.assertIn('resultsSourceFrameAtEvent===resultsConfirmFrame+10', harness)
        run_match = harness.index('async function runMatch(matchIndex,expected){')
        pause_schedule = harness.index('scheduleResultsSourceFramePauses,{frames}', run_match)
        sss = harness.index('await chooseFinalDestination();', run_match)
        self.assertLess(pause_schedule, sss,
                        'Exact source pauses must be armed before the original SSS/match route')

    def test_exact_results_confirmation_can_be_observed_without_a_retry(self):
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text(encoding='utf-8')
        self.assertIn("'results-observe-after-confirmation':{type:'boolean'}", harness)
        self.assertIn("!keyboardSourceTickConfirmMode||Number(values.matches||2)!==1", harness)
        self.assertIn('resultsConfirmFrame+150', harness)
        self.assertIn('const frames=[192,202,363,373,resultsConfirmFrame,resultsConfirmFrame+10];',
                      harness,
                      'Do not leave a future Results pause armed if the natural third pulse exits first')
        self.assertIn('for(;!resultsObserveAfterConfirmation&&pulses<48&&state.phase!==1;pulses++)',
                      harness,
                      'The bounded observation must not dispatch another Enter after confirmation')
        observation = harness.index('if(resultsObserveAfterConfirmation&&state.phase!==1)')
        no_retry_assertion = harness.index('result.results_post_confirmation_observation=', observation)
        css_assertion = harness.index(
            "assert.equal(state.phase,1,`Natural Results ${matchIndex} did not return to original CSS`)",
            observation)
        self.assertLess(no_retry_assertion, css_assertion,
                        'The partial observation must retain Results without claiming CSS return')
        self.assertIn('post-confirmation-observation-before-any-retry', harness)
        self.assertIn("'results-observation-pass'", harness)

    def test_exact_results_pause_scheduler_is_a_dev_only_pre_step_control(self):
        runtime = (ROOT / 'src/gameplay_menu_browser.cpp').read_text(encoding='utf-8')
        pause = runtime.index('int melee_web_native_menu_results_pause_schedule(')
        loop = runtime.index('for(unsigned step=0;step<elapsed.steps;step++){')
        boundary = runtime.index('scheduled_results_pauses.before_tick(results_source_frame)', loop)
        source_step = runtime.index('source_frames.before_step(present_source);', loop)
        self.assertLess(loop, boundary)
        self.assertLess(boundary, source_step,
                        'The pause must stop before the declared source sample is consumed')
        self.assertIn('ResultsSourceFramePauseSchedule::Boundary::due', runtime)
        self.assertIn('running=false;menu_clock.reset();', runtime[boundary:source_step])
        self.assertIn('scheduled_results_pauses.all_consumed()', runtime)
        self.assertIn('scheduled_results_pauses.clear()', runtime)
        cmake = (ROOT / 'cmake/FighterRuntime.cmake').read_text(encoding='utf-8')
        development = cmake.split('add_executable(gameplay_menu_browser', 1)[1].split(
            '# The public player', 1
        )[0]
        public = cmake.split('# The public player', 1)[1].split(
            '# Shared typed scene/model tables', 1
        )[0]
        self.assertIn('_melee_web_native_menu_results_pause_schedule', development)
        self.assertNotIn('_melee_web_native_menu_results_pause_schedule', public)
        self.assertIn('_melee_web_native_menu_results_pause_schedule',
                      (ROOT / 'scripts/build.py').read_text(encoding='utf-8'))

    def test_cpu_page_waiter_refreshes_at_confirmation_boundary(self):
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text(encoding='utf-8')
        start = harness.index('const waitForCpuPagesBeforeSourceFrame=')
        end = harness.index('// Source-tick-three-pulse waits inside the page', start)
        gate = harness[start:end]
        self.assertIn('diagnosticFrame-lastTraceFrame>=12||diagnosticFrame>=targetFrame-1', gate)
        self.assertLess(gate.index('const trace=await readResultsSourcePadTrace();'),
                        gate.index('if(diagnosticFrame>=targetFrame)'))

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
        self.assertIn("(keyboardSourceTickConfirmMode?560:600)", harness)
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
        self.assertIn("assert.equal(confirmation,resultsConfirmFrame,", harness)
        self.assertIn('assert.deepEqual(pulses,[180,360,resultsConfirmFrame]', harness)
        self.assertIn('before tick-${resultsConfirmFrame} confirmation', harness)
        page_gate = harness.index('await waitForCpuPagesBeforeSourceFrame(resultsConfirmFrame,')
        confirmation_queue = harness.index(
            'confirmation=await queueSourceStartAtExactTick(resultsConfirmFrame,', page_gate)
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
        self.assertIn("'keyboard-gated-p1-enter'", harness)
        self.assertIn("'keyboard-gated-two-prefix'", harness)
        self.assertIn('const keyboardPortErrors=[[0],[0],[-1],[-1]];', harness)
        self.assertIn('controllerProfile.keyboard_active_mask&~3,0', harness)
        self.assertIn('row.inputServiceStatusAtEvent?.keyboard_active_mask===3', harness)
        self.assertIn('historical_results_port_status', harness)
        self.assertIn('expectedPortErrors:keyboardPortErrors', harness)
        self.assertIn('expectedDisconnectedCpuSlots:keyboardAutoPageSlots', harness)
        self.assertIn("page.on('crash',error=>report.page_crashes.push", harness)
        self.assertIn("browserCdp.on('Target.targetCrashed'", harness)
        gated_start = harness.index('}else if(keyboardGatedMode){')
        gated_end = harness.index("\n  }else{\n    await writeProgress(`match-${matchIndex}-natural-results`)",
                                  gated_start)
        gated = harness[gated_start:gated_end]
        first_enter = gated.index("driver.pressChord(['Enter'],{holdMs:160,releaseMs:120})")
        self.assertIn('const initialEnterTargets=keyboardPrefixGatedMode?[192,363]:[198,296,394,509];', gated)
        self.assertLess(gated.index('const initialEnterTargets=keyboardPrefixGatedMode?'), first_enter)
        phase_gate = gated.index('waitForResultsInternalPhase(180,2,')
        self.assertLess(phase_gate, gated.index('for(const targetFrame of initialEnterTargets)'))
        self.assertLess(phase_gate, first_enter)
        self.assertLess(first_enter, gated.index('summarizeResultsPadTrace(trace).p1_start_runs.length>0'))
        self.assertIn('if(initialStartObserved&&!keyboardPrefixGatedMode)break;', gated)
        self.assertIn('returned to CSS before the disconnected CPU auto-page gate', gated)
        self.assertIn('returned to CSS before all expected disconnected CPU pages auto-advanced', gated)
        self.assertIn('cannot pass without the observed disconnected CPU auto-page gate', harness)
        self.assertIn('cannot pass without a retained post-page keyboard confirmation', harness)
        self.assertIn('lastCpuPageTransition+1', gated)
        self.assertIn('confirmationLowerBound>lastCpuPageTransition', gated)
        self.assertIn('results_keyboard_page_gate', gated)
        self.assertIn('retainResultsInputEvents()', gated)
        self.assertIn("pageCheck.status='post-page-keyboard-dispatched';", gated)
        self.assertIn("row.status==='post-page-keyboard-dispatched'", harness)
        validation_start = harness.index('if(keyboardGatedMode){',
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
        self.assertIn('assertResultsCpuPagesAfterP1KeyboardPrefix', trace_helper)
        self.assertIn('first P1 Start edge must be consumed from original Results phase 2', trace_helper)
        self.assertIn('expectedDisconnectedCpuSlots', trace_helper)
        self.assertIn('expectedPortErrors', trace_helper)
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

    def test_camera_entry_observer_brackets_onenter_and_collision_adoption_ownership(self):
        context = (ROOT / 'src/gameplay_results_context.c').read_text(encoding='utf-8')
        begin = context.index('MeleeWebResultsContext* melee_web_results_context_begin(')
        end = context.index('\nint melee_web_results_context_tick(', begin)
        body = context[begin:end]
        boundaries = [
            'context->camera_allocation_generation_before_onenter =',
            'context->camera_pool_before_onenter = cm_804D645C;',
            'context->owner_camera_pool_before_onenter = owner_camera_pool;',
            'gm_Scene_Results_OnEnter(&context->match);',
            'context->camera_allocation_generation_after_onenter =',
            'context->camera_pool_after_onenter = cm_804D645C;',
            'melee_web_collision_adopt_dummy(error,error_size)',
            'context->camera_pool_after_collision_adoption = cm_804D645C;',
            'context->camera_allocation_generation_after_collision_adoption =',
            'context->camera_allocation_subject_count_after_collision_adoption =',
            'context->camera_pool = context->camera_pool_after_onenter;',
            'owner_camera_pool = context->camera_pool;',
            'camera_pool_owned(context, "collision adoption"',
        ]
        positions = [body.index(boundary) for boundary in boundaries]
        self.assertEqual(positions, sorted(positions))
        self.assertIn('camera_pool_owned(context, "tick entry"', context)
        self.assertIn('camera_pool_owned(context, "source draw"', context)
        self.assertIn('camera_pool_owned(context, "scene exit entry"', context)
        self.assertIn('camera_pool_owned(context, "close entry"', context)
        self.assertIn('source_generation == context->camera_allocation_generation', context)
        self.assertIn('source_allocation_generation=%u expected_generation=%u', context)
        self.assertIn('source_camera_allocation_generation_after_onenter', context)
        release_start = context.index('static int release_source_camera_and_ground(')
        release_end = context.index('\nstatic void restore_pad_and_source_globals', release_start)
        release = context[release_start:release_end]
        self.assertIn('cm_804D645C != context->saved_camera_pool', release)
        self.assertIn('Original Results camera globals were not restored after scene teardown', release)
        self.assertLess(release.index('stage_info = context->saved_stage;'),
                        release.index('Original Results camera globals were not restored after scene teardown'))
        camera_patch = (ROOT / 'patches/melee-gameplay.patch').read_text(encoding='utf-8')
        self.assertIn('melee_web_camera_pool_last_subject_count = n_subjects;', camera_patch)
        self.assertIn('++melee_web_camera_pool_allocation_generation;', camera_patch)

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
        self.assertIn('source_camera_allocation_generation_before_onenter', browser)
        self.assertIn('source_camera_allocation_generation_after_onenter', browser)
        self.assertIn('source_camera_allocation_generation_after_collision_adoption', browser)
        harness = (ROOT / 'tests/fighter_cpu9_lineup_browser_test.mjs').read_text(encoding='utf-8')
        self.assertIn('source_camera_allocation_subject_count_after_onenter,8', harness)
        self.assertIn('source_camera_allocation_generation_after_collision_adoption', harness)


if __name__ == '__main__':
    unittest.main()
