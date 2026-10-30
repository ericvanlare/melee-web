import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from scripts import source_state_manifest as manifest


class SourceStateManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="source-state-manifest-")
        self.root = Path(self.temporary.name) / "repo"
        self.root.mkdir()
        self.source_root = self.root / "build/gameplay-source"
        self.source_root.mkdir(parents=True)
        fixture_root = Path(__file__).parent / "fixtures/source_state_manifest"
        sources = {
            self.source_root / "melee/fighter.c": fixture_root / "source_root/melee/fighter.c",
            self.source_root / "extern/dolphin/src/dolphin/ax/AXAlloc.c": None,
            self.source_root / "sysdolphin/baselib/hsd.c": None,
            self.source_root / "sdk/OSAlloc.cpp": None,
            self.root / "src/project_owner.c": fixture_root / "project_owner.c",
            self.root / "renderer/renderer.c": fixture_root / "foreign/renderer.c",
        }
        for destination, source in sources.items():
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(source.read_text() if source else "int fixture_source;\n", encoding="utf-8")
        subprocess.run(["git", "init", "--quiet"], cwd=self.source_root, check=True)
        subprocess.run(["git", "config", "user.name", "Manifest Test"], cwd=self.source_root, check=True)
        subprocess.run(["git", "config", "user.email", "manifest-test@example.invalid"], cwd=self.source_root, check=True)
        subprocess.run(["git", "add", "--all"], cwd=self.source_root, check=True)
        subprocess.run(["git", "commit", "--quiet", "-m", "fixture"], cwd=self.source_root, check=True)
        self.melee_commit = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=self.source_root, text=True
        ).strip()
        (self.source_root / ".git/melee-web-gameplay.patch").write_text("fixture patch\n", encoding="utf-8")

        (self.root / "dependencies.lock.json").write_text(json.dumps({
            "repositories": {"melee": {"commit": self.melee_commit}}
        }), encoding="utf-8")
        for relative in (
            "CMakeLists.txt",
            "cmake/FighterRuntime.cmake",
            "scripts/source_state_manifest.py",
            "patches/melee-gameplay.patch",
            "src/slippi_profile/patches/melee-profile-hooks.patch",
        ):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture identity input\n", encoding="utf-8")

        self.build = self.root / "build"
        self.build.mkdir(exist_ok=True)
        self.cache = self.build / "CMakeCache.txt"
        self.cache.write_text("CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8")
        self.build_graph = self.build / "build.ninja"
        self.build_graph.write_text("ninja_dyndep_version = 1\n", encoding="utf-8")
        self.version = self.root / ".deps/emsdk/emscripten-version.txt"
        self.version.parent.mkdir(parents=True, exist_ok=True)
        self.version.write_text("6.0.9\n", encoding="utf-8")
        self.wasm = self.build / "fixture.wasm"
        self.wasm.write_bytes(b"fixture wasm bytes")
        self.link_map = self.build / "fixture.map"
        self.compile_commands = self.build / "compile_commands.json"
        self.producer_sources = self.build / "producer-sources.tsv"
        self._write_inputs()

    def tearDown(self):
        self.temporary.cleanup()

    def _write_inputs(self, map_rows=None, producers=None, compile_sources=None):
        generated = {
            "fighter.c": self.source_root / "melee/fighter.c",
            "AXAlloc.c": self.source_root / "extern/dolphin/src/dolphin/ax/AXAlloc.c",
            "hsd.c": self.source_root / "sysdolphin/baselib/hsd.c",
            "OSAlloc.cpp": self.source_root / "sdk/OSAlloc.cpp",
        }
        project = {
            "project_owner.c": self.root / "src/project_owner.c",
            "renderer.c": self.root / "renderer/renderer.c",
        }
        if producers is None:
            producers = {
                "gameplay_runtime": [generated["fighter.c"], project["project_owner.c"]],
                "hsd_native_runtime": [generated["hsd.c"]],
                "fighter_source_runtime": [generated["AXAlloc.c"], generated["OSAlloc.cpp"]],
            }
        if compile_sources is None:
            compile_sources = [source for sources in producers.values() for source in sources]
        entries = []
        for source in compile_sources:
            entries.append({
                "directory": str(self.build),
                "file": str(source),
                "output": f"obj/{source.name}.o",
            })
        self.compile_commands.write_text(json.dumps(entries), encoding="utf-8")
        producer_lines = [f"{target}\t{source}\n" for target, sources in producers.items() for source in sources]
        self.producer_sources.write_text("".join(producer_lines), encoding="utf-8")
        if map_rows is None:
            map_rows = [
                (0x400, 0x1B8, 4, "libgameplay_runtime.a(fighter.c.o)", ".data.source_initialized"),
                (0x404, 0x000, 4, "libgameplay_runtime.a(fighter.c.o)", ".bss.source_zero"),
                (0x408, 0x000, 4, "libfighter_source_runtime.a(AXAlloc.c.o)", ".bss.ax_allocator"),
                (0x40C, 0x1BC, 4, "libgameplay_runtime.a(project_owner.c.o)", ".data.project_owner_state"),
                (0x410, 0x000, 4, "libfighter_source_runtime.a(OSAlloc.cpp.o)", ".bss.os_alloc"),
                (0x414, 0x1C0, 4, "librenderer_runtime.a(renderer.c.o)", ".data.renderer_state"),
            ]
        map_text = "    Addr      Off     Size Out     In      Symbol\n"
        for address, offset, size, object_name, section in map_rows:
            map_text += f"{address:x} {offset:x} {size:x} {object_name}:({section})\n"
        self.link_map.write_text(map_text, encoding="utf-8")

    def _args(self, target=manifest.TARGET):
        return SimpleNamespace(
            target=target,
            link_map=str(self.link_map),
            wasm=str(self.wasm),
            compile_commands=str(self.compile_commands),
            producer_sources=str(self.producer_sources),
            source_root=str(self.source_root),
            repository_root=str(self.root),
            cmake_cache=str(self.cache),
            build_graph=str(self.build_graph),
            emscripten_version_file=str(self.version),
            build_type="Release",
        )

    def test_manifest_includes_pinned_melee_and_dolphin_c_and_lists_exclusions(self):
        result = manifest.build_manifest(self._args())
        rows = {row["input_object"]: row for row in result["mutable_input_sections"]}
        melee = rows["libgameplay_runtime.a(fighter.c.o)"]
        self.assertEqual(melee["decision"], "source-c-static-candidate")
        self.assertEqual(melee["source"], "melee/fighter.c")
        dolphin = rows["libfighter_source_runtime.a(AXAlloc.c.o)"]
        self.assertEqual(dolphin["decision"], "source-c-static-candidate")
        self.assertEqual(dolphin["source"], "extern/dolphin/src/dolphin/ax/AXAlloc.c")
        self.assertEqual(rows["libgameplay_runtime.a(project_owner.c.o)"]["decision"], "excluded")
        self.assertEqual(rows["libfighter_source_runtime.a(OSAlloc.cpp.o)"]["exclusion_reason"],
                         "generated-source-non-c-translation-unit")
        self.assertEqual(rows["librenderer_runtime.a(renderer.c.o)"]["exclusion_reason"],
                         "archive-outside-generated-source-producers")
        self.assertFalse(result["complete_game_state"])
        self.assertEqual(result["configuration_identity"]["locked_melee_commit"], self.melee_commit)
        self.assertIn("AXAlloc.c", "\n".join(result["configuration_identity"]["producer_source_sha256"]))

    def test_non_private_target_is_rejected(self):
        with self.assertRaisesRegex(manifest.ManifestError, "private to"):
            manifest.build_manifest(self._args("gameplay_release"))

    def test_wrong_pinned_checkout_is_rejected(self):
        lock = self.root / "dependencies.lock.json"
        lock.write_text(json.dumps({"repositories": {"melee": {"commit": "1" * 40}}}), encoding="utf-8")
        with self.assertRaisesRegex(manifest.ManifestError, "does not match"):
            manifest.build_manifest(self._args())

    def test_unresolvable_member_from_known_producer_fails_closed(self):
        self._write_inputs(map_rows=[
            (0x400, 0x1B8, 4, "libfighter_source_runtime.a(missing.c.o)", ".data.state"),
        ])
        with self.assertRaisesRegex(manifest.ManifestError, "cannot resolve"):
            manifest.build_manifest(self._args())

    def test_missing_source_producer_fails_closed(self):
        producers = {
            "gameplay_runtime": [self.source_root / "melee/fighter.c"],
            "fighter_source_runtime": [self.source_root / "extern/dolphin/src/dolphin/ax/AXAlloc.c"],
        }
        self._write_inputs(producers=producers)
        with self.assertRaisesRegex(manifest.ManifestError, "missing source producer"):
            manifest.build_manifest(self._args())

    def test_map_rejects_malformed_duplicate_overlap_and_non_wasm32_rows(self):
        cases = (
            ("bad lib.a(x.o):(.data.state)\n", "malformed mutable"),
            (
                "400 0 4 lib.a(x.o):(.data.state)\n400 0 4 lib.a(x.o):(.data.state)\n",
                "duplicate mutable",
            ),
            (
                "400 0 8 lib.a(x.o):(.data.one)\n404 4 4 lib.a(y.o):(.bss.two)\n",
                "overlapping mutable",
            ),
            ("100000000 0 1 lib.a(x.o):(.data.state)\n", "outside wasm32"),
            ("0x400 0 4 lib.a(x.o):(.data.state)\n", "malformed mutable"),
            ("- 0 4 lib.a(x.o):(.data.state)\n", "lacks numeric"),
        )
        for rows, expected in cases:
            with self.subTest(expected=expected):
                self.link_map.write_text("Addr Off Size Out In Symbol\n" + rows, encoding="utf-8")
                with self.assertRaisesRegex(manifest.ManifestError, expected):
                    manifest._parse_map(self.link_map)

        for section_class in manifest.MUTABLE_SECTION_CLASSES:
            with self.subTest(malformed_section_class=section_class):
                self.link_map.write_text(
                    "Addr Off Size Out In Symbol\n"
                    f"bad lib.a(x.o):(.{section_class}.state)\n",
                    encoding="utf-8",
                )
                with self.assertRaisesRegex(manifest.ManifestError, "malformed mutable"):
                    manifest._parse_map(self.link_map)

    def test_map_rejects_repeated_section_even_if_vma_differs(self):
        self.link_map.write_text(
            "Addr Off Size Out In Symbol\n"
            "400 0 4 lib.a(x.o):(.data.state)\n"
            "404 4 4 lib.a(x.o):(.data.state)\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(manifest.ManifestError, "repeated mutable"):
            manifest._parse_map(self.link_map)

    def test_duplicate_object_member_provenance_fails_closed(self):
        duplicate = self.source_root / "duplicate/fighter.c"
        duplicate.parent.mkdir()
        duplicate.write_text("int duplicate;\n", encoding="utf-8")
        producers = {
            "gameplay_runtime": [self.source_root / "melee/fighter.c", duplicate],
            "hsd_native_runtime": [self.source_root / "sysdolphin/baselib/hsd.c"],
            "fighter_source_runtime": [self.source_root / "extern/dolphin/src/dolphin/ax/AXAlloc.c"],
        }
        compile_sources = [source for sources in producers.values() for source in sources]
        self._write_inputs(producers=producers, compile_sources=compile_sources,
                           map_rows=[(0x400, 0x1B8, 4, "libgameplay_runtime.a(fighter.c.o)", ".data.state")])
        with self.assertRaisesRegex(manifest.ManifestError, "ambiguous archive member"):
            manifest.build_manifest(self._args())


if __name__ == "__main__":
    unittest.main()
