"""Focused checks for the bounded Release public runtime packaging target."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("public_build", ROOT / "scripts/build.py")
public_build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(public_build)


def _uleb(value):
    encoded = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        encoded.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(encoded)


def _wasm_with_exports(names):
    payload = bytearray(_uleb(len(names)))
    for index, name in enumerate(names):
        encoded = name.encode("utf-8")
        payload.extend(_uleb(len(encoded)))
        payload.extend(encoded)
        payload.extend((0,))  # function export
        payload.extend(_uleb(index))
    return b"\0asm\1\0\0\0" + b"\7" + _uleb(len(payload)) + payload


class PublicRuntimeBuildTests(unittest.TestCase):
    def test_release_target_has_a_small_explicit_api(self):
        cmake = (ROOT / "cmake/FighterRuntime.cmake").read_text(encoding="utf-8")
        public = cmake.split("# The public player", 1)[1].split(
            "# Shared typed scene/model tables", 1
        )[0]
        self.assertIn("if(CMAKE_BUILD_TYPE STREQUAL \"Release\")", public)
        self.assertIn("add_custom_target(runtime-public DEPENDS gameplay_public)", public)
        self.assertNotIn("--profiling-funcs", public)
        self.assertIn("_melee_web_native_menu_message", public)
        self.assertIn("_melee_web_input_set_keyboard_layout", public)
        for forbidden in public_build.PUBLIC_RUNTIME_FORBIDDEN_EXPORTS:
            self.assertNotIn(forbidden, public)

    def test_development_target_keeps_instrumentation(self):
        cmake = (ROOT / "cmake/FighterRuntime.cmake").read_text(encoding="utf-8")
        development = cmake.split("add_executable(gameplay_menu_browser", 1)[1].split(
            "# The public player", 1
        )[0]
        self.assertIn("--profiling-funcs", development)
        self.assertIn("_melee_web_native_menu_replay", development)
        self.assertIn("_melee_web_native_menu_diagnostics", development)

    def test_build_cli_rejects_non_release_public_runtime(self):
        with self.assertRaisesRegex(ValueError, "Release-only"):
            public_build.build(1, root=ROOT, target="runtime-public", configuration="RelWithDebInfo")

    def test_provenance_covers_reviewed_patch_and_build_pipeline(self):
        required = {
            "patches/melee-gameplay.patch",
            "patches/aurora-browser.patch",
            "scripts/bootstrap.py",
            "scripts/build.py",
            "scripts/gameplay_bool.py",
            "scripts/gameplay_sources.py",
            "scripts/generate_common_schema.py",
            "scripts/generate_fighter_registry.py",
            "scripts/materialize_pipeline_cache.py",
        }
        self.assertTrue(required.issubset(public_build.PUBLIC_RUNTIME_SOURCE_FILES))

    def test_wasm_export_reader_reports_actual_exports(self):
        with tempfile.TemporaryDirectory() as directory:
            wasm = Path(directory) / "runtime.wasm"
            wasm.write_bytes(_wasm_with_exports(["_main", "_malloc", "memory_probe"]))
            exports = public_build._wasm_exports(wasm)
        self.assertEqual(
            [item["name"] for item in exports],
            ["_main", "_malloc", "memory_probe"],
        )
        self.assertTrue(all(item["kind"] == 0 for item in exports))


if __name__ == "__main__":
    unittest.main()
