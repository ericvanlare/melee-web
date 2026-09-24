"""Focused checks for the production audio-player package boundary."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import runpy
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))

import build_public as public  # noqa: E402
import stage_audio_preview as stage  # noqa: E402


class AudioPlayerReleaseTests(unittest.TestCase):
    def generated(self, *, production: bool = True):
        native = {
            name: (b"gameplay_audio_preview.wasm" if name.endswith(".js") else b"fixture")
            for name in stage.NATIVE
        }
        with patch.object(stage, "runtime", return_value=(native, "a" * 64)), \
                patch.object(stage, "source_commit", return_value="b" * 40):
            return stage.expected_files(production=production)

    @staticmethod
    def runtime_graph(files, runtime_hash):
        prefix = f"runtime/{runtime_hash}/"
        return {
            path.removeprefix(prefix): data
            for path, data in files.items()
            if path.startswith(prefix)
        }

    @staticmethod
    def write_manifest(path: Path, value: dict) -> None:
        path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def test_production_profile_is_distinct_and_contains_the_reviewed_audio_graph(self):
        files, meta = self.generated()
        record = stage.package_record(files, meta, production=True)
        runtime_prefix = f"runtime/{meta['runtime_hash']}/"

        self.assertEqual(record["schema"], stage.PRODUCTION_SCHEMA)
        self.assertEqual(record["project"], "webmelee")
        self.assertEqual(record["profile"], "audio-player")
        self.assertIn(runtime_prefix + "gameplay_audio_preview.js", files)
        self.assertIn(runtime_prefix + "gameplay_audio_preview.wasm", files)
        self.assertIn(runtime_prefix + "dsp-coefficients.mjs", files)
        self.assertIn(runtime_prefix + "audio-worklet.js", files)
        self.assertFalse(any(path.endswith("/gameplay_public.js") for path in files))
        self.assertFalse(any(path.endswith("/gameplay_public.wasm") for path in files))

        self.assertIn(b'data-environment="production"', files["index.html"])
        self.assertNotIn(b"[staging]", files["index.html"])
        self.assertNotIn(b"audio-note", files["index.html"])
        self.assertNotIn(b"Audio is disabled", files["index.html"])
        self.assertIn(b"This public alpha enables music and sound effects", files["notices.html"])
        self.assertNotIn(b"This staging preview", files["notices.html"])
        self.assertIn(b"Audio-enabled public alpha", files["licenses/runtime-third-party.txt"])
        self.assertIn(b"dolphin-gpl-2.0-or-later.txt", files["licenses/runtime-third-party.txt"])
        self.assertEqual(files["_headers"], public._headers("production", False, "player").encode())
        self.assertEqual(files["_redirects"], public._redirects("production").encode())
        self.assertEqual(files["robots.txt"], public._robots("production", False).encode())

    def test_preview_profile_remains_staging_only(self):
        files, meta = self.generated(production=False)
        record = stage.package_record(files, meta, production=False)

        self.assertEqual(record["schema"], stage.SCHEMA)
        self.assertEqual(record["project"], "webmelee-staging")
        self.assertEqual(record["profile"], "audio-preview")
        self.assertIn(b'data-environment="preview"', files["index.html"])
        self.assertIn(b"[staging]", files["index.html"])
        self.assertIn(b"This staging preview", files["notices.html"])
        self.assertIn(b"Audio staging preview", files["licenses/runtime-third-party.txt"])
        self.assertEqual(files["_redirects"], b"# Audio listening preview only. No host redirects.\n")

    def test_cross_profile_audit_rejects_the_other_manifest(self):
        production_files, production_meta = self.generated()
        preview_files, preview_meta = self.generated(production=False)
        with tempfile.TemporaryDirectory(prefix="audio-player-package-") as directory:
            root = Path(directory)
            output = root / "site"
            manifest = root / "site.manifest.json"
            with patch.object(stage, "expected_files", return_value=(production_files, production_meta)):
                stage.prepare(output, manifest, production=True)
                stage.audit(output, manifest, production=True)
                with self.assertRaisesRegex(ValueError, "bytes|manifest"):
                    stage.audit(output, manifest, production=False)

            preview_output = root / "preview"
            preview_manifest = root / "preview.manifest.json"
            with patch.object(stage, "expected_files", return_value=(preview_files, preview_meta)):
                stage.prepare(preview_output, preview_manifest, production=False)
                stage.audit(preview_output, preview_manifest, production=False)
                with self.assertRaisesRegex(ValueError, "bytes|manifest"):
                    stage.audit(preview_output, preview_manifest, production=True)

    def test_production_wrapper_selects_production_policy(self):
        wrapper = SCRIPTS / "release_audio_player.py"
        with patch.object(stage, "main") as entrypoint, \
                patch.object(sys, "argv", [str(wrapper)]):
            runpy.run_path(str(wrapper), run_name="__main__")
        entrypoint.assert_called_once_with(production=True)

    def test_production_graph_rejects_external_upload_private_and_diagnostic_content(self):
        files, meta = self.generated()
        graph = self.runtime_graph(files, meta["runtime_hash"])
        cases = (
            ("runtime-audio.mjs", b"\nfetch('https://evil.example')", "external runtime URL|network API"),
            ("runtime-audio.mjs", b"\nsendBeacon('/upload')", "upload/evidence"),
            ("runtime-audio.mjs", b"\n/Users/" + b"secret/local", "private path"),
            ("runtime-audio.mjs", b"\nconsole.log('debug')", "diagnostic code"),
        )
        for name, marker, message in cases:
            with self.subTest(name=name, marker=marker):
                tampered = dict(graph)
                tampered[name] += marker
                with self.assertRaisesRegex(public.BuildError, message):
                    public._validate_runtime_graph(tampered, audio=True)

    def test_production_native_loader_binding_is_required(self):
        native = {name: b"fixture" for name in stage.NATIVE}
        native["gameplay_audio_preview.js"] = b"loader without its wasm name"
        with patch.object(stage, "runtime", return_value=(native, "a" * 64)), \
                patch.object(stage, "source_commit", return_value="b" * 40), \
                self.assertRaisesRegex(public.BuildError, "does not bind gameplay_audio_preview.wasm"):
            stage.expected_files(production=True)

    def test_prepare_rejects_dangling_manifest_symlink(self):
        files, meta = self.generated()
        with tempfile.TemporaryDirectory(prefix="audio-player-symlink-") as directory:
            root = Path(directory)
            output = root / "site"
            manifest = root / "manifest.json"
            target = root / "missing-target.json"
            manifest.symlink_to(target)
            with patch.object(stage, "expected_files", return_value=(files, meta)), \
                    self.assertRaisesRegex(ValueError, "symlink|fresh output paths"):
                stage.prepare(output, manifest, production=True)
            self.assertFalse(output.exists())
            self.assertFalse(target.exists())

    def _production_manifest(self, root: Path):
        files, meta = self.generated()
        manifest = root / "site.manifest.json"
        self.write_manifest(manifest, stage.package_record(files, meta, production=True))
        return manifest, stage.package_record(files, meta, production=True)

    @staticmethod
    def fake_http(url):
        return 404, {"X-Robots-Tag": "noindex"}, b"", url

    def test_production_verify_accepts_only_reviewed_origins(self):
        accepted = (
            "https://webmelee.gg",
            "https://1234abcd.webmelee.pages.dev",
            "https://webmelee-staging.pages.dev",
            "https://1234abcd.webmelee-staging.pages.dev",
            "http://127.0.0.1:18961",
            "http://localhost:18961",
        )
        rejected = (
            "https://www.webmelee.gg",
            "https://webmelee.pages.dev",
            "https://webmelee.gg.evil.example",
            "https://1234abcd.webmelee-staging.pages.dev.evil.example",
            "https://1234abcd.webmelee.pages.dev/path",
            "https://1234ABCD.webmelee.pages.dev",
            "https://1234567.webmelee.pages.dev",
            "http://webmelee.gg",
            "https://webmelee.gg@evil.example",
        )
        with tempfile.TemporaryDirectory(prefix="audio-player-verify-") as directory:
            root = Path(directory)
            manifest, _ = self._production_manifest(root)
            with patch.object(stage.http, "check_resource", return_value={}), \
                    patch.object(stage.http, "get", side_effect=self.fake_http):
                for origin in accepted:
                    with self.subTest(origin=origin):
                        result = stage.verify(origin, manifest, production=True)
                        self.assertEqual(result["result"], "pass")

            for origin in rejected:
                with self.subTest(origin=origin), patch.object(stage.http, "check_resource") as check, \
                        patch.object(stage.http, "get") as get, \
                        self.assertRaisesRegex(ValueError, "restricted"):
                    stage.verify(origin, manifest, production=True)
                    check.assert_not_called()
                    get.assert_not_called()

    def test_production_verify_rejects_wrong_manifest_before_network(self):
        with tempfile.TemporaryDirectory(prefix="audio-player-manifest-") as directory:
            root = Path(directory)
            manifest, value = self._production_manifest(root)
            for field, replacement in (
                ("schema", stage.SCHEMA),
                ("project", "webmelee-staging"),
                ("profile", "audio-preview"),
                ("source_sha", "bad"),
                ("identity_sha256", "bad"),
            ):
                with self.subTest(field=field):
                    tampered = dict(value)
                    tampered[field] = replacement
                    manifest.write_text(json.dumps(tampered), encoding="utf-8")
                    with patch.object(stage.http, "check_resource") as check, \
                            patch.object(stage.http, "get") as get, \
                            self.assertRaises((ValueError, AttributeError)):
                        stage.verify("https://webmelee.gg", manifest, production=True)
                    check.assert_not_called()
                    get.assert_not_called()
            manifest.unlink()

    def test_production_verify_rejects_inventory_mutation_before_network(self):
        with tempfile.TemporaryDirectory(prefix="audio-player-inventory-") as directory:
            root = Path(directory)
            manifest, value = self._production_manifest(root)
            cases = []
            extra = dict(value)
            extra["files"] = list(value["files"]) + [{
                "path": "runtime/" + value["runtime_hash"] + "/evil.mjs",
                "size": 1,
                "sha256": "0" * 64,
            }]
            cases.append(("extra path", extra))
            bad_size = dict(value)
            bad_size["files"] = [dict(item) for item in value["files"]]
            bad_size["files"][0]["size"] = -1
            cases.append(("negative size", bad_size))
            for name, tampered in cases:
                with self.subTest(name=name):
                    self.write_manifest(manifest, tampered)
                    with patch.object(stage.http, "check_resource") as check, \
                            patch.object(stage.http, "get") as get, \
                            self.assertRaisesRegex(ValueError, "inventory"):
                        stage.verify("https://webmelee.gg", manifest, production=True)
                    check.assert_not_called()
                    get.assert_not_called()

    def test_production_verify_rejects_exposed_source_map(self):
        with tempfile.TemporaryDirectory(prefix="audio-player-map-") as directory:
            root = Path(directory)
            manifest, _ = self._production_manifest(root)

            def get_with_map_failure(url):
                if url.endswith(".map"):
                    return 200, {"X-Robots-Tag": "noindex"}, b"map", url
                return self.fake_http(url)

            with patch.object(stage.http, "check_resource", return_value={}), \
                    patch.object(stage.http, "get", side_effect=get_with_map_failure), \
                    self.assertRaisesRegex(ValueError, "Unexpected exposed route"):
                stage.verify("https://webmelee.gg", manifest, production=True)


if __name__ == "__main__":
    unittest.main()
