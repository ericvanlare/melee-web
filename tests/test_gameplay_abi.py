"""Execute actual gameplay layouts on Wasm and compare PowerPC compiler evidence."""
import ast
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_sources import prepare_sources


class GameplayAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sdk = ROOT / ".deps/emsdk"
        cls.emscripten = cls.sdk / "upstream/emscripten"
        if not (cls.emscripten / "emcc.py").is_file():
            raise unittest.SkipTest("Project-local SDK unavailable; run scripts/bootstrap.py")
        cls.env = dict(os.environ, EMSDK=str(cls.sdk), EM_CONFIG=str(cls.sdk / ".emscripten"),
                       EM_CACHE=str(cls.emscripten / "cache"), EMSDK_PYTHON=sys.executable)
        node_setting = next(ast.literal_eval(statement.value)
            for statement in ast.parse((cls.sdk / ".emscripten").read_text()).body
            if isinstance(statement, ast.Assign) and any(isinstance(target, ast.Name)
                and target.id == "NODE_JS" for target in statement.targets))
        cls.node = Path(node_setting.replace("$CFGDIR", str(cls.sdk))).resolve()
        if not cls.node.is_relative_to(cls.sdk.resolve()):
            raise RuntimeError("Expected the pinned project-local Node runtime")
        cls.original_header = ROOT / ".deps/melee/src/melee/ft/types.h"
        cls.original_digest = hashlib.sha256(cls.original_header.read_bytes()).digest()
        cls.source = prepare_sources(ROOT)
        cls.temp = tempfile.TemporaryDirectory(prefix="melee gameplay ABI ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        table = (ROOT / ".deps/melee/src/melee/ft/ftmotionstates.c").read_text()
        wait = re.search(r"// ftCo_MS_Wait = 14\s+([^,]+),\s+([^,]+),\s+([^,]+),", table)
        if wait is None:
            raise AssertionError("Original Wait initializer not found")
        # Keep original enum expressions and numeric initializer; callbacks are
        # irrelevant to layout and deliberately excluded from this compile probe.
        (cls.directory / "motion_wait_fixture.h").write_text(
            '#include <melee/ft/kinds/ftCommon/forward.h>\n'
            + 'static const MotionState motion_wait = {'
            + ','.join(wait.groups()) + ',0,0,0,0,0};\n')

    def run_command(self, command):
        result = subprocess.run(command, cwd=self.directory, env=self.env, capture_output=True,
                                text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def common(self, source):
        return ["-std=c11", "-DTARGET_PC", "-ffp-contract=off", "-I", str(ROOT / "src"),
                "-I", str(source), "-I", str(ROOT / ".deps/aurora/include")]

    def test_actual_patched_wasm_structs_and_every_animation_flag_alias(self):
        output = self.directory / "gameplay_abi.js"
        self.run_command([sys.executable, str(self.emscripten / "emcc.py"),
            *self.common(self.source), "-O1", "-Wall", "-Wextra", "-Werror",
            str(ROOT / "src/gameplay_abi.c"), str(ROOT / "tests/gameplay_abi_trace.c"),
            "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(output)])
        result = self.run_command([str(self.node), str(output)])
        self.assertIn("canonical animation flag aliases: passed", result.stdout)
        self.assertIn("native command overlays unsupported", result.stdout)

    def compiler_values(self, label, target, source):
        output = self.directory / (label + ".ll")
        self.run_command([str(self.sdk / "upstream/bin/clang"), "--target=" + target,
            "-ffreestanding", "-isystem", str(self.emscripten / "cache/sysroot/include"),
            *self.common(source), "-O2", "-S", "-emit-llvm", "-I", str(self.directory),
            str(ROOT / "tests/gameplay_abi_reference.c"), "-o", str(output)])
        values = {}
        for name, body in re.findall(r"define[^\n]* @([a-z_0-9]+)\([^\n]*\)[^{]*\{(.*?)\n\}",
                                     output.read_text(), re.S):
            value = re.search(r"ret i32 (-?\d+)", body)
            self.assertIsNotNone(value, "Expected a compiler constant for " + name)
            values[name] = int(value.group(1)) & 0xffffffff
        self.assertEqual(len(values), 76)
        return values

    def test_original_powerpc_and_patched_wasm_agree_with_explicit_negative_control(self):
        original = ROOT / ".deps/melee/src"
        ppc = self.compiler_values("original_ppc", "powerpc-unknown-eabi", original)
        wasm = self.compiler_values("patched_wasm", "wasm32-unknown-emscripten", self.source)
        broken = self.compiler_values("unpatched_wasm", "wasm32-unknown-emscripten", original)
        expected = {"flag_b0":0x80000000, "flag_loop":0x40000000, "flag_b2":0x20000000,
                    "flag_b3":0x10000000, "flag_b4":0x08000000, "flag_b5":0x04000000,
                    "flag_b6":0x02000000, "flag_b7":0x01000000, "flag_pad":0xffc00000,
                    "flag_mask":0x003ffe00, "flag_pad2":0x1c0, "flag_kind":63,
                    "flag_nested0":0xfe00, "flag_nested7":0x1c0,
                    "fighter_size":0x23ec, "command_size":0x24, "command_tail_offset":0x1c,
                    "gobj_size":0x38}
        expected.update({f"stage_flag_{bit}": 1 << (31-bit) for bit in range(8)})
        expected.update(stage_callbacks_size=20, stage_flags_offset=16)
        expected.update(motion_move_id=0xff000000, motion_xa=0xff00, motion_xb=255,
                        motion_size=32, motion_word_offset=8, motion_callback_offset=12,
                        motion_wait_b0=1, motion_wait_b1=1,
                        motion_wait_default=ppc["motion_wait_default"],
                        motion_wait_move=ppc["motion_wait_default"],
                        motion_wait_word=(ppc["motion_wait_default"] << 24) | (1 << 22) | (1 << 23))
        expected.update({f"motion_b{bit}": 1 << (23-bit) for bit in range(8)})
        expected.update({f"byte_flag_{bit}": 1 << (7-bit) for bit in range(8)})
        expected.update({f"byte_clear_{bit}": 255 ^ (1 << (7-bit)) for bit in range(8)})
        expected.update(color_opcode=0xfc000000,color_timer=0x03ffffff,
                        color_rot1_x=0x03ffe000,color_rot1_yz=0x1fff,
                        color_rot2_x=0x00fff000,color_rot2_yz=0xfff,color_enable=0x02000000)
        for name in ("color_opcode","color_timer","color_rot1_x","color_rot2_x","color_enable"):
            self.assertNotEqual(broken[name],ppc[name])
        expected.update(byte_flag_size=1,fighter_visible_on=1,fighter_visible_off=0)
        for bit in range(8):
            self.assertNotEqual(broken[f"byte_flag_{bit}"],ppc[f"byte_flag_{bit}"])
            self.assertNotEqual(broken[f"byte_clear_{bit}"],ppc[f"byte_clear_{bit}"])
        self.assertNotEqual(broken["fighter_visible_on"],ppc["fighter_visible_on"])
        self.assertNotEqual(broken["fighter_visible_off"],ppc["fighter_visible_off"])
        for key, value in expected.items():
            with self.subTest(field=key):
                self.assertEqual(ppc[key], value)
                self.assertEqual(wasm[key], value)
        for key in ("motion_move_id", "motion_b0", "motion_b1", "motion_wait_move",
                    "motion_wait_b0", "motion_wait_b1"):
            self.assertNotEqual(broken[key], ppc[key])
        self.assertNotEqual(broken["flag_loop"], ppc["flag_loop"])
        self.assertNotEqual(broken["flag_mask"], ppc["flag_mask"])
        self.assertNotEqual(broken["flag_kind"], ppc["flag_kind"])
        for bit in range(8):
            self.assertNotEqual(broken[f"stage_flag_{bit}"], ppc[f"stage_flag_{bit}"])
        self.assertEqual((ppc["nested_offset"], wasm["nested_offset"]), (0x596, 0x594))
        self.assertEqual((ppc["throw_flag_word"], wasm["throw_flag_word"]), (0x80000000, 1))
        self.assertEqual(ppc["command_word"], 0x0c000007)
        self.assertNotEqual(wasm["command_word"], ppc["command_word"])

    def test_original_fighter_and_ftanim_compile_and_dependency_stays_pristine(self):
        for name in ("fighter", "ftanim", "ftwaitanim"):
            with self.subTest(source=name):
                self.run_command([sys.executable, str(self.emscripten / "emcc.py"),
                    *self.common(self.source), "-include", str(ROOT / "src/gameplay_compat.h"),
                    "-c", str(self.source / f"melee/ft/{name}.c"),
                    "-o", str(self.directory / (name + ".o"))])
        self.assertEqual(hashlib.sha256(self.original_header.read_bytes()).digest(), self.original_digest)
        # Preparation composes the reviewed adapter patch with the source bool
        # ABI transform. Check both forward patches against pristine upstream,
        # and reverse the exact composed patch that owns the generated tree.
        # These checks do not mutate either checkout.
        reviewed = ROOT / "patches/melee-gameplay.patch"
        composed = self.source.parent / ".git/melee-web-gameplay.patch"
        self.assertTrue(composed.is_file(), "Prepared source ownership patch is missing")
        for source, patch, reverse in [
                (ROOT / ".deps/melee", reviewed, False),
                (ROOT / ".deps/melee", composed, False),
                (self.source.parent, composed, True)]:
            result = subprocess.run(["git", "apply", "--check", *( ["--reverse"] if reverse else []),
                                     str(patch)], cwd=source, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
