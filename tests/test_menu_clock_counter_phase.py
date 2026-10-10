"""Execute candidate source clock APIs with typed source records and fixture services."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import unittest
ROOT = Path(os.environ.get('MELEE_REPO_ROOT', Path(__file__).resolve().parents[1])).resolve()
CANDIDATE = Path(os.environ.get('MELEE_CLOCK_COUNTER_CANDIDATE', ROOT)).resolve()
sys.path.insert(0, str(ROOT / 'tests'))
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function


def added(source):
    return '\n'.join(line[1:] for line in source.splitlines()
                     if line.startswith('+') and not line.startswith('+++'))


def structure(source, signature):
    return _function(source, signature) + ';'


class MenuClockCounterPhaseTests(OwnedWorkspaceTests):
    def test_actual_source_counter_phase_and_checked_restoration(self):
        compiler = shutil.which(os.environ.get('CC', 'cc'))
        original = ROOT / '.deps/melee/src/melee'
        if not compiler or not (original / 'gm/types.h').is_file():
            self.skipTest('Pinned original source and native C compiler required')
        patch_path = CANDIDATE / 'patches/melee-gameplay.patch'
        header_path = CANDIDATE / 'src/gameplay_match_clock.h'
        patch = added(patch_path.read_text())
        types = (original / 'gm/types.h').read_text()
        gm1601 = (original / 'gm/gm_1601.c').read_text()
        gm136 = (original / 'gm/gm_1A36.c').read_text()
        controller = (original / 'gm/gm_1A36.static.h').read_text()
        controller = controller[controller.index('struct gm_controller_map {'):
                                controller.index('} controller_map;') + len('} controller_map;')]
        clock_globals = patch[patch.index('static struct gm_80479D58_t melee_source_clock_saved;'):
                              patch.index('static int melee_source_clock_owned(void)')]
        controller_globals = patch[patch.index('static struct controller_map melee_controller_map_saved;'):
                                   patch.index('int melee_web_controller_map_begin(void)')]
        signatures = [
            'static int melee_source_clock_owned(void)',
            'static int melee_source_clock_owner_valid(int owner)',
            'static int melee_source_clock_state_supported(void)',
            'int melee_web_source_clock_begin(int owner)',
            'int melee_web_source_clock_pre(void (*on_frame)(void))',
            'int melee_web_source_clock_post(void)',
            'int melee_web_source_clock_present(void)',
            'int melee_web_source_clock_request(int* request)',
            'int melee_web_source_clock_end(void)',
            'int melee_web_source_clock_end_failure(void)',
            'static int melee_menu_clock_counters_idle(void)',
            'int melee_web_menu_clock_capture_counters(',
            'int melee_web_menu_clock_begin_with_counters(',
            'int melee_web_menu_clock_finish_constructor(void)',
            'int melee_web_menu_clock_begin(void)',
            'int melee_web_menu_clock_request(int* request)',
            'int melee_web_menu_clock_tick(void)',
            'int melee_web_menu_clock_present(void)',
            'int melee_web_menu_clock_end(void)',
        ]
        parts = [r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gameplay_match_clock.h"
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
typedef uint64_t u64; typedef int32_t s32;
#define UNK_T void*
#define PAD_MAX_CONTROLLERS 4
''', structure(types, 'struct gm_801677C0_s {'),
            structure(types, 'struct gm_80479D58_t {'), controller, r'''
static struct gm_80479D58_t gm_80479D58;
static struct { u64* unk_2; } HSD_GObjLibInitData;
static void* HSD_GObj_804D781C;
static void* HSD_GObj_804D7838;
static void* HSD_GObj_804D7830;
static void* HSD_GObj_804D7814;
static void* HSD_GObj_804D7818;
static u64 gm_803DA8C8[2]={1,2};
static int gm_801A46B8(int bit) { return (gm_80479D58.unk_10.x2>>bit)&1; }
static int gm_801A45E8(int bit) { return (gm_80479D58.unk_10.x0>>bit)&1; }
static int lb_80019A30(int index) { (void)index; return 1; }
static u64 gm_801A48A4(u8 mask) { return mask; }
static void gm_EvaluateAllControllerInputs(void) { assert(0); }
''', _function(gm1601, 'void gm_801677C0('),
            _function(gm136, 'static void fn_801A396C('),
            _function(gm136, 'void gm_801A3E88('), controller_globals,
            _function(patch, 'int melee_web_controller_map_begin(void)'),
            _function(patch, 'int melee_web_controller_map_end(void)'), clock_globals]
        parts.extend(_function(patch, signature) for signature in signatures)
        parts.append(CONTROL)
        code = '\n\n'.join(parts)
        workspace = self.new_workspace(ROOT, 'menu-clock-counter-phase-')
        source = workspace / 'control.c'; binary = workspace / 'control'
        source.write_text(code)
        evidence_env = os.environ.get('MELEE_CLOCK_COUNTER_EVIDENCE')
        evidence = Path(evidence_env) if evidence_env else workspace / 'evidence'
        evidence.mkdir(parents=True, exist_ok=False)
        (evidence / 'generated.c').write_text(code)
        inputs = [patch_path, header_path, original / 'gm/types.h',
                  original / 'gm/gm_1601.c', original / 'gm/gm_1A36.c',
                  original / 'gm/gm_1A36.static.h']
        (evidence / 'source-bindings.json').write_text(json.dumps([
            {'path': str(path), 'bytes': path.stat().st_size,
             'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for path in inputs], indent=2)+'\n')
        for label, command in [('compile', [compiler, '-std=c11', '-O0', '-Wall', '-Wextra',
                                '-Werror', '-I', str(header_path.parent), str(source), '-o', str(binary)]),
                               ('control', [str(binary)])]:
            result = subprocess.run(command, cwd=workspace, capture_output=True, text=True, timeout=30)
            (evidence / (label+'.json')).write_text(json.dumps(
                {'argv': command, 'cwd': str(workspace), 'returncode': result.returncode}, indent=2)+'\n')
            (evidence / (label+'.stdout')).write_text(result.stdout)
            (evidence / (label+'.stderr')).write_text(result.stderr)
            self.assertEqual(result.returncode, 0, f'{label} failure retained: {evidence}\n{result.stderr}')
        if evidence_env:
            shutil.copy2(binary, evidence / 'control-binary')


CONTROL = r'''
static unsigned callbacks;
static void callback(void) { ++callbacks; }
static bool gate(void) { return true; }
static void foreign_controller(int i) { (void)i; }
static u64 caller_mask=UINT64_C(0xABCDEF);
static struct gm_80479D58_t caller, active;
static struct controller_map caller_controller;
static MeleeWebMenuClockCounters triple={149,11,17};
static void reset_caller(void) {
    assert(melee_source_clock_owner==0);
    memset(&gm_80479D58,0,sizeof(gm_80479D58));
    gm_80479D58.unk_0=5001;gm_80479D58.unk_4=5002;gm_80479D58.unk_8=5003;
    gm_80479D58.unk_C=2;
    gm_80479D58.unk_10.x0=2;gm_80479D58.unk_10.x1=1;
    gm_80479D58.unk_10.x2=1;gm_80479D58.unk_10.x3=3;
    gm_80479D58.unk_10.x4[0]=gate;gm_80479D58.unk_10.x4[1]=gate;
    gm_80479D58.unk_10.xC=&caller_mask;
    gm_80479D58.unk_10.unk_20=123;gm_80479D58.unk_10.unk_28=456;
    gm_80479D58.unk_10.unk_30=callback;gm_80479D58.unk_10.unk_34=1;
    gm_80479D58.unk_10.unk_38_0=gm_80479D58.unk_10.unk_38_1=1;
    memset(&controller_map,0,sizeof(controller_map));controller_map.xF0=foreign_controller;
    controller_map.xF4=77;
    HSD_GObjLibInitData.unk_2=&caller_mask;
    caller=gm_80479D58;caller_controller=controller_map;
}
static void restored(void) {
    assert(!memcmp(&gm_80479D58,&caller,sizeof(caller)));
    assert(!memcmp(&controller_map,&caller_controller,sizeof(controller_map)));
    assert(HSD_GObjLibInitData.unk_2==&caller_mask);
    assert(!melee_source_clock_owner&&!melee_menu_clock_constructor_pending);
}
static void refusal_capture(void) {
    MeleeWebMenuClockCounters out={91,92,93}, before=out;
    active=gm_80479D58;
    assert(!melee_web_menu_clock_capture_counters(&out));
    assert(!memcmp(&out,&before,sizeof(out)));
    assert(!memcmp(&active,&gm_80479D58,sizeof(active)));
}
int main(void) {
    struct gm_80479D58_t ordinary, expected;
    struct controller_map map_before;
    u64* mask;
    MeleeWebMenuClockCounters captured;
    int request=-1;
    reset_caller();assert(!melee_web_menu_clock_begin_with_counters(NULL));restored();
    assert(!melee_web_menu_clock_capture_counters(NULL));restored();refusal_capture();
    /* Existing begin is the non-counter authority, not a rewritten reset. */
    assert(melee_web_menu_clock_begin());ordinary=gm_80479D58;
    assert(!melee_web_menu_clock_capture_counters(NULL));
    gm_80479D58.unk_0=149;gm_80479D58.unk_4=11;gm_80479D58.unk_8=17;
    assert(melee_web_menu_clock_capture_counters(&captured));
    assert(!memcmp(&captured,&triple,sizeof(triple)));
    melee_source_clock_prepared=1;refusal_capture();melee_source_clock_prepared=0;
    melee_source_clock_in_callback=1;refusal_capture();melee_source_clock_in_callback=0;
    void** contexts[]={&HSD_GObj_804D781C,&HSD_GObj_804D7838,&HSD_GObj_804D7830,
                      &HSD_GObj_804D7814,&HSD_GObj_804D7818};
    for(unsigned i=0;i<5;i++){*contexts[i]=&caller;refusal_capture();*contexts[i]=NULL;}
    mask=HSD_GObjLibInitData.unk_2;HSD_GObjLibInitData.unk_2=&caller_mask;
    refusal_capture();HSD_GObjLibInitData.unk_2=mask;
    assert(melee_web_menu_clock_end());restored();
    assert(melee_web_source_clock_begin(MELEE_WEB_SOURCE_CLOCK_MATCH));
    refusal_capture();active=gm_80479D58;
    assert(!melee_web_menu_clock_begin_with_counters(&captured));
    assert(!melee_web_menu_clock_finish_constructor());
    assert(!memcmp(&active,&gm_80479D58,sizeof(active)));
    assert(melee_web_source_clock_end());restored();
    assert(melee_web_menu_clock_begin_with_counters(&captured));
    expected=ordinary;expected.unk_0=149;expected.unk_4=11;expected.unk_8=17;
    assert(!memcmp(&expected,&gm_80479D58,sizeof(expected)));
    assert(melee_menu_clock_constructor_pending);refusal_capture();
    active=gm_80479D58;map_before=controller_map;mask=HSD_GObjLibInitData.unk_2;
    assert(!melee_web_source_clock_pre(callback));assert(!callbacks);
    assert(!melee_web_source_clock_post());assert(!melee_web_source_clock_present());
    assert(!melee_web_menu_clock_tick());assert(!melee_web_menu_clock_present());
    assert(!melee_web_menu_clock_begin_with_counters(&triple));
    assert(!memcmp(&active,&gm_80479D58,sizeof(active)));
    assert(!memcmp(&map_before,&controller_map,sizeof(map_before)));
    assert(HSD_GObjLibInitData.unk_2==mask);
    /* Existing zero request observation must remain usable before OnEnter. */
    assert(melee_web_menu_clock_request(&request)&&request==0);
    for(unsigned i=0;i<5;i++){
        *contexts[i]=&caller;assert(!melee_web_menu_clock_finish_constructor());
        assert(!memcmp(&active,&gm_80479D58,sizeof(active)));*contexts[i]=NULL;
    }
    melee_source_clock_prepared=1;assert(!melee_web_menu_clock_finish_constructor());
    melee_source_clock_prepared=0;melee_source_clock_in_callback=1;
    assert(!melee_web_menu_clock_finish_constructor());melee_source_clock_in_callback=0;
    HSD_GObjLibInitData.unk_2=&caller_mask;
    assert(!melee_web_menu_clock_finish_constructor());
    assert(!melee_web_menu_clock_end());assert(melee_menu_clock_constructor_pending);
    HSD_GObjLibInitData.unk_2=mask;
    assert(melee_web_menu_clock_finish_constructor());
    assert(!melee_menu_clock_constructor_pending);
    assert(!memcmp(&ordinary,&gm_80479D58,sizeof(ordinary)));
    active=gm_80479D58;assert(!melee_web_menu_clock_finish_constructor());
    assert(!memcmp(&active,&gm_80479D58,sizeof(active)));
    assert(melee_web_menu_clock_end());restored();
    /* Constructor failure can end pending; foreign controller cannot be overwritten. */
    assert(melee_web_menu_clock_begin_with_counters(&captured));
    controller_map.xF0=foreign_controller;
    assert(!melee_web_menu_clock_end());assert(melee_menu_clock_constructor_pending);
    controller_map.xF0=fn_801A396C;
    assert(melee_web_menu_clock_end());restored();
    assert(melee_web_menu_clock_begin());
    assert(!melee_menu_clock_constructor_pending);assert(melee_web_menu_clock_end());restored();
    puts("PASS actual counter APIs: unchanged begin/nontriple state; MENU-only capture; pending guards; one finish; checked caller restoration");
    return 0;
}
'''
if __name__ == '__main__': unittest.main(verbosity=2)
