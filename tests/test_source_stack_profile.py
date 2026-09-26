"""Check the runtime ABI profile against owned executable instructions.

No register captures or expected gameplay states are inputs to this check.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from original_boot_context import calls, signed16, words
from retail_allocation_profile import Dol, SOURCE_REVISION, read_symbols


class SourceStackProfileTests(unittest.TestCase):
    def test_owned_dol_frames_stack_root_and_seed(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned DOL not configured (MELEE_CPU_DOL)")
        source = Path(os.environ.get("MELEE_CPU_SOURCE_ROOT", ROOT / ".deps/melee"))
        symbols = source / "config/GALE01/symbols.txt"
        pinned = subprocess.check_output(
            ["git", "-C", str(source), "show", f"{SOURCE_REVISION}:config/GALE01/symbols.txt"])
        self.assertEqual(symbols.read_bytes(), pinned)
        dol, table = Dol(Path(configured)), read_symbols(symbols)
        profile = {name: int(value, 16) for name, value in re.findall(
            r"#define MELEE_WEB_GALE01R2_(\w+)\s+(0x[0-9A-Fa-f]+)u",
            (ROOT / "src/source_ppc_profile_gale01r2.h").read_text())}

        init = words(dol, table["__init_registers"])
        # lis r1,high; ori r1,r1,low (unsigned low half).
        self.assertEqual(init[0] & 0xffff0000, 0x3c200000)
        self.assertEqual(init[1] & 0xffff0000, 0x60210000)
        stack = ((init[0] & 0xffff) << 16) | (init[1] & 0xffff)
        self.assertEqual(stack, profile["SOURCE_STACK_TOP"])

        startup = ["__start", "main", "gm_801A4510", "runGameMode",
                   "gm_801A4014", "gm_801A4D34"]
        callbacks = [
            ("HSD_GObj_80390CFC", "GOBJ_DISPATCH"),
            ("Fighter_8006ABA0", "FIGHTER_CPU_CALLBACK"),
            ("ftCo_800B3900", "CPU_CALLBACK"),
            ("ftCo_800B2AFC", "CPU_STATE_DISPATCH"),
            ("ftCo_800B24B8", "CPU_STATE_18"),
            ("ftCo_800ADE48", "CPU_FLOOR_QUERY"),
            ("mpCheckFloor", "MP_CHECK_FLOOR"),
        ]

        def frame(name):
            # Each supported prologue has one stwu r1,-N(r1).
            updates = [signed16(w) for w in words(dol, table[name])[:8]
                       if w & 0xffff0000 == 0x94210000]
            self.assertEqual(len(updates), 1, name)
            self.assertLess(updates[0], 0, name)
            return -updates[0]

        for name in startup:
            stack -= frame(name)
        self.assertEqual(stack, profile["GM_CALLBACK_SP"])
        for name, macro in callbacks:
            self.assertEqual(frame(name), profile["FRAME_" + macro], name)
            stack -= frame(name)

        # Validate direct edges; GObj dispatch selects the callback indirectly.
        chain = startup + [name for name, _ in callbacks]
        for caller, callee in zip(chain, chain[1:]):
            if caller == "HSD_GObj_80390CFC":
                continue
            self.assertTrue(calls(dol, table[caller], table[callee]["address"])[1],
                            f"{caller} -> {callee}")
        # The audited callee passes its own local to mpLib_8004ED5C.
        body, sites = calls(dol, table["mpCheckFloor"], table["mpLib_8004ED5C"]["address"])
        self.assertTrue(sites)
        offsets = {signed16(w) for site in sites for w in body[max(0, site-16):site]
                   if w & 0xffff0000 == 0x38a10000}  # addi r5,r1,offset
        self.assertEqual(offsets, {profile["FLOOR_LOCAL_Y0"]})
        seed = int.from_bytes(dol.read(table["seed_ptr"]["address"], 4), "big")
        self.assertEqual(seed, table["seed"]["address"])
        self.assertEqual(seed, profile["SEED_WORD"])


if __name__ == "__main__":
    unittest.main()
