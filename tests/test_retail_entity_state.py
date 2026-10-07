"""The production state observer retains source entity ordinals and float bits."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
# Exact four-row output captured from the pre-visitor formatter at 07bcfdf.
FIXTURE = ROOT / "tests" / "fixtures" / "retail-entity-state-before-shared-visitor.jsonl"

HARNESS = r'''
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/gm/forward.h>
#include <sysdolphin/baselib/random.h>
#include <stdio.h>
#include <string.h>
static StaticPlayer players[4];
static Fighter fighters[3];
static HSD_GObj objects[3];
static u32 seed = 123;
u32* seed_ptr = &seed;
u32 gm_GetFrameCount(void) { return 17; }
StaticPlayer* Player_GetPtrForSlot(int slot) { return &players[slot]; }
Gm_PKind Player_GetPlayerSlotType(s32 slot) { return slot < 2 ? Gm_PKind_Cpu : Gm_PKind_NA; }
s32 Player_GetStocks(int slot) { return 4 - slot; }
extern void melee_web_retail_state(void);
extern void melee_web_retail_entities(void);
static void emit(void) {
    printf("{"); melee_web_retail_state(); melee_web_retail_entities(); puts("}");
}
int main(void) {
    for (unsigned i = 0; i < 3; ++i) {
        objects[i].user_data = &fighters[i];
        fighters[i].motion_id = 100 + i;
        fighters[i].cur_pos.x = -0.0f;
        fighters[i].cur_pos.y = 1.25f;
        fighters[i].cur_anim_frame = 2.5f;
    }
    players[0].player_entity[0] = &objects[0];
    players[1].player_entity[0] = &objects[1];
    players[1].player_entity[1] = &objects[2];
    fighters[0].kind = FTKIND_KIRBY;
    fighters[1].kind = FTKIND_POPO;
    fighters[2].kind = FTKIND_NANA;
    emit();
    fighters[1].kind = FTKIND_ZELDA;
    fighters[2].kind = FTKIND_SEAK;
    emit();
    players[1].player_entity[0] = &objects[2];
    players[1].player_entity[1] = &objects[1];
    emit();
    players[1].player_entity[1] = NULL;
    emit();
}
'''


IDENTITY_HARNESS = r'''
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/gm/forward.h>
#include <sysdolphin/baselib/random.h>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static StaticPlayer players[6];
static Fighter fighters[5];
static HSD_GObj objects[5];
static u32 seed = 123;
u32* seed_ptr = &seed;
static int extra_slot = 0;
u32 gm_GetFrameCount(void) { return 17; }
StaticPlayer* Player_GetPtrForSlot(int slot) { return &players[slot]; }
Gm_PKind Player_GetPlayerSlotType(s32 slot) {
    if (slot >= 0 && slot < 4) return Gm_PKind_Cpu;
    return slot == 4 && extra_slot ? Gm_PKind_Cpu : Gm_PKind_NA;
}
s32 Player_GetStocks(int slot) { return 4 - slot; }
extern void melee_web_retail_entities_reset(void);
extern void melee_web_retail_entities_index(uint32_t);
static void emit(uint32_t match) {
    printf("{\"record\":\"identity\"");
    melee_web_retail_entities_index(match); puts("}");
}
static void initialize(void) {
    for (unsigned slot = 0; slot < 4; ++slot) {
        objects[slot].user_data = &fighters[slot];
        fighters[slot].gobj = &objects[slot];
        fighters[slot].player_id = slot;
        players[slot].player_entity[0] = &objects[slot];
    }
}
int main(int argc, char** argv) {
    struct rlimit no_core = {0, 0};
    setrlimit(RLIMIT_CORE, &no_core);
    initialize();
    const char* mode = argc > 1 ? argv[1] : "positive";
    if (strcmp(mode, "missing") == 0) players[2].player_entity[0] = NULL;
    else if (strcmp(mode, "backlink") == 0) fighters[2].gobj = &objects[3];
    else if (strcmp(mode, "player_id") == 0) fighters[2].player_id = 1;
    else if (strcmp(mode, "duplicate") == 0) players[2].player_entity[0] = &objects[1];
    else if (strcmp(mode, "extra_slot") == 0) extra_slot = 1;
    else if (strcmp(mode, "secondary") == 0)
        players[2].player_entity[1] = &objects[4];
    else if (strcmp(mode, "positive") != 0 && strcmp(mode, "replacement") != 0)
        return 2;
    melee_web_retail_entities_reset();
    if (strcmp(mode, "replacement") == 0) {
        objects[4].user_data = &fighters[4];
        fighters[4].gobj = &objects[4];
        fighters[4].player_id = 0;
        emit(0);
        players[0].player_entity[0] = &objects[4];
        emit(0);
    } else {
        emit(0);
    }
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
        self.assertEqual(ran.stdout.encode(), FIXTURE.read_bytes(),
                         "shared Fighter visitor changed the retail JSON byte stream")
        rows = [json.loads(line) for line in ran.stdout.splitlines()]
        self.assertEqual(len(rows), 4)
        for row in rows:
            self.assertEqual((row["rng"], row["match_frame"]), (123, 17))
            self.assertEqual(len(row["fighters"]), 2)
            for entity in row["fighter_entities"]:
                self.assertEqual(entity["position_bits"], ["80000000", "3fa00000", "00000000"])
                self.assertEqual(entity["animation_frame_bits"], "40200000")
                self.assertEqual(entity["input_hex"], "00" * 0x6c)
                self.assertEqual(entity["stocks"], 4 - entity["slot"])
        self.assertEqual([(e["slot"], e["entity_index"]) for e in rows[0]["fighter_entities"]],
                         [(0, 0), (1, 0), (1, 1)])
        self.assertNotEqual(rows[0]["fighter_entities"][1]["kind"],
                            rows[0]["fighter_entities"][2]["kind"])
        before, after = rows[1]["fighter_entities"], rows[2]["fighter_entities"]
        self.assertEqual(before[1]["kind"], after[2]["kind"])
        self.assertEqual(before[2]["kind"], after[1]["kind"])
        self.assertEqual(rows[2]["fighters"][1]["kind"], after[1]["kind"])
        self.assertEqual(len(rows[3]["fighter_entities"]), 2)

    def test_indexed_primary_identity_and_unsupported_invariants(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        generated = ROOT / "build/gameplay-source/src"
        if not compiler or not (generated / "melee/ft/types.h").is_file():
            self.skipTest("Native compiler and generated pinned source required")
        with tempfile.TemporaryDirectory(prefix="retail-indexed-identity-") as directory:
            temp = Path(directory)
            harness, executable = temp / "identity.c", temp / "identity"
            harness.write_text(IDENTITY_HARNESS)
            includes = [ROOT / "src", generated, ROOT / ".deps/aurora/include",
                        ROOT / ".deps/melee/extern/dolphin/include"]
            built = subprocess.run([compiler, "-std=c11", "-O1", "-DTARGET_PC", "-DAURORA",
                                    "-include", str(ROOT / "src/gameplay_compat.h"),
                                    *(f"-I{path}" for path in includes),
                                    str(ROOT / "src/gameplay_retail_state.c"),
                                    str(harness), "-o", str(executable)],
                                   capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            valid = subprocess.run([str(executable), "replacement"],
                                   capture_output=True, text=True, timeout=10)
            self.assertEqual(valid.returncode, 0, valid.stdout + valid.stderr)
            rows = [json.loads(line) for line in valid.stdout.splitlines()]
            self.assertEqual(len(rows), 2)
            for row in rows:
                self.assertEqual(len(row["fighter_entities"]), 4)
                self.assertEqual(
                    [(e["match_index"], e["slot"], e["entity_index"],
                      e["fighter_player_id"], e["fighter_gobj_linked"])
                     for e in row["fighter_entities"]],
                    [(0, slot, 0, slot, True) for slot in range(4)],
                )
            self.assertEqual([e["generation"] for e in rows[0]["fighter_entities"]],
                             [0, 0, 0, 0])
            self.assertEqual([e["generation"] for e in rows[1]["fighter_entities"]],
                             [1, 0, 0, 0])

            for unsupported in ("missing", "backlink", "player_id", "duplicate",
                                "extra_slot", "secondary"):
                rejected = subprocess.run([str(executable), unsupported],
                                          capture_output=True, text=True, timeout=10)
                self.assertNotEqual(rejected.returncode, 0,
                                    f"indexed source identity accepted {unsupported}")


if __name__ == "__main__":
    unittest.main()
