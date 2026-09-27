"""Real-asset Results owner rejection and source mode-exit controls.

These synthetic standings are not a reproduction of a natural match failure.
"""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class ResultsCameraOwnershipTests(unittest.TestCase):
    def test_host_handoff_rejects_each_boundary_without_repeating_callback(self):
        """Production host dispatch, with faulting owner/commit seams only.

        The real-asset case below checks the actual profile and camera owners;
        this smaller test can inject a failure after callback/commit writes.
        """
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("C compiler required")
        source = (ROOT / "src/gameplay_menu_host.c").read_text(encoding="utf-8")
        start = source.index("static int results_handoff_owned(")
        end = source.index("\nint melee_web_menu_host_results_end(", start)
        production = source[start:end]
        harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct { int live; } Profile;
typedef struct {
    Profile* profile;
    int results_active, results_exited;
} MeleeWebMenuHost;
typedef struct GameModeState {
    void (*on_exit)(struct GameModeState*);
} GameModeState;
enum { gmVsMode_State_Css = 1, gmVsMode_State_Prize = 192 };
static Profile profile;
static MeleeWebMenuHost host, *owner = &host;
static GameModeState gm_Mode_Vs_States[5];
static int stage, fault_stage, fault_kind, camera_live, destination;
static int callbacks, commits, profile_checks, camera_checks;
static const char* phases[] = {"mode OnExit entry", "mode OnExit", "route commit"};
static int fail(char* e, size_t n, const char* text) {
    if(e && n) snprintf(e,n,"%s",text);
    return 0;
}
static int ok(char* e, size_t n) { if(e && n) *e=0; return 1; }
static void inject(void) {
    if(stage != fault_stage) return;
    if(fault_kind == 1) profile.live=0;
    if(fault_kind == 2) camera_live=0;
}
static int melee_web_save_profile_owner_live(Profile* p, char* e, size_t n) {
    ++profile_checks;
    return p->live ? ok(e,n) : fail(e,n,"fixture profile root changed");
}
static int melee_web_results_context_check_handoff(const char* phase, char* e, size_t n) {
    ++camera_checks;
    assert(strcmp(phase,phases[stage]) == 0);
    if(!camera_live) {
        if(e && n) snprintf(e,n,"fixture camera triple changed at %s",phase);
        return 0;
    }
    return ok(e,n);
}
static void on_exit(GameModeState* state) {
    assert(state == &gm_Mode_Vs_States[4]);
    ++callbacks;
    stage=1;
    inject();
}
static int melee_web_vs_mode_next_state(void) { return destination; }
static int commit_results_route(MeleeWebMenuHost* h, char* e, size_t n) {
    assert(h == owner && h->results_exited);
    ++commits;
    stage=2;
    inject();
    return ok(e,n);
}
''' + production + r'''
static void reset(int broken_stage, int kind) {
    profile.live=camera_live=1;
    host=(MeleeWebMenuHost){&profile,1,0};
    gm_Mode_Vs_States[4].on_exit=on_exit;
    callbacks=commits=profile_checks=camera_checks=stage=0;
    fault_stage=broken_stage;
    fault_kind=kind;
    destination=gmVsMode_State_Css;
    inject();
}
int main(void) {
    char error[256];
    for(int boundary=0; boundary<3; ++boundary) {
        for(int kind=1; kind<=2; ++kind) {
            reset(boundary,kind);
            assert(!melee_web_menu_host_results_exit(&host,error,sizeof(error)));
            assert(strstr(error,phases[boundary]));
            assert(callbacks == (boundary != 0));
            assert(commits == (boundary == 2));
            assert(profile_checks == boundary+1);
            assert(camera_checks == boundary+(kind == 2));
            // Only the test repairs the injected fault. A consumed callback
            // must remain non-repeatable even after its owner becomes valid.
            profile.live=camera_live=1;
            fault_stage=-1;
            if(boundary == 0) {
                assert(!host.results_exited);
                assert(melee_web_menu_host_results_exit(&host,error,sizeof(error)));
                assert(callbacks == 1 && commits == 1);
            } else {
                assert(host.results_exited);
                assert(!melee_web_menu_host_results_exit(&host,error,sizeof(error)));
                assert(callbacks == 1 && commits == (boundary == 2));
            }
        }
    }
    reset(-1,0);
    assert(melee_web_menu_host_results_exit(&host,error,sizeof(error)));
    assert(callbacks == 1 && commits == 1 && profile_checks == 3 && camera_checks == 3);
    reset(-1,0);
    destination=gmVsMode_State_Prize;
    assert(melee_web_menu_host_results_exit(&host,error,sizeof(error)));
    assert(callbacks == 1 && commits == 0 && profile_checks == 2 && camera_checks == 2);
    for(int kind=1; kind<=2; ++kind) {
        reset(1,kind);
        destination=gmVsMode_State_Prize;
        assert(!melee_web_menu_host_results_exit(&host,error,sizeof(error)));
        assert(callbacks == 1 && commits == 0);
    }
    puts("Results handoff entry/callback/commit guards and non-repeatable exit passed");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="results-handoff-") as directory:
            root = Path(directory)
            source_path, binary = root / "handoff.c", root / "handoff"
            source_path.write_text(harness, encoding="utf-8")
            compiled = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(source_path), "-o", str(binary)],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("non-repeatable exit passed", result.stdout)

    def _run_trace_cases(self, cases, *, host_route=False):
        roots = [ROOT / "assets-local" / name for name in
                 ("native-menus", "repro-results-v1", "next-gate")]
        required = [roots[0] / "MnSlChr.usd", roots[1] / "GmRst.usd",
                    roots[1] / "SdRst.usd", roots[1] / "TyDatai.usd",
                    roots[2] / "PlSk.dat", roots[2] / "PlZd.dat",
                    roots[2] / "PlYs.dat", roots[2] / "PlSs.dat",
                    roots[2] / "PlFc.dat"]
        # The full host fixture visits CSS/SSS before Results and therefore
        # needs original menu music as well as the standalone Results assets.
        # load_directory includes it from the declared native-menus root.
        if host_route:
            required.append(roots[0] / "menu01.hps")
        if not all(path.is_file() for path in required):
            missing = ", ".join(str(path.relative_to(ROOT)) for path in required
                                if not path.is_file())
            self.skipTest("Owned Results fixture inputs required: " + missing)
        targets = [ROOT / "build" / directory / "gameplay_results_scene_trace.js"
                   for directory in ("browser", "browser-release")]
        targets = [path for path in targets if path.is_file()]
        if not targets:
            self.skipTest("Build gameplay_results_scene_trace first")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        for command, markers in cases.items():
            with self.subTest(command=command):
                invocation = [str(node_runtime()), str(target), command, *map(str, roots)]
                result = subprocess.run(invocation, cwd=ROOT, capture_output=True,
                                        text=True, timeout=90)
                output = "\n".join(line[:500] for line in
                                   (result.stdout + result.stderr).splitlines())[-12000:]
                if result.returncode:
                    failures = ROOT / "work/results-test-failures"
                    failures.mkdir(parents=True, exist_ok=True)
                    retained = Path(tempfile.mkdtemp(prefix="trace-", dir=failures))
                    (retained / "stdout.log").write_text(result.stdout)
                    (retained / "stderr.log").write_text(result.stderr)
                    (retained / "command.json").write_text(json.dumps(
                        {"command": invocation, "exit_code": result.returncode}, indent=2))
                    output = f"Full trace failure: {retained}\n" + output
                self.assertEqual(result.returncode, 0, output)
                for marker in (*markers, "all four participant demo owners constructed and closed"):
                    self.assertIn(marker, result.stdout)

    def test_host_profile_and_camera_entry_faults(self):
        self._run_trace_cases({
            "--lineup-b-results-handoff-guard":
                ("rejected profile root before mode OnExit",
                 "rejected camera pool before mode OnExit",
                 "host OnExit+commit tick=2"),
        }, host_route=True)

    def test_zelda_origin_sheik_three_winner_demo_variants(self):
        # Real source CopyPAD consumer selects B/Y/X; no seed sweep or direct
        # variant setter. This Node lane tests state/lifecycle without drawing;
        # the rendered -host-draw controls require the initialized GPU target.
        cases = {}
        for button, variant, motion, symbol in (
                ("b", 0, 0, "ftCo_MS_DeadDown"),
                ("y", 1, 2, "ftCo_MS_DeadRight"),
                ("x", 2, 5, "ftCo_MS_DeadUpStarIce")):
            command = f"--lineup-b-zelda-sheik-stock-delayed-demo-{button}-host-state"
            cases[command] = (
                "match_kind=1", "winner_ckind=18 winner_ftkind=7",
                f"winner_demo_variant={variant} CopyPAD[2].held={button}",
                f"winner_demo_motion={motion} name={symbol}",
                "draw_scope=unrun native-state-only",
                "winner_demo_delayed_frames=744 source_draw_api_calls=0",
                "host OnExit+commit tick=744")
        self._run_trace_cases(cases, host_route=True)

    def test_live_pool_guard_and_sheik_mode_exit_controls(self):
        self._run_trace_cases({
            "--lineup-b-camera-pool-guard":
                ("rejected tick entry", "rejected draw entry",
                 "rejected scene exit entry", "rejected close entry"),
            "--lineup-b-zelda-sheik-mode-exit": ("winner_ckind=18 winner_ftkind=7",),
            "--lineup-b-sheik-mode-exit": ("winner_ckind=19 winner_ftkind=7",),
            "--lineup-b-sheik-stock-mode-exit":
                ("match_kind=1", "winner_ckind=19 winner_ftkind=7"),
            "--lineup-b-sheik-stock-delayed-mode-exit":
                ("match_kind=1", "winner_ckind=19 winner_ftkind=7", "tick=744"),
        })


if __name__ == "__main__":
    unittest.main()
