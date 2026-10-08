"""Exercise the original up-B callback registration and damage consumer in Wasm."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from gameplay_sources import prepare_sources
from owned_test_workspace import OwnedWorkspaceTests


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class KoopaCallbackSignatures(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.compiler = ROOT / ".deps/emsdk/upstream/emscripten/emcc.py"
        if not cls.compiler.is_file():
            raise unittest.SkipTest("Pinned Wasm SDK required")
        cls.temp = cls.new_workspace(ROOT, "koopa-callback-abi-")
        cls.source = prepare_sources()

    def test_production_registration_and_damage_consumer(self):
        relative = "melee/ft/kinds/ftKoopa/ftkoopaspecialhi.c"
        current = (self.source / relative).read_text()
        original = (ROOT / ".deps/melee/src" / relative).read_text()
        setter = "static inline void ftKp_SpecialHi_Enter_inline(Fighter_GObj* gobj)"
        adapter = "static void melee_web_koopa_specialhi_callback(HSD_GObj* gobj)"
        provider = function((self.source / "melee/ft/kinds/ftKoopa/ftkoopa.c").read_text(),
                            "void ftKp_Init_80132B38(void)")
        self.assertEqual(provider, "void ftKp_Init_80132B38(void) {}")
        consumer = function((self.source / "melee/ft/ftcommon.c").read_text(),
                            "void ftCommon_8007DB58(HSD_GObj* gobj)")
        # Only the fixture instruments the otherwise empty provider, making its
        # invocation observable without changing production source.
        provider = provider.replace("{}", "{ record(3); }")
        env = dict(os.environ, EM_CONFIG=str(ROOT / ".deps/emsdk/.emscripten"),
                   EMSDK_PYTHON=sys.executable, TMPDIR=str(self.temp))
        for negative in (True, False):
            label = "negative" if negative else "positive"
            extracted = provider + "\n" + consumer + "\n"
            if not negative:
                extracted += function(current, adapter) + "\n"
            extracted += function(original if negative else current, setter) + "\n"
            (self.temp / (label + "-extracted.inc")).write_text(extracted)
            (self.temp / "koopa_callback.inc").write_text(extracted)
            target = self.temp / (label + ".js")
            command = [sys.executable, str(self.compiler), "-std=c11", "-O1",
                       "-fno-inline", "-DTARGET_PC", "-Isrc", "-I" + str(self.source),
                       "-I.deps/aurora/include", "-I" + str(self.temp),
                       "tests/gameplay_koopa_callback_test.c", "-sENVIRONMENT=node",
                       "-sEXIT_RUNTIME=1", "-o", str(target)]
            (self.temp / (label + "-command.txt")).write_text(repr(command) + "\n")
            built = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                   text=True, timeout=90)
            (self.temp / (label + "-compile.log")).write_text(built.stdout + built.stderr)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(node_runtime()), str(target)], capture_output=True,
                                 text=True, timeout=20)
            (self.temp / (label + "-run.log")).write_text(run.stdout + run.stderr)
            if negative:
                self.assertNotEqual(run.returncode, 0)
                self.assertRegex(run.stdout + run.stderr,
                                 "function signature mismatch|indirect call type mismatch")
            else:
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn("Koopa callback ABI and order passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
