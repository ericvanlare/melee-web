"""Asset-free exact-source SSS note ownership controls; not constructor acceptance."""
import os
from pathlib import Path
import shlex
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
ROOT = Path(__file__).resolve().parents[1]

PRELUDE = '\n#include <assert.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <string.h>\n#define MELEE_WEB_PAD_STATE_BYTES 822\n#define MELEE_WEB_MENU_SSS_PAIR_ENTRY 1\n#define MELEE_WEB_MENU_SSS_PAIR_RETURN 2\n#define MELEE_WEB_MENU_SSS_READY 2\n#define MELEE_WEB_MENU_CLOSED 0\n#define MELEE_WEB_HOST_SCENE_SSS 2\n#define GM_VS 7\n#define GS_SSS 9\ntypedef struct { unsigned char bytes[64]; } SSSData;\ntypedef struct { SSSData sss; int phase; } MeleeWebMenuSession;\ntypedef struct { int active; uint64_t generation; } MeleeWebAudio;\nstruct GameSceneInfo { int scene_kind; const void *enter_data,*exit_data; };\ntypedef struct { int qcount; void *queue; } PadLib;\n'
STUBS = '\ntypedef struct MeleeWebMenuHost {\n MeleeWebMenuSession *session; MeleeWebAudio *audio,*first_sss_pair_audio_owner;\n int entered,drawing,source_scene,source_mode_kind,vs_mode_owned,queue;\n uint32_t seed; uint64_t generation,audio_generation,first_sss_pair_audio_generation;\n uint64_t first_sss_pair_world_generation,final_pending_css_draw_generation;\n int first_sss_pair_state; char first_sss_pair_error[160];\n MeleeWebMenuFirstSssPairNoteSnapshot first_sss_pair_entry,first_sss_pair_returned;\n struct GameSceneInfo source_scene_info;\n} MeleeWebMenuHost;\nstatic MeleeWebMenuHost *owner; static uint32_t *seed_ptr;\nstatic PadLib HSD_PadLibData;\nstatic void *HSD_GObj_804D781C,*HSD_GObj_804D7838,*HSD_GObj_804D7830,*HSD_GObj_804D7814,*HSD_GObj_804D7818;\nstatic uint64_t generation=11; static int active=1; static uint32_t frame=0;\nstatic struct GameSceneInfo *info;\nstatic int live(MeleeWebMenuHost*h,char*e,size_t n){(void)e;(void)n;return h==owner&&active;}\nstatic const SSSData *melee_web_menu_sss(MeleeWebMenuSession*s){return &s->sss;}\nstatic void *melee_web_current_scene_info(void){return info;}\nstatic int melee_web_menu_phase(MeleeWebMenuSession*s){return s->phase;}\ntypedef struct {uint64_t generation;} Stats;\nstatic Stats melee_web_gameplay_stats(void){return (Stats){generation};}\nstatic uint64_t melee_web_audio_generation(MeleeWebAudio*a){return a->generation;}\nstatic int melee_web_audio_is_active(MeleeWebAudio*a){return a->active;}\nstatic int melee_web_audio_bank_transport_active(void){return active;}\nstatic int gm_GetCurrentGameMode(void){return GM_VS;}\nstatic int gm_GetPreviousGameMode(void){return 6;}\nstatic int gm_GetCurrentSceneIndex(void){return 2;}\nstatic int gm_GetPreviousSceneIndex(void){return 1;}\nstatic uint32_t gm_801A4BA8(void){return frame;}\nstatic void melee_web_pad_state_capture(uint8_t*p){memset(p,0xa5,822);}\n'
CONTROL = '\nstatic MeleeWebMenuHost h;static MeleeWebMenuSession session;static MeleeWebAudio audio;\nstatic void setup(void){memset(&h,0,sizeof h);memset(&session,0,sizeof session);audio=(MeleeWebAudio){1,4};h.session=&session;h.audio=&audio;h.first_sss_pair_audio_owner=&audio;h.audio_generation=h.first_sss_pair_audio_generation=4;h.generation=11;h.final_pending_css_draw_generation=10;h.first_sss_pair_state=2;h.source_scene=2;h.source_mode_kind=7;h.vs_mode_owned=1;h.seed=123;session.phase=2;memset(&session.sss,0x5a,sizeof session.sss);h.source_scene_info=(struct GameSceneInfo){9,&session.sss,&session.sss};info=&h.source_scene_info;owner=&h;seed_ptr=&h.seed;generation=11;active=1;HSD_PadLibData=(PadLib){0,&h.queue};HSD_GObj_804D781C=HSD_GObj_804D7838=HSD_GObj_804D7830=HSD_GObj_804D7814=HSD_GObj_804D7818=NULL;}\nint main(void){\n setup();first_sss_pair_note(&h,&session,&session.sss,149,1);assert(h.first_sss_pair_state==3&&h.first_sss_pair_entry.captured);assert(h.first_sss_pair_entry.scene_frame==0&&h.first_sss_pair_entry.random_seed==123);assert(!memcmp(&h.first_sss_pair_entry.sss,&session.sss,sizeof session.sss));first_sss_pair_note(&h,&session,&session.sss,149,2);assert(h.first_sss_pair_state==4&&h.first_sss_pair_returned.captured);MeleeWebMenuFirstSssPairNoteSnapshot saved=h.first_sss_pair_entry;first_sss_pair_note(&h,&session,&session.sss,149,1);assert(h.first_sss_pair_state==5&&!memcmp(&saved,&h.first_sss_pair_entry,sizeof saved));\n for(int test=0;test<10;test++){setup();SSSData payload={0};uint32_t foreign_seed=0;\n switch(test){case 0:generation=10;h.generation=10;break;case 1:seed_ptr=&foreign_seed;break;case 2:info=NULL;break;case 3:HSD_GObj_804D781C=&h;break;case 4:audio.generation++;break;case 5:h.entered=1;break;case 6:HSD_PadLibData.qcount=1;break;case 7:h.source_scene_info.enter_data=&payload;break;case 8:session.phase=3;break;case 9:h.first_sss_pair_state=3;break;}\n first_sss_pair_note(&h,&session,&session.sss,149,1);assert(h.first_sss_pair_state==5&&!h.first_sss_pair_entry.captured);assert(session.phase==(test==8?3:2));assert(session.sss.bytes[0]==0x5a);\n }\n setup();first_sss_pair_note(&h,&session,&session.sss,149,1);saved=h.first_sss_pair_entry;generation=12;h.generation=12;first_sss_pair_note(&h,&session,&session.sss,149,2);assert(h.first_sss_pair_state==5&&!h.first_sss_pair_returned.captured&&!memcmp(&saved,&h.first_sss_pair_entry,sizeof saved));\n puts("PASS exact host note: paired scalar capture; duplicate retention; 10 owner/phase negatives; changed generation refusal");return 0;\n}\n'

class FirstSssConstructorPairHostTests(OwnedWorkspaceTests):
    def test_exact_host_note_ownership_and_retention(self):
        source = (ROOT / "src/gameplay_menu_host.c").read_text()
        header = (ROOT / "src/gameplay_menu_host.h").read_text()
        start = header.index("typedef struct MeleeWebMenuFirstSssPairNoteSnapshot")
        end = header.index("typedef struct MeleeWebMenuFirstSssTickSnapshot", start)
        code = PRELUDE + header[start:end] + STUBS + _function(source,
            "static void first_sss_pair_fail(") + _function(source,
            "static void first_sss_pair_note(") + CONTROL
        work = self.new_workspace(ROOT, "first-sss-host-")
        c = work / "control.c"
        binary = work / "control"
        c.write_text(code)
        compile_result = subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
            ["-std=c11", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(binary)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (work / "compile.log").write_text(compile_result.stdout)
        self.assertEqual(compile_result.returncode, 0, compile_result.stdout)
        run_result = subprocess.run([str(binary)], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (work / "run.log").write_text(run_result.stdout)
        self.assertEqual(run_result.returncode, 0, run_result.stdout)
        self.assertIn("PASS exact host note", run_result.stdout)

if __name__ == "__main__":
    unittest.main()
