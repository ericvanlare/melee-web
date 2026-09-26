"""Reduce the actual Popo→Nana history writer, with owned PPC instruction checks."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from original_boot_context import words
from retail_allocation_profile import Dol, read_symbols

HARNESS = r'''
#include <stdio.h>
#include <string.h>
static Fighter leader, partner;
int main(void) {
    partner.cpu.x444 = &partner.cpu.xFC[5];
    partner.cpu.x448 = &partner.cpu.xFC[0];
    for (int value = -128; value <= 127; ++value) {
        float axis = value < 0 ? value / 128.0f : value / 127.0f;
        leader.input.lstick[0].x = leader.input.lstick[0].y = axis;
        leader.input.cstick[0].x = leader.input.cstick[0].y = axis;
        leader.input.held_buttons[0] = 0x100;
        ftCo_800B0918(&leader, &partner);
        struct Fighter_x1A88_xFC_t* row = partner.cpu.x444;
        if (row->lstick.x != value || row->lstick.y != value ||
            row->cstick.x != value || row->cstick.y != value || row->x0 != 0x100) {
            fprintf(stderr, "axis %d stored as %d/%d/%d/%d\n", value,
                    row->lstick.x, row->lstick.y, row->cstick.x, row->cstick.y);
            return 1;
        }
        if (partner.cpu.x444 < partner.cpu.xFC ||
            partner.cpu.x444 >= partner.cpu.xFC + ARRAY_SIZE(partner.cpu.xFC) ||
            partner.cpu.x448 < partner.cpu.xFC ||
            partner.cpu.x448 >= partner.cpu.xFC + ARRAY_SIZE(partner.cpu.xFC)) return 2;
    }
    puts("Nana history: all 256 signed stick bytes and ring wrapping passed");
}
'''


class NanaInputHistoryTests(unittest.TestCase):
    def test_actual_partner_correction_retains_double_fma_rounding(self):
        compiler = shutil.which("clang")
        source = ROOT / "build/gameplay-source/src"
        path = source / "melee/ft/kinds/ftCommon/ftCo_0A01.c"
        if not compiler or not path.is_file():
            self.skipTest("Native compiler and prepared pinned gameplay source required")
        text = path.read_text()
        begin = text.rindex("static inline", 0, text.index("inlineJ0("))
        end = text.rindex("\n", begin, text.index("ftCo_800B0CA8(Fighter*", begin))
        program = r'''
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftPopo/forward.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Fighter leader, partner;
static struct Fighter_x1A88_xFC_t history;
Fighter* ftCo_800A589C(Fighter* fp) { return &leader; }
void ftPp_SpecialLw_Enter(HSD_GObj* object) { abort(); }
''' + text[begin:end] + r'''
static float from_bits(uint32_t bits) { float f; memcpy(&f,&bits,4); return f; }
int main(void) {
    /* Reduced operands from the first original/port difference, not runtime
     * inputs. The production callback owns the computation under test. */
    partner.motion_id = 57; leader.motion_id = 14;
    partner.cpu.xFA_b7 = 1; partner.x2225_b3 = 1;
    partner.cpu.x448 = &history;
    partner.cur_pos.x = from_bits(0xc240ea7e);
    history.cur_pos.x = from_bits(0xc241a9dc);
    partner.cur_pos.y = history.cur_pos.y = from_bits(0x38d1b717);
    ftCo_800B0AF4(&partner);
    uint32_t x, y;
    memcpy(&x,&partner.cur_pos.x,4); memcpy(&y,&partner.cur_pos.y,4);
    printf("Nana correction x=%08x y=%08x\n",x,y);
    return x != 0xc240f410 || y != 0x38d1b717;
}
'''
        with tempfile.TemporaryDirectory(prefix="nana-correction-") as directory:
            temp = Path(directory)
            fixture, executable = temp / "trace.c", temp / "trace"
            fixture.write_text(program)
            includes = [ROOT / "src", source, ROOT / ".deps/aurora/include",
                        ROOT / ".deps/melee/extern/dolphin/include"]
            built = subprocess.run([
                compiler, "-std=c11", "-O1", "-ffp-contract=off", "-DTARGET_PC", "-DAURORA",
                "-include", str(ROOT / "src/gameplay_compat.h"),
                *(f"-I{path}" for path in includes), str(fixture), "-lm", "-o", str(executable),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_actual_history_writer_preserves_signed_axes(self):
        compiler = shutil.which("clang")
        source = ROOT / "build/gameplay-source/src"
        path = source / "melee/ft/kinds/ftCommon/ftCo_0A01.c"
        if not compiler or not path.is_file():
            self.skipTest("Native compiler and prepared pinned gameplay source required")
        text = path.read_text()
        # Compile the untouched production helper and complete ring writer,
        # not a duplicated implementation or a replacement gameplay routine.
        begin = text.index("static inline u8 inlineM0(float x)")
        end = text.rindex("static inline", begin, text.index("inlineJ0(", begin))
        program = '#include <melee/ft/types.h>\n' + text[begin:end] + HARNESS
        with tempfile.TemporaryDirectory(prefix="nana-history-") as directory:
            temp = Path(directory)
            fixture, executable = temp / "trace.c", temp / "trace"
            fixture.write_text(program)
            includes = [ROOT / "src", source, ROOT / ".deps/aurora/include",
                        ROOT / ".deps/melee/extern/dolphin/include"]
            built = subprocess.run([
                compiler, "-std=c11", "-O1", "-DTARGET_PC", "-DAURORA",
                "-fsanitize=float-cast-overflow", "-fno-sanitize-recover=all",
                "-include", str(ROOT / "src/gameplay_compat.h"),
                *(f"-I{path}" for path in includes), str(fixture), "-o", str(executable),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("all 256 signed stick bytes", ran.stdout)

    def test_owned_writer_uses_signed_conversion_then_low_byte_store(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned GALE01r2 DOL required (MELEE_CPU_DOL)")
        symbols = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")
        body = words(Dol(Path(configured)), symbols["ftCo_800B0918"])
        # Each axis has positive/negative scale paths, both using fctiwz,
        # stfd and lwz before stb writes the low byte to offsets 6..9.
        for start, store_offset in ((26, 6), (44, 7), (62, 8), (80, 9)):
            self.assertEqual(body[start + 1:start + 4],
                             [0xfc00001e, 0xd8010018, 0x8001001c])
            self.assertEqual(body[start + 7:start + 10],
                             [0xfc00001e, 0xd8010018, 0x8001001c])
            self.assertEqual(body[start + 11], 0x98050000 | store_offset)

    def test_owned_partner_correction_uses_double_fma_then_single_round(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned GALE01r2 DOL required (MELEE_CPU_DOL)")
        symbols = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")
        body = words(Dol(Path(configured)), symbols["ftCo_800B0AF4"])
        self.assertEqual(body[85:90], [0xfc020032, 0xc02300b0, 0xfc03007a,
                                      0xfc000018, 0xd00300b0])
        self.assertEqual(body[93:97], [0xfc020032, 0xfc03007a,
                                      0xfc000018, 0xd00300b4])


if __name__ == "__main__":
    unittest.main()
