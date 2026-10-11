"""Exercise the production diagnostic writer against pinned source layouts."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/gm/forward.h>
#include <sysdolphin/baselib/random.h>
#include <sys/resource.h>
#include <string.h>
#include "gameplay_retail_state.h"
static StaticPlayer players[6];
static Fighter fighters[5];
static HSD_GObj objects[5];
static u32 seed=123;
u32* seed_ptr=&seed;
static int missing_player=-1;
u32 gm_GetFrameCount(void){return 17;}
StaticPlayer* Player_GetPtrForSlot(int slot){return slot==missing_player?NULL:&players[slot];}
Gm_PKind Player_GetPlayerSlotType(s32 slot){return players[slot].slot_type;}
s32 Player_GetStocks(int slot){return players[slot].stocks;}
extern void melee_web_retail_entities(void);
int main(int argc,char** argv){
    struct rlimit no_core={0,0};setrlimit(RLIMIT_CORE,&no_core);
    const CharacterKind characters[4]={CKIND_PURIN,CKIND_POPONANA,CKIND_MARIO,CKIND_FOX};
    const FighterKind kinds[5]={FTKIND_PURIN,FTKIND_POPO,FTKIND_NANA,FTKIND_MARIO,FTKIND_FOX};
    unsigned index=0;
    for(unsigned slot=0;slot<6;slot++){
        players[slot].slot_type=slot<4?Gm_PKind_Cpu:Gm_PKind_NA;
        if(slot>=4)continue;
        players[slot].player_character=characters[slot];
        players[slot].transformed[1]=1;
        players[slot].stocks=4;
        for(unsigned ordinal=0;ordinal<(slot==1?2:1);ordinal++,index++){
            players[slot].player_entity[ordinal]=&objects[index];
            objects[index].user_data=&fighters[index];
            fighters[index].gobj=&objects[index];
            fighters[index].player_id=slot;
            fighters[index].kind=kinds[index];
            fighters[index].cur_pos.x=-0.0f;
            fighters[index].cur_pos.y=1.25f;
        }
    }
    const char* mode=argc>1?argv[1]:"positive";
    if(!strcmp(mode,"missing_primary"))players[2].player_entity[0]=NULL;
    else if(!strcmp(mode,"missing_partner"))players[1].player_entity[1]=NULL;
    else if(!strcmp(mode,"missing_data"))objects[2].user_data=NULL;
    else if(!strcmp(mode,"missing_player"))missing_player=1;
    else if(!strcmp(mode,"missing_tail"))missing_player=5;
    else if(!strcmp(mode,"human"))players[0].slot_type=Gm_PKind_Human;
    else if(!strcmp(mode,"absent_slot"))players[3].slot_type=Gm_PKind_NA;
    else if(!strcmp(mode,"wrong_character"))players[1].player_character=CKIND_ZELDA;
    else if(!strcmp(mode,"wrong_kind"))fighters[2].kind=FTKIND_SEAK;
    else if(!strcmp(mode,"wrong_player"))fighters[2].player_id=0;
    else if(!strcmp(mode,"wrong_backlink"))fighters[2].gobj=&objects[1];
    else if(!strcmp(mode,"duplicate"))players[1].player_entity[1]=&objects[1];
    else if(!strcmp(mode,"cross_slot"))players[1].player_entity[1]=&objects[0];
    else if(!strcmp(mode,"cross_data"))objects[2].user_data=&fighters[0];
    else if(!strcmp(mode,"object_as_data"))objects[2].user_data=&objects[2];
    else if(!strcmp(mode,"extra_partner"))players[2].player_entity[1]=&objects[2];
    else if(!strcmp(mode,"transformed")){players[1].transformed[0]=1;players[1].transformed[1]=0;}
    else if(!strcmp(mode,"bad_form"))players[1].transformed[1]=0;
    else if(!strcmp(mode,"active_tail"))players[5].slot_type=Gm_PKind_Cpu;
    else if(!strcmp(mode,"tail_entity"))players[4].player_entity[0]=&objects[0];
    else if(!strcmp(mode,"tail_partner"))players[5].player_entity[1]=&objects[2];
    else if(strcmp(mode,"positive")&&strcmp(mode,"unchecked"))return 2;
    if(!strcmp(mode,"unchecked"))melee_web_retail_entities();
    else melee_web_retail_entities_checked();
    return 0;
}
'''


class RetailEntityProfileTests(unittest.TestCase):
    def test_checked_writer_matches_full_writer_and_rejects_invalid_owners(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        generated = ROOT / "build/gameplay-source/src"
        if not compiler or not (generated / "melee/ft/types.h").is_file():
            self.skipTest("Native compiler and generated pinned source required")
        (ROOT / "work").mkdir(exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix="retail-entity-profile-", dir=ROOT / "work"))
        harness, executable = scratch / "profile.c", scratch / "profile"
        harness.write_text(HARNESS)
        try:
            includes = [ROOT / "src", generated, ROOT / ".deps/aurora/include",
                        ROOT / ".deps/melee/extern/dolphin/include"]
            command = [compiler, "-std=c11", "-O1", "-DTARGET_PC", "-DAURORA",
                       "-include", str(ROOT / "src/gameplay_compat.h"),
                       *(f"-I{path}" for path in includes),
                       str(ROOT / "src/gameplay_retail_state.c"), str(harness),
                       "-o", str(executable)]
            (scratch / "command.json").write_text(json.dumps(command))
            built = subprocess.run(command, capture_output=True, text=True, timeout=30)
            (scratch / "compiler.log").write_text(built.stdout + built.stderr)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            def run(mode):
                result = subprocess.run([str(executable), mode], capture_output=True,
                                        text=True, timeout=10)
                (scratch / f"{mode}.json").write_text(json.dumps({
                    "returncode": result.returncode, "stdout": result.stdout,
                    "stderr": result.stderr}))
                return result

            checked, unchecked = run("positive"), run("unchecked")
            self.assertEqual(checked.returncode, 0, checked.stderr)
            self.assertEqual(unchecked.returncode, 0, unchecked.stderr)
            self.assertEqual(checked.stdout, unchecked.stdout)
            row = json.loads('{"sentinel":0' + checked.stdout + '}')
            self.assertEqual([(e["slot"], e["entity_index"], e["kind"])
                              for e in row["fighter_entities"]],
                             [(0, 0, 15), (1, 0, 10), (1, 1, 11), (2, 0, 0), (3, 0, 1)])
            for entity in row["fighter_entities"]:
                self.assertEqual(entity["position_bits"], ["80000000", "3fa00000", "00000000"])
                self.assertEqual(entity["input_hex"], "00" * 0x6c)
            for mode in ("missing_primary", "missing_partner", "missing_data", "missing_player",
                         "missing_tail", "human", "absent_slot", "wrong_character", "wrong_kind",
                         "wrong_player", "wrong_backlink", "duplicate", "cross_slot", "cross_data",
                         "object_as_data", "extra_partner", "transformed", "bad_form", "active_tail",
                         "tail_entity", "tail_partner"):
                with self.subTest(mode=mode):
                    rejected = run(mode)
                    self.assertNotEqual(rejected.returncode, 0)
                    self.assertEqual(rejected.stdout, "", "Invalid inventory emitted entity fields")
        except BaseException:
            print("Retained retail entity-profile failure:", scratch)
            raise
        else:
            shutil.rmtree(scratch)
