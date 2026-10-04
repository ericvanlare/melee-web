"""The production state observer retains source entity ordinals and float bits."""
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
#include <stdio.h>
#include <string.h>
static StaticPlayer players[4];
static Fighter fighters[6];
static HSD_GObj objects[6];
static u32 seed = 123;
u32* seed_ptr = &seed;
u32 gm_GetFrameCount(void) { return 17; }
StaticPlayer* Player_GetPtrForSlot(int slot) { return &players[slot]; }
Gm_PKind Player_GetPlayerSlotType(s32 slot) { return slot < 4 ? Gm_PKind_Cpu : Gm_PKind_NA; }
s32 Player_GetStocks(int slot) { return 4 - slot; }
extern void melee_web_retail_state(void);
extern void melee_web_retail_entities(void);
extern void melee_web_retail_state_v10(void);
extern void melee_web_retail_entities_reset_v10(void);
extern void melee_web_retail_entities_index_v10(uint32_t, int);
static void emit(void) {
    printf("{"); melee_web_retail_state(); melee_web_retail_entities(); puts("}");
}
static void emit_v10(uint32_t match, int require_primaries) {
    printf("{"); melee_web_retail_state_v10();
    melee_web_retail_entities_index_v10(match, require_primaries); puts("}");
}
int main(void) {
    for (unsigned i = 0; i < 6; ++i) {
        objects[i].user_data = &fighters[i];
        fighters[i].gobj = &objects[i];
        fighters[i].player_id = i == 3 ? 2 : i == 4 ? 3 : 1;
        if (i == 0) fighters[i].player_id = 0;
        fighters[i].motion_id = 100 + i;
        fighters[i].cur_pos.x = -0.0f;
        fighters[i].cur_pos.y = 1.25f;
        fighters[i].cur_anim_frame = 2.5f;
    }
    players[0].player_entity[0] = &objects[0];
    players[1].player_entity[0] = &objects[1];
    players[1].player_entity[1] = &objects[2];
    players[2].player_entity[0] = &objects[3];
    players[3].player_entity[0] = &objects[4];
    fighters[0].kind = FTKIND_KIRBY;
    fighters[1].kind = FTKIND_POPO;
    fighters[2].kind = FTKIND_NANA;
    fighters[3].kind = FTKIND_SAMUS;
    fighters[4].kind = FTKIND_YOSHI;
    fighters[5].kind = FTKIND_NANA;
    emit();
    fighters[1].kind = FTKIND_ZELDA;
    fighters[2].kind = FTKIND_SEAK;
    emit();
    players[1].player_entity[0] = &objects[2];
    players[1].player_entity[1] = &objects[1];
    emit();
    players[1].player_entity[1] = NULL;
    emit();

    players[1].player_entity[0] = &objects[1];
    players[1].player_entity[1] = &objects[2];
    fighters[1].kind = FTKIND_POPO;
    fighters[2].kind = FTKIND_NANA;
    melee_web_retail_entities_reset_v10();
    emit_v10(0, 1);
    players[1].player_entity[1] = NULL;
    emit_v10(0, 0);
    players[1].player_entity[1] = &objects[5];
    emit_v10(0, 0);
}
'''


class RetailEntityStateTests(unittest.TestCase):
    def test_secondary_identity_transform_swap_and_absence(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        generated = ROOT / "build/gameplay-source/src"
        if not compiler or not (generated / "melee/ft/types.h").is_file():
            self.skipTest("Native compiler and generated pinned source required")
        with tempfile.TemporaryDirectory(prefix="retail-entity-state-") as directory:
            temp = Path(directory)
            harness, executable = temp / "trace.c", temp / "trace"
            harness.write_text(HARNESS)
            includes = [ROOT / "src", generated, ROOT / ".deps/aurora/include",
                        ROOT / ".deps/melee/extern/dolphin/include"]
            built = subprocess.run([compiler, "-std=c11", "-O1", "-DTARGET_PC", "-DAURORA",
                                    "-include", str(ROOT / "src/gameplay_compat.h"),
                                    *(f"-I{path}" for path in includes),
                                    str(ROOT / "src/gameplay_retail_state.c"),
                                    str(harness), "-o", str(executable)],
                                   capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        rows = [json.loads(line) for line in ran.stdout.splitlines()]
        self.assertEqual(len(rows), 7)
        for row in rows[:4]:
            self.assertEqual((row["rng"], row["match_frame"]), (123, 17))
            self.assertEqual(len(row["fighters"]), 4)
            for entity in row["fighter_entities"]:
                self.assertEqual(entity["position_bits"], ["80000000", "3fa00000", "00000000"])
                self.assertEqual(entity["animation_frame_bits"], "40200000")
                self.assertEqual(entity["input_hex"], "00" * 0x6c)
                self.assertEqual(entity["stocks"], 4 - entity["slot"])
        self.assertEqual([(e["slot"], e["entity_index"]) for e in rows[0]["fighter_entities"]],
                         [(0, 0), (1, 0), (1, 1), (2, 0), (3, 0)])
        self.assertNotEqual(rows[0]["fighter_entities"][1]["kind"],
                            rows[0]["fighter_entities"][2]["kind"])
        before, after = rows[1]["fighter_entities"], rows[2]["fighter_entities"]
        self.assertEqual(before[1]["kind"], after[2]["kind"])
        self.assertEqual(before[2]["kind"], after[1]["kind"])
        self.assertEqual(rows[2]["fighters"][1]["kind"], after[1]["kind"])
        self.assertEqual(len(rows[3]["fighter_entities"]), 4)
        v10_live, v10_absent, v10_rejoined = rows[4:]
        self.assertEqual([(entity["slot"], entity["entity_index"])
                          for entity in v10_live["fighter_entities"]],
                         [(0, 0), (1, 0), (1, 1), (2, 0), (3, 0)])
        partner0 = v10_live["fighter_entities"][2]
        self.assertEqual((partner0["kind"], partner0["generation"]),
                         (11, 0))
        self.assertFalse(any(entity["slot"] == 1 and entity["entity_index"] == 1
                             for entity in v10_absent["fighter_entities"]))
        partner2 = next(entity for entity in v10_rejoined["fighter_entities"]
                        if entity["slot"] == 1 and entity["entity_index"] == 1)
        self.assertEqual((partner2["kind"], partner2["generation"]),
                         (11, 2))


if __name__ == "__main__":
    unittest.main()
