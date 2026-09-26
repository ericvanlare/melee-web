"""Executed Nana r5 producers from actual source, plus the owned DOL profile."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from original_boot_context import calls, words
from retail_allocation_profile import Dol, read_symbols


class NanaSourceCarryTests(unittest.TestCase):
    def test_actual_follower_entry_rejects_bypassed_producers(self):
        compiler = shutil.which("clang")
        source = ROOT / "build/gameplay-source/src"
        path = source / "melee/ft/kinds/ftCommon/ftCo_0A01.c"
        if not compiler or not path.is_file():
            self.skipTest("Native compiler and prepared pinned source required")
        text = path.read_text()
        begin = text.index("static inline melee_source_bool isPopoUpB(")
        end = text.index("\n}\n", text.index("\nvoid ftCo_800B101C(", begin)) + 3
        program = r'''
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftPopo/forward.h>
#include <stdlib.h>
#include <string.h>
static Fighter nana, popo;
static int known, found, no_partner_calls, invalidations, frames;
void melee_web_source_frame_enter(MeleeWebSourceFrameGuard* guard, MeleeWebSourceFrameId id) {
    if(id!=MELEE_WEB_SOURCE_FRAME_CPU_NANA_FOLLOW) abort(); frames++;
}
void melee_web_source_frame_leave(MeleeWebSourceFrameGuard* guard) { frames--; }
void melee_web_source_context_invalidate_r5_at(const char* boundary, uint32_t line) {
    known=0; invalidations++;
}
Fighter* ftCo_800A589C(Fighter* fp) { return found ? &popo : NULL; }
void ftCo_800B0918(Fighter* a,Fighter* b) {}
void ftCo_800A7AAC(Fighter* fp) {}
void ftCo_800AEFB8(Fighter* fp) { no_partner_calls++; }
void ftCo_800B0760(Fighter* fp) { abort(); }
melee_source_bool ftCo_800B0CA8(Fighter* a,Fighter* b) { abort(); }
melee_source_bool ftCo_800B0E98(Fighter* a,Fighter* b) { abort(); }
''' + text[begin:end] + r'''
int main(void) {
    /* These callback doubles only isolate which production branch executes.
     * The separate source-context trace tests actual register/lease ownership. */
    for(int branch=0;branch<3;++branch) {
        memset(&nana,0,sizeof(nana)); memset(&popo,0,sizeof(popo));
        known=1; invalidations=frames=no_partner_calls=0; found=branch!=2;
        nana.cpu.xFA_b7=1;
        nana.motion_id=branch==0 ? ftPp_MS_SpecialHi_0 : ftCo_MS_Wait;
        if(branch==1) popo.cur_pos.x=26.0f;
        ftCo_800B101C(&nana);
        if(known || invalidations!=1 || frames || no_partner_calls!=(branch==2)) return 1;
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="nana-carry-bypass-") as directory:
            fixture, executable = Path(directory)/"trace.c", Path(directory)/"trace"
            fixture.write_text(program)
            includes = [ROOT / "src", source, ROOT / ".deps/aurora/include",
                        ROOT / ".deps/melee/extern/dolphin/include"]
            built = subprocess.run([
                compiler, "-std=c11", "-O1", "-DTARGET_PC", "-DAURORA",
                "-include", str(ROOT / "src/gameplay_compat.h"),
                *(f"-I{p}" for p in includes), str(fixture), "-o", str(executable),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            self.assertEqual(subprocess.run([str(executable)], timeout=10).returncode, 0)

    def test_actual_leaf_tracks_executed_definitions_not_return_value(self):
        compiler = shutil.which("clang")
        source = ROOT / "build/gameplay-source/src"
        path = source / "melee/ft/kinds/ftCommon/ftCo_0A01.c"
        if not compiler or not path.is_file():
            self.skipTest("Native compiler and prepared pinned source required")
        text = path.read_text()
        def function(name):
            begin = text.index(f"\nmelee_source_bool {name}(Fighter*") + 1
            return text[begin:text.index("\n}\n", begin) + 3]
        leaf = function("ftCo_800A3200") + function("ftCo_800B0CA8")
        program = r'''
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/it/types.h>
#include <melee/it/inlines.h>
#include <stdio.h>
#include <string.h>
static Fighter nana, popo;
static HSD_GObj item_gobj;
static Item item;
static int known, kind, definitions;
static uint32_t word;
int melee_web_source_context_publish_nana_motion_r5(void* n, void* p, int32_t m) {
    if(n!=&nana || p!=&popo || m!=popo.motion_id) return 0;
    known=1; kind=1; word=(uint32_t)m; definitions++; return 1;
}
int melee_web_source_context_publish_nana_empty_item_r5(void* n) {
    if(n!=&nana || nana.item_gobj) return 0;
    known=1; kind=2; word=0; definitions++; return 1;
}
void melee_web_source_context_invalidate_r5_at(const char* boundary, uint32_t line) {
    known=0; kind=0; word=0; definitions++;
}
''' + leaf + r'''
int main(void) {
    const int early[] = {252,253,224,227,266,267,268,269,270,
                         245,246,288,289,12,13,305,306,145,146,147};
    item_gobj.user_data=&item;
    /* All motion branches, both return results, and null/non-null held items.
     * A held item must not clobber a motion retained by an earlier return. */
    for(int motion=0; motion<400; ++motion) {
        int returns_early=0;
        for(unsigned i=0;i<sizeof(early)/sizeof(early[0]);++i)
            returns_early |= motion==early[i];
        for(int held=0;held<3;++held) for(int follow=0;follow<2;++follow) {
            memset(&nana,0,sizeof(nana)); memset(&popo,0,sizeof(popo));
            nana.item_gobj=held ? &item_gobj : NULL;
            item.kind=held==1 ? It_Kind_Box : It_Kind_Bat;
            popo.motion_id=motion; nana.x2225_b3=follow;
            known=kind=definitions=0; word=0xdeadbeef;
            int result=ftCo_800B0CA8(&nana,&popo);
            if(returns_early) {
                if(result || !known || kind!=1 || word!=(uint32_t)motion ||
                   definitions!=1) return 1;
            } else if(!held) {
                if(result!=follow || !known || kind!=2 || word || definitions!=2)
                    return 2;
            } else if(known || definitions!=2 || (held==1 && result)) return 3;
        }
    }
    puts("Nana executed r5: early motion, null item, rejected Item owner passed");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="nana-carry-") as directory:
            fixture, executable = Path(directory)/"trace.c", Path(directory)/"trace"
            fixture.write_text(program)
            includes = [ROOT / "src", source, ROOT / ".deps/aurora/include",
                        ROOT / ".deps/melee/extern/dolphin/include"]
            built = subprocess.run([
                compiler, "-std=c11", "-O1", "-DTARGET_PC", "-DAURORA",
                "-include", str(ROOT / "src/gameplay_compat.h"),
                *(f"-I{p}" for p in includes), str(fixture), "-o", str(executable),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("rejected Item owner passed", ran.stdout)
            # Negative control: keep the same source branches but omit only
            # the new observation hooks. No supported definition reaches us.
            unobserved = program.replace(leaf, "\n".join((
                "#define melee_web_source_context_publish_nana_motion_r5(...) 0",
                "#define melee_web_source_context_publish_nana_empty_item_r5(...) 0",
                "#define melee_web_source_context_invalidate_r5_at(...) ((void)0)",
                leaf)))
            fixture.write_text(unobserved)
            built = subprocess.run([
                compiler, "-std=c11", "-O1", "-DTARGET_PC", "-DAURORA",
                "-include", str(ROOT / "src/gameplay_compat.h"),
                *(f"-I{p}" for p in includes), str(fixture), "-o", str(executable),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            self.assertNotEqual(subprocess.run([str(executable)], timeout=10).returncode, 0)

    def test_owned_definitions_frames_and_preserving_returns(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned GALE01r2 DOL required (MELEE_CPU_DOL)")
        table = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")
        dol = Dol(Path(configured))
        # Bind the full manually audited bodies, not merely the producer words:
        # B0CA8 is a leaf with precisely three r5 writes; B0E98 and the two
        # returning B101C paths preserve r5 after this producer. This is an
        # executable profile check, not a register/captured-answer fixture.
        audited = {
            "ftCo_800B0CA8": "c7c282dcc1bb7dc1d8c90d625266045b36b574811a63b5079e0a0099e4cb9e22",
            "ftCo_800B0E98": "c5047a6422dec1be2f9ffdb664f281b3f7fd6d816de5d338ca6a240ff48685cb",
            "ftCo_800B101C": "d1fd95e661c4dc10f6773851649018b10e6ea0c43d12e91b4930107f617e5948",
            "ftCo_800B2AFC": "99c46f59be413aa0b790ae16e714b2f1417197257a8965597c395e5675352ff5",
            "ftCo_800B3900": "05db8e10f3b2fb1207851dedde6c0e09289ae52af50a3ec9a1808893d05eef4a",
            "ftCo_800B2790": "fb61bcefbebed7035904ebcd6f952488ab7618cfae841e463fdbdd939e07a47b",
            "ftCo_800ADC28": "e5ad7ad1f649f6b53fbd38f910a09a4c722fff6fe1f52cbd5f4b402f0df848ed",
            "ftCo_800AC5A0": "0533db7c454be365ac80ac878c985be255ec37e2227ceb7a2de5982229a4a607",
        }
        for name, digest in audited.items():
            symbol = table[name]
            self.assertEqual(hashlib.sha256(dol.read(symbol["address"], symbol["size"])).hexdigest(), digest)
        leaf = words(dol, table["ftCo_800B0CA8"])
        self.assertEqual(leaf[0], 0x80a40010)  # lwz r5,motion(r4)
        self.assertEqual(leaf[(0x800b0dac-0x800b0ca8)//4], 0x80a31974)
        self.assertEqual(leaf[(0x800b0db8-0x800b0ca8)//4], 0x80a5002c)
        profile = {name: int(value, 16) for name, value in re.findall(
            r"#define MELEE_WEB_GALE01R2_(\w+)\s+(0x[0-9A-Fa-f]+)u",
            (ROOT / "src/source_ppc_profile_gale01r2.h").read_text())}
        for name, macro in (("ftCo_800B101C", "CPU_NANA_FOLLOW"),
                            ("ftCo_800B0E98", "CPU_NANA_FOLLOW_CHECK")):
            self.assertEqual(words(dol, table[name])[2],
                             0x94210000 | ((-profile["FRAME_"+macro]) & 0xffff))
        for caller, callee, sites in (
                ("ftCo_800B101C", "ftCo_800B0CA8", [0x800b1198]),
                ("ftCo_800B101C", "ftCo_800B0E98", [0x800b11f8]),
                ("ftCo_800B0E98", "ftCo_800B0CA8", [0x800b0eb4])):
            _, offsets = calls(dol, table[caller], table[callee]["address"])
            self.assertEqual([table[caller]["address"]+4*i for i in offsets], sites)
        _, dispatch = calls(dol, table["ftCo_800B2AFC"], table["ftCo_800B101C"]["address"])
        self.assertEqual([table["ftCo_800B2AFC"]["address"]+4*i for i in dispatch], [0x800b2f60])
        self.assertEqual(dol.read(0x800b2f64, 4).hex(), "48000430")  # b epilogue
        # B2790 resets the command list before charge-cancel/dispatch. That
        # helper writes only r0/r3 and memory, so it does not clobber Nana's r5.
        self.assertEqual(words(dol, table["ftCo_800B462C"]),
                         [0x38631a88, 0x38030454, 0x90030554, 0x4e800020])


if __name__ == "__main__":
    unittest.main()
