import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "tests" / "fighter_cpu9_lineup_browser_test.mjs"
RUNNER = ROOT / "scripts" / "runtime_incident_campaign.mjs"


class RuntimeIncidentCampaignTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.harness = HARNESS.read_text(encoding="utf-8") + (ROOT / "tests/runtime_callback_recorder.mjs").read_text(encoding="utf-8")
        cls.runner = RUNNER.read_text(encoding="utf-8")

    def test_cpu_conversion_reselects_every_requested_fighter(self):
        self.assertIn("for(let door=0;door<4;door++)await selectCpuCharacter(door,expected[door]);", self.harness)
        self.assertNotIn("for(const door of [2,3])await selectCpuCharacter(door,expected[door]);", self.harness)

    def test_campaign_flags_and_authored_stage_ids(self):
        self.assertIn("'stop-on-timing-pause':{type:'boolean'}", self.harness)
        self.assertIn("'stage-kind':{type:'string'}", self.harness)
        self.assertIn("'wall-bound-seconds':{type:'string'}", self.harness)
        self.assertIn("'stage-setup-only':{type:'boolean'}", self.harness)
        self.assertIn("'stage-map-preflight':{type:'boolean'}", self.harness)
        self.assertIn("sourceId:0x1F", self.harness)
        self.assertNotIn("battlefield:Object.freeze({id:'battlefield',name:'Battlefield',sourceId:0x01", self.harness)
        self.assertIn("const stageKind=values['stage-kind']||'final-destination'", self.harness)
        self.assertIn("wallBoundSeconds=campaignRequested?Number(values['wall-bound-seconds']??(stageSetupOnly?120:600)):null", self.harness)

    def test_stage_map_preflight_binds_to_authored_pinned_enum(self):
        header = ROOT / ".deps" / "melee" / "src" / "melee" / "gr" / "forward.h"
        if not header.is_file():
            self.skipTest("pinned authored stage enum is unavailable in this checkout")
        result = subprocess.run(
            ["node", str(HARNESS), "--stage-map-preflight"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        preflight = json.loads(result.stdout)
        self.assertEqual(preflight["result"], "pass")
        authored = preflight["authored"]
        self.assertEqual(authored["final-destination"], 0x20)
        self.assertEqual(authored["battlefield"], 0x1F)
        for name, expected in (("St_Kind_Last", 0x20), ("St_Kind_Battle", 0x1F)):
            match = re.search(rf"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*{name}\b", header.read_text())
            self.assertIsNotNone(match, name)
            self.assertEqual(int(match.group(1), 16), expected)

    def test_stage_setup_only_has_bounded_sss_readiness_boundary(self):
        self.assertIn("if(stageSetupOnly&&!stopOnTimingPause)", self.harness)
        self.assertIn("const ready=await waitForMatchReady('original four-player match entry',stageSetupOnly?30000:60000);", self.harness)
        self.assertIn("report.result='stage-setup-pass';", self.harness)
        self.assertIn("stage_setup_only:stageSetupOnly", self.harness)
        setup_branch = "else if(stageSetupOnly){\n    await chooseStage();\n    report.result='stage-setup-pass';\n  }"
        self.assertIn(setup_branch, self.harness)
        self.assertLess(self.harness.index("await chooseStage();"), self.harness.index("else await runMatch(1,lineup);"))

    def test_pause_mode_retains_receipt_and_blocks_resume_paths(self):
        self.assertIn("timing-pause-receipt-v1", self.harness)
        self.assertIn("timing-pause-${suffix}.json", self.harness)
        self.assertIn("timing-pause-${suffix}", self.harness)
        self.assertGreaterEqual(self.harness.count("failOnTimingPause"), 6)
        self.assertIn("if(stopOnTimingPause)await failOnTimingPause(`Results ${matchIndex}`,state);", self.harness)
        self.assertIn("if(stopOnTimingPause)await checkTimingPause(`${label} before PAD`,await diagnostic());", self.harness)
        self.assertIn("if(stopOnTimingPause)await checkTimingPause(`${label} after PAD`,await diagnostic());", self.harness)
        self.assertIn("await checkTimingPause('CSS observation',state);", self.harness)

    def test_match_entry_requires_native_readiness_and_four_player_observations(self):
        self.assertIn("async function waitForMatchReady(label,timeoutMs=60000)", self.harness)
        self.assertIn("state?.match?.ready===true", self.harness)
        self.assertIn("const MATCH_PLAYER_SLOTS=Object.freeze(['p0','p1','p2','p3']);", self.harness)
        self.assertIn("if(readiness.ready&&readiness.missing_players.length===0)return state;", self.harness)
        self.assertIn("melee-web-match-readiness-failure-v1", self.harness)
        self.assertIn("match-readiness-${suffix}.json", self.harness)
        self.assertIn("match_readiness_failures", self.harness)
        self.assertIn("const ready=await waitForMatchReady('original four-player match entry',stageSetupOnly?30000:60000);", self.harness)
        self.assertLess(
            self.harness.index("const ready=await waitForMatchReady('original four-player match entry',stageSetupOnly?30000:60000);"),
            self.harness.index("async function runMatch(matchIndex,expected)"),
        )

    def test_readiness_receipt_distinguishes_runtime_and_observer_failures(self):
        result = subprocess.run(
            ["node", str(HARNESS), "--readiness-preflight"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        preflight = json.loads(result.stdout)
        self.assertEqual(preflight["result"], "pass")
        self.assertEqual(
            [(row["name"], row["reason"]) for row in preflight["observed"]],
            [
                ("preparation", None),
                ("runtime-error-outside-match", "runtime-error"),
                ("phase-seven-left-before-ready", "phase-left-before-ready"),
                ("observer-error", "match-observer-error"),
            ],
        )
        self.assertEqual(preflight["timeout_phase"], 3)
        self.assertEqual(preflight["timeout_reason"], "timeout")
        self.assertIn("function matchReadinessFailureReason(state,sawPhase7)", self.harness)
        self.assertIn("if(sawPhase7&&state?.phase!==7)return 'phase-left-before-ready';", self.harness)
        self.assertIn("const failureReason=matchReadinessFailureReason(state,sawPhase7)??'timeout';", self.harness)
        self.assertIn("await failOnMatchReadiness(label,state,failureReason);", self.harness)
        self.assertIn("if(state?.assetFatal)return 'asset-fatal';", self.harness)
        self.assertIn("if(state?.nativeCommandError)return 'native-command-error';", self.harness)
        self.assertIn("if(readiness.observer_error)return 'match-observer-error';", self.harness)
        self.assertIn("'phase-left-before-ready'", self.harness)

    def test_callback_capture_is_bounded_and_scalar(self):
        self.assertIn("max_samples:100,max_incidents:24", self.harness)
        self.assertIn("SAMPLE_INTERVAL_MS=100", self.harness)
        self.assertIn("reason_counts:Array(10).fill(0),unknown_reason_count:0", self.harness)
        self.assertIn("dropped_reason_counts:Array(10).fill(0),dropped_unknown_reason_count:0", self.harness)
        self.assertIn("invalid_preparation_count:0,dropped_invalid_preparation_count:0", self.harness)
        self.assertIn("const reasonBucket=reasonCode!==null&&reasonCode>=0&&reasonCode<10?reasonCode:null;", self.harness)
        self.assertIn("if(droppedBucket===null)capture.dropped_unknown_reason_count++;", self.harness)
        self.assertIn("globalThis.menuDiagnosticSample=(...args)=>", self.harness)
        self.assertIn("globalThis.menuDiagnosticIncident=(...args)=>", self.harness)
        self.assertIn("source_commit:null,runtime_hash:null,build_profile:'unknown'", self.harness)
        self.assertIn("artifact_scope:'private-development-artifact'", self.harness)
        self.assertIn("hash_scope:'exact SHA-256 of gameplay_menu_browser.wasm bytes; not the public runtime graph'", self.harness)
        self.assertIn("public_runtime_graph_bound:false", self.harness)
        self.assertIn("if(capture.incidents.length>MAX_INCIDENTS){", self.harness)
        self.assertIn("const dropped=capture.incidents.shift();capture.dropped_incidents++;", self.harness)
        self.assertIn("runtime_diagnostics:report.runtime_diagnostics", self.harness)
        self.assertIn("if(capture.samples.length>MAX_SAMPLES){capture.samples.shift();capture.dropped_samples++;}", self.harness)
        self.assertNotIn("exportDiagnostics", self.harness)

    def test_incident_reason_preflight_preserves_ring_and_dropped_counts(self):
        result = subprocess.run(
            ["node", str(HARNESS), "--incident-capture-preflight"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        preflight = json.loads(result.stdout)
        self.assertEqual(preflight["result"], "pass")
        self.assertEqual(preflight["capture_status"], "installed")
        preparation = preflight["preparation_only"]
        self.assertEqual(preparation["reason_counts"][7], 25)
        self.assertEqual(preparation["dropped_reason_counts"][7], 1)
        guard = preflight["guard_after_preparation"]
        self.assertEqual(guard["reason_counts"][1], 1)
        self.assertEqual(guard["dropped_reason_counts"][1], 1)
        self.assertEqual(preflight["invalid_preparation"]["invalid_preparation_count"], 1)
        self.assertEqual(preflight["invalid_preparation"]["dropped_invalid_preparation_count"], 1)
        self.assertEqual(preflight["unknown_reason"]["unknown_reason_count"], 1)

    def test_tracked_incident_observer_preserves_unexpected_and_missing_accounting(self):
        result = subprocess.run(
            ["node", str(HARNESS), "--incident-capture-preflight"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        preflight = json.loads(result.stdout)
        self.assertEqual(preflight["result"], "pass")
        self.assertEqual(preflight["capture_status"], "installed")
        guard = preflight["guard_after_preparation"]
        self.assertEqual(guard["reason_counts"][1], 1)
        self.assertEqual(guard["dropped_reason_counts"][1], 1)
        invalid = preflight["invalid_preparation"]
        self.assertEqual(invalid["invalid_preparation_count"], 1)
        self.assertEqual(invalid["dropped_invalid_preparation_count"], 1)
        self.assertEqual(preflight["unknown_reason"]["unknown_reason_count"], 1)

    def test_runner_has_frozen_four_attempt_plan(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [
                    "node",
                    str(RUNNER),
                    "--url",
                    "http://127.0.0.1:1234/runtime.html",
                    "--disc",
                    "owned.ciso",
                    "--out-root",
                    directory,
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            plan = json.loads(result.stdout)
            plan_path = Path(plan["plan_path"])
            self.assertTrue(plan_path.is_file())
            self.assertEqual(json.loads(plan_path.read_text(encoding="utf-8")), plan)
        self.assertEqual(plan["attempt_count"], 4)
        self.assertEqual(plan["contention"], "shared-host-uncontrolled")
        self.assertEqual(plan["condition"], "shared-host-uncontrolled")
        self.assertEqual(Path(plan["receipt_path"]).name, "campaign-receipt.json")
        self.assertEqual(plan["browser_context"]["kind"], "persistent-user-data-dir")
        self.assertEqual(plan["browser_context"]["driver_cache"], "uncontrolled")
        self.assertEqual(plan["reducer_packet"]["experiments_per_boundary"], 2)
        self.assertEqual(
            [(row["lineup"], row["stage_kind"], row["cache"]) for row in plan["attempts"]],
            [
                ("A", "final-destination", "cold"),
                ("A", "final-destination", "warm"),
                ("B", "battlefield", "cold"),
                ("B", "battlefield", "warm"),
            ],
        )
        for row in plan["attempts"]:
            self.assertEqual(row["matches"], 2)
            self.assertEqual(row["wall_bound_seconds"], 600)
            self.assertTrue(row["stop_on_timing_pause"])
            self.assertEqual(row["audio"], "enabled")
            self.assertIn("--stop-on-timing-pause", row["args"])
            self.assertIn("--user-data-dir", row["args"])
        profile_args = [row["args"][row["args"].index("--user-data-dir") + 1] for row in plan["attempts"]]
        self.assertEqual(len(set(profile_args)), 1)
        self.assertEqual(Path(profile_args[0]).name, ".campaign-browser-profile")

    def test_runner_contention_label_and_cache_request(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [
                    "node",
                    str(RUNNER),
                    "--url",
                    "https://webmelee.gg/runtime.html",
                    "--disc",
                    "owned.ciso",
                    "--out-root",
                    directory,
                    "--contention",
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
        plan = json.loads(result.stdout)
        self.assertEqual(plan["contention"], "controlled-contention")
        self.assertEqual(plan["condition"], "controlled-contention")
        self.assertTrue(all(row["label"] == "controlled-contention" for row in plan["attempts"]))
        self.assertTrue(all("--controlled-contention" in row["args"] for row in plan["attempts"]))
        cold, warm = plan["attempts"][0], plan["attempts"][1]
        self.assertIn("render-cache=clear", cold["url"])
        self.assertNotIn("render-cache=clear", warm["url"])

    def test_harness_persistent_context_and_shared_host_label(self):
        self.assertIn("'user-data-dir':{type:'string'}", self.harness)
        self.assertIn("chromium.launchPersistentContext(userDataDirectory,launchConfig)", self.harness)
        self.assertIn("cache_reuse:userDataDirectory?'campaign-shared-origin-profile':'temporary-context'", self.harness)
        self.assertIn("driver_cache:'uncontrolled'", self.harness)
        self.assertIn("campaignCondition=controlledContention?'controlled-contention':'shared-host-uncontrolled'", self.harness)
        cleanup = self.harness.split("async function closeOwnedBrowserResources(){", 1)[1].split(
            "const onOwnedInterrupt=", 1)[0]
        self.assertIn("['context',browserContext,owner=>owner.close()]", cleanup)
        self.assertIn("if(!resource)continue;", cleanup)
        self.assertIn("if(!ownedClosePromises.has(resource))", cleanup)
        self.assertIn("await ownedClosePromises.get(resource);", cleanup)
        self.assertIn("report.cleanup[name]={status:'failed',error:message};", cleanup)
        self.assertIn("report.result='fail';process.exitCode=1;", cleanup)
        self.assertIn("}finally{\n    await closeOwnedBrowserResources();", self.harness)

    def test_campaign_wall_watchdog_covers_setup_through_results(self):
        self.assertIn("campaignWallBoundTimer=armCampaignWallWatchdog();", self.harness)
        self.assertIn("function triggerCampaignWallBound()", self.harness)
        self.assertIn("campaign_wall_bound_exceeded", self.harness)
        self.assertIn("await bounded(retainRuntimeDiagnosticsCapture());", self.harness)
        self.assertIn("await bounded(page.close());", self.harness)
        self.assertIn("await bounded(browserContext.close());", self.harness)
        self.assertIn("if(timer!==null)clearTimeout(timer);", self.harness)
        self.assertIn("capture-timeout", self.harness)
        self.assertIn("?'no-page-to-capture':", self.harness)
        self.assertIn("if(campaignWallBoundExceeded){", self.harness)
        self.assertIn("report.result='fail';process.exitCode=1;", self.harness)
        self.assertIn("deadline:campaignDeadline===null?Date.now()+65*60*1000:campaignDeadline", self.harness)
        self.assertIn("clearTimeout(campaignWallBoundTimer);campaignWallBoundTimer=null;", self.harness)
        self.assertLess(
            self.harness.index("checkCampaignWallBound();\n  report.browser={"),
            self.harness.index("report.browser={"),
        )
        watchdog = self.harness[
            self.harness.index("function triggerCampaignWallBound()"):
            self.harness.index("function checkCampaignWallBound()")
        ]
        self.assertNotIn("menuDiagnosticPad", watchdog)
        self.assertNotIn("menuDiagnostic", watchdog)
        self.assertNotIn("sourcepolicy", watchdog.lower())

    def test_successful_campaign_clears_watchdog_and_tears_down_cache(self):
        self.assertIn("status=['pass','setup-only-pass','stage-setup-pass','results-observation-pass'].includes(report.result)?", self.harness)
        self.assertIn("report.campaign_wall_bound.timer_status='cleared';", self.harness)
        self.assertIn("async function unloadAfterNaturalResultsCss()", self.harness)
        self.assertIn("if(!report.results_observation_only)\n      await unloadAfterNaturalResultsCss();", self.harness)
        self.assertIn("await driver.unload();", self.harness)
        self.assertIn("melee-web-runtime-cache-teardown-v1", self.harness)
        self.assertIn("module_save_runtime_cache_owner:'web/melee-runtime.mjs unloadAndSave'", self.harness)
        self.assertIn("nativeCacheIdle===1?'pass':'cache-write-failed'", self.harness)
        self.assertIn("receipt.save_observed=", self.harness)
        self.assertIn("cache-teardown.json", self.harness)
        self.assertIn("await retainRuntimeDiagnosticsCapture();\n    await persistReceipt();\n    throw error;", self.harness)
        self.assertIn("Number(cache.file_bytes)>0", self.harness)
        self.assertIn("Number(cache.clears)===0", self.harness)

    def test_runner_freezes_plan_and_writes_stopping_receipt(self):
        self.assertIn("campaign-plan.json", self.runner)
        self.assertIn("campaign-receipt.json", self.runner)
        self.assertIn("melee-web-runtime-incident-campaign-receipt-v1", self.runner)
        self.assertIn("receipt.stop={reason:'attempt-failed'", self.runner)
        self.assertIn("profile_cleanup='preserved-after-failure'", self.runner)
        self.assertIn("if(receipt.status==='pass')", self.runner)

    def test_cache_warmth_requires_observed_runtime_status(self):
        self.assertIn("cache_evidence.restore_observed", self.runner)
        self.assertIn("actual native warmth requires harness cache_evidence.restore_observed", self.runner)
        self.assertIn("Module.runtimeCacheState?{state:Module.runtimeCacheState.state", self.harness)
        self.assertIn("requested_cache:url.searchParams.get('render-cache')==='clear'?'cold':'warm'", self.harness)
        self.assertIn("evidence.status='observed-populated'", self.harness)


if __name__ == "__main__":
    unittest.main()
