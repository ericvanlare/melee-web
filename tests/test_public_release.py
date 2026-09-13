"""Focused fail-closed tests for the static public shell release boundary."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import audit_public  # noqa: E402
import build_public  # noqa: E402
from materialize_pipeline_cache import materialize  # noqa: E402

from audit_public import AuditError, audit  # noqa: E402
from build_public import (  # noqa: E402
    BuildError,
    DEFAULT_SOURCE,
    MAX_FILE_BYTES,
    PLAYER_RUNTIME_FILES,
    RUNTIME_REQUIRED_EXPORTS,
    RUNTIME_FORBIDDEN_EXPORTS,
    PLAYER_SOURCE,
    _tree_hash,
    RUNTIME_SOURCE_FILES,
    SOURCE_ALLOWLIST,
    build,
)


def _uleb(value: int) -> bytes:
    result = bytearray()
    while True:
        byte = value & 0x7f
        value >>= 7
        result.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(result)


class PublicReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="melee-public-release-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        for name in SOURCE_ALLOWLIST:
            shutil.copyfile(DEFAULT_SOURCE / name, self.source / name)

    def paths(self, mode: str = "preview") -> tuple[Path, Path]:
        output = self.root / mode
        manifest = self.root / f"{mode}.manifest.json"
        build(self.source, output, mode, "Release Operator" if mode == "production" else None,
              "rights@example.test" if mode == "production" else None, manifest)
        return output, manifest

    def runtime_source_fixture(self) -> Path:
        """Create producer inputs without depending on any local native build.

        Native tools and prepared gameplay are deliberately tiny fixtures. The
        real provenance readers still hash files, trees, patches and Git state;
        no validation function is mocked or skipped.
        """
        if hasattr(self, "fixture_repo"):
            return self.fixture_repo
        repo = self.root / "producer-checkout"
        repo.mkdir()
        for rel in set(RUNTIME_SOURCE_FILES) | {
            f"web/{name}" for name in build_public.PLAYER_SOURCE_RUNTIME_FILES
        }:
            destination = repo / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / rel, destination)
        for rel in build_public.RUNTIME_TOOLCHAIN_PATHS:
            destination = repo / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(f"test tool fixture: {rel}\n")
        materialize(repo / "web/initial_pipeline_cache.db.gz.b64",
                    repo / "build/browser-release/initial_pipeline_cache.db")
        prepared = repo / "build/gameplay-source"
        prepared.mkdir()
        (prepared / "fixture.c").write_text("int fixture(void) { return 0; }\n")
        subprocess.run(["git", "init", "-q", "--template=", "--initial-branch=fixture"],
                       cwd=prepared, check=True)
        subprocess.run(["git", "add", "fixture.c"], cwd=prepared, check=True)
        subprocess.run(["git", "-c", "user.name=Release Test", "-c",
                        "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
                        "-c", "core.hooksPath=/dev/null", "commit", "-qm", "Fixture"],
                       cwd=prepared, check=True)
        for rel in build_public.PREPARED_GAMEPLAY_PATCHES.values():
            (repo / rel).write_text("test prepared patch fixture\n")
        for module in (build_public, audit_public):
            root_patch = patch.object(module, "ROOT", repo)
            root_patch.start()
            self.addCleanup(root_patch.stop)
        self.fixture_repo = repo
        return repo

    def runtime_fixture(self) -> Path:
        fixture_root = self.runtime_source_fixture()
        container = self.root / f"runtime-input-{len(tuple(self.root.glob('runtime-input-*')))}"
        runtime = container / "artifacts"
        runtime.mkdir(parents=True)
        source_map = {
            "melee-runtime.mjs": fixture_root / "web" / "melee-runtime.mjs",
            "runtime-assets.mjs": fixture_root / "web" / "runtime-assets.mjs",
            "disc-image.mjs": fixture_root / "web" / "disc-image.mjs",
            "dsp-coefficients.mjs": fixture_root / "web" / "dsp-coefficients.mjs",
            "prototype-keyboard-layouts.mjs": fixture_root / "web" / "prototype-keyboard-layouts.mjs",
            "audio-worklet.js": fixture_root / "web" / "audio-worklet.js",
            "audio-ring.mjs": fixture_root / "web" / "audio-ring.mjs",
        }
        for name, path in source_map.items():
            shutil.copyfile(path, runtime / name)
        (runtime / "gameplay_public.js").write_text(
            'FS.mkdir("/home/web_user"); ENV["HOME"] = "/home/web_user"; // gameplay_public.wasm\n'
        )
        (runtime / "gameplay_public.data").write_bytes((fixture_root / "build/browser-release/initial_pipeline_cache.db").read_bytes())
        # Minimal version-1 Wasm module with one function export; the audit
        # parses the export section rather than trusting the sidecar list.
        export_names = tuple(RUNTIME_REQUIRED_EXPORTS)
        exports = bytearray(_uleb(len(export_names)))
        for index, export_name in enumerate(export_names):
            encoded_name = export_name.encode("utf-8")
            exports.extend(_uleb(len(encoded_name)) + encoded_name + b"\x00" + _uleb(index))
        payload = bytes(exports)
        (runtime / "gameplay_public.wasm").write_bytes(b"\x00asm\x01\x00\x00\x00\x07" + _uleb(len(payload)) + payload)
        artifacts = []
        for name in ("gameplay_public.js", "gameplay_public.wasm", "gameplay_public.data"):
            data = (runtime / name).read_bytes()
            artifacts.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        source_hashes = {name: hashlib.sha256((fixture_root / name).read_bytes()).hexdigest() for name in RUNTIME_SOURCE_FILES}
        tool_paths = (".deps/emsdk/.emscripten", ".deps/emsdk/upstream/emscripten/emcc",
                      ".deps/emsdk/upstream/emscripten/emscripten-version.txt", ".venv/bin/cmake", ".venv/bin/ninja")
        tool_hashes = {name: hashlib.sha256((fixture_root / name).read_bytes()).hexdigest() for name in tool_paths}
        seed_source = fixture_root / "web/initial_pipeline_cache.db.gz.b64"
        seed_materialized = fixture_root / "build/browser-release/initial_pipeline_cache.db"
        all_exports = [{"name": name, "kind": 0, "index": index} for index, name in enumerate(export_names)]
        identity = {
            "schema": "melee-web-runtime-public-build-v1",
            "target": "runtime-public",
            "configuration": "Release",
            "artifact_root": ".",
            "pipeline_seed": {
                "source": {"path": "web/initial_pipeline_cache.db.gz.b64", "bytes": seed_source.stat().st_size,
                            "sha256": hashlib.sha256(seed_source.read_bytes()).hexdigest()},
                "materialized": {"path": "build/browser-release/initial_pipeline_cache.db", "bytes": seed_materialized.stat().st_size,
                                 "sha256": hashlib.sha256(seed_materialized.read_bytes()).hexdigest()},
                "expected_sha256": hashlib.sha256(seed_materialized.read_bytes()).hexdigest(),
                "sqlite_tables": [],
            },
            "source_inputs": {
                "files_sha256": source_hashes,
                "trees": {name: {"path": name, "files": _tree_hash(fixture_root / name)[0], "sha256": _tree_hash(fixture_root / name)[1]} for name in ("src", "cmake")},
                "prepared_gameplay": {
                    "path": "build/gameplay-source",
                    "pinned_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=fixture_root / "build/gameplay-source", text=True).strip(),
                    "composed_patch": {"path": "build/gameplay-source/.git/melee-web-composed.patch", "sha256": hashlib.sha256((fixture_root / "build/gameplay-source/.git/melee-web-composed.patch").read_bytes()).hexdigest()},
                    "reviewed_patch": {"path": "build/gameplay-source/.git/melee-web-gameplay.patch", "sha256": hashlib.sha256((fixture_root / "build/gameplay-source/.git/melee-web-gameplay.patch").read_bytes()).hexdigest()},
                    "working_tree_diff_sha256": hashlib.sha256(subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=fixture_root / "build/gameplay-source")).hexdigest(),
                    "tree": {"path": "build/gameplay-source", "files": _tree_hash(fixture_root / "build/gameplay-source")[0], "sha256": _tree_hash(fixture_root / "build/gameplay-source")[1]},
                },
            },
            "toolchain": {"emscripten": "fixture", "cmake": "fixture", "ninja": "fixture", "sha256": tool_hashes},
            "upload_convention": {"group": "artifacts plus the reviewed public-shell files",
                                   "identity_path": "build/runtime-public-identity.json",
                                   "identity_is_outside_artifact_root": True},
            "artifacts": artifacts,
            "wasm_exports": {"required": list(RUNTIME_REQUIRED_EXPORTS), "functions": list(RUNTIME_REQUIRED_EXPORTS),
                             "all": all_exports, "javascript_bindings": {name: name for name in RUNTIME_REQUIRED_EXPORTS},
                             "forbidden_absent": sorted(RUNTIME_FORBIDDEN_EXPORTS)},
        }
        (container / "runtime-public-identity.json").write_text(json.dumps(identity, sort_keys=True) + "\n")
        return runtime

    def test_preview_build_is_auditable_and_uses_hashed_assets(self):
        output, manifest = self.paths()
        result = audit(output, manifest)
        self.assertEqual(result["mode"], "preview")
        self.assertIn('data-environment="preview"', (output / "index.html").read_text())
        self.assertIn('<title>[staging] WebMelee (WIP)</title>', (output / "index.html").read_text())
        self.assertEqual(len(list((output / "assets").iterdir())), 2)
        self.assertFalse((output / "_redirects").exists())

    def test_player_requires_runtime_and_preserves_immutable_loader_graph(self):
        with self.assertRaisesRegex(BuildError, "runtime-dir"):
            build(output=self.root / "missing-player", profile="player")
        runtime = self.runtime_fixture()
        output = self.root / "player"
        manifest = self.root / "player.manifest.json"
        build(output=output, manifest=manifest, profile="player", runtime_dir=runtime)
        result = audit(output, manifest)
        self.assertEqual(result["profile"], "player")
        runtime_hash = json.loads(manifest.read_text())["runtime"]["hash"]
        self.assertTrue((output / "runtime" / runtime_hash / "player" / "player-shell.mjs").is_file())
        self.assertEqual((output / "runtime" / runtime_hash / "melee-runtime.mjs").read_bytes(),
                         (ROOT / "web" / "melee-runtime.mjs").read_bytes())
        self.assertNotIn("gameplay_public.wasm", (output / "index.html").read_text())
        player_css = f"/runtime/{runtime_hash}/player/player.css"
        legal_css = f"/assets/site.{hashlib.sha256((DEFAULT_SOURCE / 'site.css').read_bytes()).hexdigest()[:16]}.css"
        self.assertIn(player_css, (output / "index.html").read_text())
        self.assertTrue((output / legal_css.lstrip("/")).is_file())
        for name in ("terms.html", "privacy.html", "copyright.html", "notices.html", "404.html"):
            page = (output / name).read_text()
            self.assertIn(legal_css, page)
            self.assertNotIn(player_css, page)

    def test_player_runtime_hash_changes_when_entry_shell_changes(self):
        runtime = self.runtime_fixture()
        player_source = self.root / "player-source"
        shutil.copytree(PLAYER_SOURCE, player_source)
        first_manifest = self.root / "first-player.manifest.json"
        build(source=player_source, output=self.root / "first-player", manifest=first_manifest,
              profile="player", runtime_dir=runtime)
        first_hash = json.loads(first_manifest.read_text())["runtime"]["hash"]
        (player_source / "player-shell.mjs").write_text(
            (player_source / "player-shell.mjs").read_text() + "\n// reviewed entry change\n"
        )
        second_manifest = self.root / "second-player.manifest.json"
        build(source=player_source, output=self.root / "second-player", manifest=second_manifest,
              profile="player", runtime_dir=runtime)
        second_hash = json.loads(second_manifest.read_text())["runtime"]["hash"]
        self.assertNotEqual(first_hash, second_hash)

    def test_player_rejects_runtime_identity_drift_and_undeclared_files(self):
        runtime = self.runtime_fixture()
        identity_path = runtime.parent / "runtime-public-identity.json"
        identity = json.loads(identity_path.read_text())
        identity["artifacts"].append({"path": "evil.js", "bytes": 5, "sha256": "0" * 64})
        identity_path.write_text(json.dumps(identity))
        with self.assertRaisesRegex(BuildError, "unauthorized artifact"):
            build(output=self.root / "extra-runtime", profile="player", runtime_dir=runtime)
        runtime = self.runtime_fixture()
        identity_path = runtime.parent / "runtime-public-identity.json"
        identity = json.loads(identity_path.read_text())
        next(item for item in identity["artifacts"] if item["path"] == "gameplay_public.wasm")["sha256"] = "0" * 64
        identity_path.write_text(json.dumps(identity))
        with self.assertRaisesRegex(BuildError, "identity mismatch"):
            build(output=self.root / "drift-runtime", profile="player", runtime_dir=runtime)

    def test_player_rejects_changed_producer_inputs_in_build_and_audit(self):
        runtime = self.runtime_fixture()
        output = self.root / "provenance-player"
        manifest = self.root / "provenance-player.manifest.json"
        build(output=output, manifest=manifest, profile="player", runtime_dir=runtime)
        inputs = (
            "src/browser_input.cpp",
            ".venv/bin/ninja",
            "build/gameplay-source/fixture.c",
            "build/gameplay-source/.git/melee-web-composed.patch",
            "build/browser-release/initial_pipeline_cache.db",
        )
        for index, rel in enumerate(inputs):
            with self.subTest(input=rel):
                source = self.fixture_repo / rel
                original = source.read_bytes()
                try:
                    source.write_bytes(original + b"\nchanged producer input\n")
                    with self.assertRaisesRegex(BuildError, "differ"):
                        build(output=self.root / f"changed-producer-{index}",
                              profile="player", runtime_dir=runtime)
                    with self.assertRaisesRegex(AuditError, "differ"):
                        audit(output, manifest)
                finally:
                    source.write_bytes(original)

    def test_player_rejects_nonstandard_generated_private_home_path(self):
        runtime = self.runtime_fixture()
        gameplay = runtime / "gameplay_public.js"
        gameplay.write_text('FS.mkdir("/home/web_user/private"); // gameplay_public.wasm\n')
        identity_path = runtime.parent / "runtime-public-identity.json"
        identity = json.loads(identity_path.read_text())
        record = next(item for item in identity["artifacts"] if item["path"] == "gameplay_public.js")
        data = gameplay.read_bytes()
        record["bytes"] = len(data)
        record["sha256"] = hashlib.sha256(data).hexdigest()
        identity_path.write_text(json.dumps(identity))
        with self.assertRaisesRegex(BuildError, "private path"):
            build(output=self.root / "private-home-runtime", profile="player", runtime_dir=runtime)

    def test_player_rejects_runtime_file_over_pages_per_file_limit(self):
        runtime = self.runtime_fixture()
        gameplay = runtime / "gameplay_public.js"
        data = b"a" * (25 * 1024 * 1024 + 1) + b"\n// gameplay_public.wasm\n"
        gameplay.write_bytes(data)
        identity_path = runtime.parent / "runtime-public-identity.json"
        identity = json.loads(identity_path.read_text())
        record = next(item for item in identity["artifacts"] if item["path"] == "gameplay_public.js")
        record["bytes"] = len(data)
        record["sha256"] = hashlib.sha256(data).hexdigest()
        identity_path.write_text(json.dumps(identity))
        with self.assertRaisesRegex(BuildError, "25 MiB"):
            build(output=self.root / "oversize-runtime", profile="player", runtime_dir=runtime)

    def test_production_requires_explicit_operator_and_contact(self):
        with self.assertRaisesRegex(BuildError, "requires explicit"):
            build(self.source, self.root / "missing-config", "production")
        output, manifest = self.paths("production")
        self.assertEqual(audit(output, manifest)["mode"], "production")
        self.assertNotIn("DRAFT PREVIEW", (output / "index.html").read_text())
        self.assertIn('data-environment="production"', (output / "index.html").read_text())
        self.assertIn("pages.dev", (output / "_headers").read_text())

    def test_output_must_be_fresh_and_manifest_outside_deploy_tree(self):
        output, manifest = self.paths()
        with self.assertRaisesRegex(BuildError, "new directory"):
            build(self.source, output)
        with self.assertRaisesRegex(BuildError, "outside deploy"):
            build(self.source, self.root / "second", manifest=self.root / "second" / "manifest.json")

    def test_extra_source_path_is_not_silently_ignored(self):
        (self.source / "runtime.wasm").write_bytes(b"\x00asm\x01\x00\x00\x00")
        with self.assertRaisesRegex(BuildError, "unauthorized source path"):
            build(self.source, self.root / "extra")

    def test_renamed_iso_header_and_wasm_are_rejected(self):
        image = bytearray(0x8006)
        image[0x8001:0x8006] = b"CD001"
        (self.source / "site.css").write_bytes(image)
        with self.assertRaisesRegex(BuildError, "ISO-9660"):
            build(self.source, self.root / "iso")
        (self.source / "site.css").write_bytes(b"\x00asm\x01\x00\x00\x00")
        with self.assertRaisesRegex(BuildError, "WebAssembly"):
            build(self.source, self.root / "wasm")

    def test_secret_file_input_and_diagnostic_code_are_rejected(self):
        (self.source / "site.js").write_text("<input type='file'>\n")
        with self.assertRaisesRegex(BuildError, "file input"):
            build(self.source, self.root / "file-input")
        original = (self.source / "site.css").read_text()
        (self.source / "site.css").write_text(original + "\nAKIA1234567890ABCDEF\n")
        with self.assertRaisesRegex(BuildError, "secret"):
            build(self.source, self.root / "secret")
        (self.source / "site.css").write_text(original + "\nconsole.log('diagnostic')\n")
        with self.assertRaisesRegex(BuildError, "diagnostic"):
            build(self.source, self.root / "diagnostic")

    def test_symlink_inputs_and_outputs_are_rejected(self):
        target = self.source / "site.css"
        replacement = self.root / "real-css"
        replacement.write_bytes(target.read_bytes())
        target.unlink()
        target.symlink_to(replacement)
        with self.assertRaisesRegex(BuildError, "symlink"):
            build(self.source, self.root / "symlink-input")
        output = self.root / "real-output"
        output.mkdir()
        link = self.root / "linked-output"
        link.symlink_to(output, target_is_directory=True)
        with self.assertRaisesRegex(BuildError, "symlink"):
            build(DEFAULT_SOURCE, link)

    def test_audit_rejects_extra_files_mutations_and_manifest_drift(self):
        output, manifest = self.paths()
        (output / "logs").mkdir()
        (output / "logs" / "debug.txt").write_text("diagnostic output")
        with self.assertRaisesRegex(AuditError, "unauthorized output directory"):
            audit(output, manifest)
        (output / "logs" / "debug.txt").unlink()
        (output / "logs").rmdir()
        (output / "assets" / next(p.name for p in (output / "assets").iterdir() if p.suffix == ".css")).write_text("mutated")
        with self.assertRaisesRegex(AuditError, "hash"):
            audit(output, manifest)
        # Rebuild in a separate fresh directory, then mutate only the sidecar.
        output = self.root / "manifest-drift"
        manifest = self.root / "manifest-drift.json"
        build(self.source, output, "preview", manifest=manifest)
        value = json.loads(manifest.read_text())
        value["files"][0]["sha256"] = "0" * 64
        manifest.write_text(json.dumps(value))
        with self.assertRaisesRegex(AuditError, "manifest"):
            audit(output, manifest)

    def test_audit_rejects_oversized_deployed_file(self):
        output = self.root / "large"
        manifest = self.root / "large.manifest.json"
        build(self.source, output, "preview", manifest=manifest)
        css = next(path for path in (output / "assets").iterdir() if path.suffix == ".css")
        css.write_bytes(b"x" * (MAX_FILE_BYTES + 1))
        with self.assertRaisesRegex(AuditError, "25 MiB"):
            audit(output, manifest)

    def test_rewritten_manifest_cannot_authorize_changed_page(self):
        output, manifest = self.paths()
        page = output / "index.html"
        page.write_text(page.read_text().replace("Gameplay is not available", "Gameplay is available"))
        value = json.loads(manifest.read_text())
        for record in value["files"]:
            if record["path"] == "index.html":
                record["size"] = page.stat().st_size
                record["sha256"] = hashlib.sha256(page.read_bytes()).hexdigest()
        manifest.write_text(json.dumps(value))
        with self.assertRaisesRegex(AuditError, "approved source"):
            audit(output, manifest)

    def test_manifest_records_every_deployed_file_and_no_private_path(self):
        output, manifest = self.paths()
        value = json.loads(manifest.read_text())
        self.assertEqual({entry["path"] for entry in value["files"]}, {
            path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()
        })
        self.assertNotIn(str(output), manifest.read_text())
        for entry in value["files"]:
            self.assertEqual(entry["sha256"], hashlib.sha256((output / entry["path"]).read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
