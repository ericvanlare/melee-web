"""Compile original Player routines against isolated boundary callbacks.

This checks mapping ownership and callback selection, not gameplay equivalence.
The real-asset content trace separately exercises Nana death and rejoin.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <melee/pl/player.h>
#include <melee/pl/types.h>
#include <melee/ft/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static HSD_GObj objects[2];
static int rebirths[2], status_updates;
void __assert(char* file, u32 line, char* text) { abort(); }
void OSReport(const char* format, ...) {}
void ftCo_800D4FF4(HSD_GObj* object) { rebirths[object == &objects[1]]++; }
bool ftLib_8008701C(HSD_GObj* object) { return object == &objects[1]; }
bool ftLib_800873CC(HSD_GObj* object) { return true; }
void ifStatus_802F6E1C(int slot) { status_updates++; }
void ftDemo_SetArchiveData(int kind, HSD_Archive* archive, int arg) {}
static void require(int ok, const char* message) {
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}
int main(void) {
    StaticPlayer* player = Player_GetPtrForSlot(0);
    memset(player, 0, sizeof(*player));
    player->player_character = CKIND_POPONANA;
    player->player_entity[0] = &objects[0];
    player->player_entity[1] = &objects[1];
    player->transformed[0] = 0; player->transformed[1] = 1;
    player->flags.b2 = true;
    player->slot_type = Gm_PKind_Cpu;
    require(Player_80032610(0, 0) == FTKIND_POPO, "Popo identity did not use mapping owner");
    require(Player_80032610(0, 1) == FTKIND_NANA, "Nana identity did not use mapping owner");
    require(Player_8003248C(0, true) == Gm_PKind_Cpu, "Nana controller type incorrect");
    Player_80032070(0, false);
    require(rebirths[0] == 1 && rebirths[1] == 1 && status_updates == 1,
            "Dead Nana must rejoin Popo at stock respawn");
    player->falls[0] = 2; player->falls[1] = 3;
    require(Player_GetFalls(0) == 2, "Nana deaths must not count as Popo stocks");
    player->player_character = CKIND_ZELDA;
    require(Player_80032610(0, 0) == FTKIND_ZELDA &&
            Player_80032610(0, 1) == FTKIND_SEAK, "Zelda form mapping incorrect");
    require(Player_GetFalls(0) == 5, "Transformed fighter falls must be combined");
    Player_80032070(0, false);
    require(rebirths[0] == 2 && rebirths[1] == 1,
            "Dormant Sheik must not respawn as Nana");
    player->player_character = CKIND_MARIO;
    player->flags.b2 = false;
    require(Player_80032610(0, 0) == FTKIND_MARIO && Player_80032610(0, 1) == -1,
            "Single-fighter control mapping incorrect");
    Player_80032070(0, false);
    require(rebirths[0] == 3 && rebirths[1] == 1, "Single-fighter control respawn incorrect");
    puts("Player mapping: Nana rejoin, transformed identity/falls, single-fighter control passed");
}
'''


class PlayerMappingTests(unittest.TestCase):
    def test_original_mapping_consumers(self):
        compiler = shutil.which("clang")
        source = ROOT / "build/gameplay-source/src"
        if not compiler or not (source / "melee/pl/player.c").is_file():
            self.skipTest("Native compiler and prepared pinned gameplay source required")
        with tempfile.TemporaryDirectory(prefix="player-mapping-") as directory:
            temp = Path(directory)
            harness, executable = temp / "trace.c", temp / "trace"
            harness.write_text(HARNESS)
            includes = [ROOT / "src", source, ROOT / ".deps/aurora/include",
                        ROOT / ".deps/melee/extern/dolphin/include"]
            built = subprocess.run([
                compiler, "-std=c11", "-O1", "-DTARGET_PC", "-DAURORA",
                "-DMELEE_WEB_GAMEPLAY", "-ffunction-sections", "-fdata-sections",
                "-include", str(ROOT / "src/gameplay_compat.h"),
                *(f"-I{path}" for path in includes),
                str(source / "melee/pl/player.c"), str(harness),
                "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                "-o", str(executable),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("Player mapping: Nana rejoin", ran.stdout)


if __name__ == "__main__":
    unittest.main()
