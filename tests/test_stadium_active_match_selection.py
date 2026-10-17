"""Actual caller witness + original RNG; no host export/MatchContext/Stage claim."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_stadium_on_init_boundary import function_body

ROOT = Path(__file__).resolve().parents[1]


class StadiumActiveMatchSelectionTests(unittest.TestCase):
    def test_copied_selection_and_actual_rng_owner_phases(self):
        compiler = shutil.which("clang++")
        random = ROOT / "build/gameplay-source/src/sysdolphin/baselib/random.c"
        if not compiler or not random.is_file():
            self.skipTest("Prepared original RNG and native C++ compiler required")
        source = (ROOT / "tests/native_menu_host_trace.cpp").read_text()
        program = r'''
#include "gameplay_menu_host.h"
#include <cassert>
#include <string_view>
extern "C" {
#include <sysdolphin/baselib/random.h>
}
'''
        for name in ["StadiumSelectionRngWitness", "StadiumSelectionDifference"]:
            start = source.index("struct " + name + " {")
            program += source[start:source.index("};", start) + 2] + "\n"
        signatures = [
            "bool stadium_rng_witness_matches(const StadiumSelectionRngWitness& witness, const MeleeWebMenuMatchSelection& selected)",
            "StadiumSelectionDifference stadium_selection_difference(const MeleeWebMenuMatchSelection& observed, const MeleeWebMenuMatchSelection& expected, uint32_t expected_live_seed)",
            "bool stadium_active_match_selection_witness_matches(const MeleeWebMenuMatchSelection& copied_selection, const MeleeWebMenuMatchSelection& current_selection, const StadiumSelectionRngWitness& active_match_rng)",
        ]
        for signature in signatures:
            name = signature.split("(")[0].split()[-1]
            program += signature + "{" + function_body(source, name + "(") + "}\n"
        program += r'''
int main() {
 u32* prior_seed_owner=seed_ptr;
 for(unsigned lifetime=0;lifetime<2;++lifetime) {
  // Explicit synthetic payload/seed storage. This exercises actual caller
  // comparisons and linked original HSD RNG, never a successful host export.
  uint32_t menu_seed=0x12345678u+lifetime,match_seed=menu_seed;
  seed_ptr=&menu_seed;
  MeleeWebMenuMatchSelection selected{}; selected.random_seed=menu_seed;
  const auto copied=selected;
  StadiumSelectionRngWitness match_rng{&match_seed,match_seed,match_seed};
  assert(!stadium_active_match_selection_witness_matches(copied,selected,match_rng));
  seed_ptr=&match_seed;
  assert(stadium_active_match_selection_witness_matches(copied,selected,match_rng));
  assert(!match_rng.source_return_captured);
  (void)HSD_Randi(13);
  assert(!stadium_active_match_selection_witness_matches(copied,selected,match_rng));
  match_rng.expected_live=match_seed; match_rng.source_return_captured=true;
  assert(stadium_active_match_selection_witness_matches(copied,selected,match_rng));
  assert(copied.random_seed==menu_seed && selected.random_seed==menu_seed);
  uint32_t foreign_seed=match_seed; seed_ptr=&foreign_seed;
  assert(!stadium_active_match_selection_witness_matches(copied,selected,match_rng));
  seed_ptr=&match_seed;
  auto changed=selected; reinterpret_cast<unsigned char*>(&changed.start)[0]^=1;
  assert(!stadium_active_match_selection_witness_matches(copied,changed,match_rng));
  changed=selected; reinterpret_cast<unsigned char*>(changed.players)[0]^=1;
  assert(!stadium_active_match_selection_witness_matches(copied,changed,match_rng));
#define REFUSE(field) changed=selected; changed.field^=1; \
  assert(!stadium_active_match_selection_witness_matches(copied,changed,match_rng)); \
  assert(!stadium_active_match_selection_witness_matches(changed,selected,match_rng));
  REFUSE(player_count); REFUSE(random_seed); REFUSE(hud_layout);
  REFUSE(unlocked_characters); REFUSE(unlocked_stages); REFUSE(save_profile_present);
  REFUSE(opening_demo); REFUSE(sudden_death);
#undef REFUSE
  (void)HSD_Randi(13);
  assert(!stadium_active_match_selection_witness_matches(copied,selected,match_rng));
  seed_ptr=&menu_seed;
  assert(!stadium_active_match_selection_witness_matches(copied,selected,match_rng));
  assert(menu_seed==copied.random_seed);
 }
 seed_ptr=prior_seed_owner;
}
'''
        scratch = Path(tempfile.mkdtemp(prefix="stadium-active-selection-", dir=ROOT / "work"))
        failed = True
        try:
            cpp, executable = scratch / "control.cpp", scratch / "control"
            cpp.write_text(program)
            rng = scratch / "original-rng.cpp"
            rng.write_text('#include "gameplay_compat.h"\nextern "C" {\n#include "' +
                           str(random) + '"\n}\n')
            command = [compiler, "-std=c++20", "-DTARGET_PC", "-fsanitize=address,undefined",
                       "-fno-omit-frame-pointer", "-I" + str(ROOT / "src"),
                       "-I" + str(ROOT / "build/gameplay-source/src"),
                       "-I" + str(ROOT / ".deps/aurora/include"),
                       str(cpp), str(rng), "-o", str(executable)]
            (scratch / "command.txt").write_text(" ".join(command) + "\n")
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            (scratch / "compile.stdout").write_text(result.stdout)
            (scratch / "compile.stderr").write_text(result.stderr)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True)
            (scratch / "run.stdout").write_text(result.stdout)
            (scratch / "run.stderr").write_text(result.stderr)
            self.assertEqual(result.returncode, 0, result.stderr)
            failed = False
        finally:
            if failed:
                print("Retained active-selection failure:", scratch)
            else:
                shutil.rmtree(scratch)
