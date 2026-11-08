import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from build_public import _diagnostics_graph_hash


SPEC = importlib.util.spec_from_file_location(
    "public_staging", Path(__file__).resolve().parents[1] / "scripts" / "stage_public.py"
)
stage = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(stage)


class PublicStagingTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory(prefix="public-staging-test-")
        self.root = Path(self.tempdir.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.base = self.root / "base"
        self.base.mkdir()
        self.base_manifest = self.root / "base.manifest.json"
        self.base_files = self._write_base()
        self.base_value = self._manifest(self.base_files)
        self.base_manifest.write_text(json.dumps(self.base_value, indent=2, sort_keys=True) + "\n")
        self.base_digest = hashlib.sha256(self.base_manifest.read_bytes()).hexdigest()

    def tearDown(self):
        self.tempdir.cleanup()

    def _write_base(self):
        files = {}
        for name in stage.HTML_FILES:
            edition = '<span id="edition">alpha · no audio</span>' if name == "index.html" else ""
            files[name] = f"<html><head><title>WebMelee</title></head><body>{edition}</body></html>".encode()
        files["_headers"] = (
            b"webmelee.pages.dev\n"
            b"  X-Robots-Tag: noindex\n"
        )
        files["_redirects"] = b"# Production redirects are audited separately.\n"
        files["robots.txt"] = b"# Production policy\nUser-agent: *\nDisallow: /\n"
        files["assets/site.css"] = b"protected css\n"
        files["licenses/runtime-third-party.txt"] = b"protected legal payload\n"
        runtime = "runtime/1234567890abcdef/gameplay_public.wasm"
        files[runtime] = b"protected runtime bytes\n"
        for rel, data in files.items():
            path = self.base / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        return files

    def _manifest(self, files):
        policy = {
            "mode": "disabled",
            "pcm_output": False,
            "dsp_resampler": False,
            "dsp_coefficients_required": False,
        }
        runtime = {
            "path": "runtime/1234567890abcdef",
            "hash": "1234567890abcdef",
            "identity_sha256": "a" * 64,
            "identity": {
                "target": "runtime-public",
                "configuration": "Release",
                "audio_policy": policy,
                "artifacts": [],
            },
        }
        return {
            "schema": stage.PUBLIC_MANIFEST_SCHEMA,
            "profile": "player",
            "mode": "production",
            "draft_preview": False,
            "index_production": False,
            "operator": stage.OPERATOR,
            "contact": stage.CONTACT,
            "runtime": runtime,
            "files": stage._records(files),
        }

    def _write_overlay(self, name="overlay"):
        output = self.root / name
        output.mkdir()
        overlay_files = stage._apply_overlay(self.base_files, output)
        manifest = self.root / "overlay.manifest.json"
        overlay_value = copy.deepcopy(self.base_value)
        overlay_value["files"] = stage._records(overlay_files)
        manifest.write_text(json.dumps(overlay_value, indent=2, sort_keys=True) + "\n")
        return output, manifest, overlay_files

    def _rollback_fixture(self):
        header = b"/*\n  Content-Security-Policy: default-src 'none'; connect-src 'self';\n"
        self.base_files["_headers"] = header + self.base_files["_headers"]
        (self.base / "_headers").write_bytes(self.base_files["_headers"])
        self.base_value["files"] = stage._records(self.base_files)
        self.base_manifest.write_bytes(stage._canonical_json_bytes(self.base_value))
        self.base_digest = stage._sha256_bytes(self.base_manifest.read_bytes())
        for name in (*stage.ROLLBACK_SOURCE_FILES, "web/online/index.html",
                     "web/audio-worklet.js", "web/audio-ring.mjs",
                     "src/slippi_profile/COPYING", "src/slippi_profile/README.md",
                     "docs/licenses/runtime-third-party.txt",
                     "docs/licenses/dolphin-gpl-2.0-or-later.txt"):
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"fixture {name}\n")
        (self.source / "src/slippi_profile/profile.h").write_text(
            '#define SLIPPI_PROFILE_CAMERA_HISTORY_V20_ID "profile-v20"\n')
        build, linked = self.root / "rollback-build", self.root / "rollback-linked"
        build.mkdir()
        linked.mkdir()
        artifacts = {}
        for name in stage.ROLLBACK_NATIVE_FILES:
            data = name.encode()
            (build / name).write_bytes(data)
            artifacts[name] = stage._sha256_bytes(data)
        table = {"schema": "melee-web-rollback-linked-statics-v1", "rows": [[32, 4, 2, 1]],
                 "wasmSha256": artifacts["gameplay_menu_browser.wasm"], "policySha256": "d" * 64}
        table_raw = stage._canonical_json_bytes(table)
        (linked / "linked-statics.json").write_bytes(table_raw)
        receipt = {"schema": "melee-web-rollback-staging-linked-table-receipt-v1",
                   "profile": "runtime-rollback-staging", "sourceSha": "b" * 40,
                   "artifacts": artifacts, "policySha256": "d" * 64,
                   "configurationIdentitySha256": "e" * 64,
                   "tableSha256": stage._sha256_bytes(table_raw), "rowCount": 1}
        receipt_raw = stage._canonical_json_bytes(receipt)
        (linked / "linked-statics-receipt.json").write_bytes(receipt_raw)
        return dict(rollback_build=build, rollback_linked=linked,
                    rollback_linked_receipt_sha256=stage._sha256_bytes(receipt_raw))

    def test_rollback_exact_inventory_and_protected_vanilla(self):
        options = self._rollback_fixture()
        output, receipt = self.root / "online-output", self.root / "online-receipt.json"
        with patch.object(stage, "_validate_source_sha"), patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"):
            result = stage.prepare_staging(
                self.source, "b" * 40, output, receipt, reuse_output=self.base,
                reuse_manifest=self.base_manifest, base_manifest_sha256=self.base_digest, **options)
            self.assertEqual(result["staging_mode"], "rollback")
            self.assertEqual(stage.validate_receipt(receipt)["rollback"], result["rollback"])
            for rel, data in self.base_files.items():
                if rel != "_headers":
                    self.assertEqual((output / rel).read_bytes(), data)
            headers = (output / "_headers").read_bytes()
            self.assertTrue(headers.startswith(self.base_files["_headers"]))
            self.assertIn(stage.ROLLBACK_RELAY.encode(), headers.split(b"/online/*", 1)[1])
            self.assertEqual(json.loads((output / "online/runtime/rollback.json").read_bytes()),
                             result["rollback"]["player"])
            with self.assertRaisesRegex(stage.StageError, "schema"):
                stage.audit_staging(self.source, "b" * 40, self.base, self.base_manifest,
                                    self.base_digest, output, result["manifest"])
            for rel in ("online/unauthorized.txt", "index.html"):
                with self.subTest(rel=rel):
                    path = output / rel
                    before = path.read_bytes() if path.exists() else None
                    path.write_bytes(b"untrusted")
                    manifest = json.loads(result["manifest"].read_bytes())
                    manifest["files"] = stage._records(stage._walk_files(output, "mutated package"))
                    result["manifest"].write_bytes(stage._canonical_json_bytes(manifest))
                    with self.assertRaisesRegex(stage.StageError, "exact overlay"):
                        stage.audit_staging(self.source, "b" * 40, self.base, self.base_manifest,
                                            self.base_digest, output, result["manifest"], **options)
                    if before is None:
                        path.unlink()
                    else:
                        path.write_bytes(before)

    def test_rollback_native_path_scan_allows_only_actual_virtual_home_context(self):
        virtual = b'FS.mkdir("/home/web_user");var env={PWD:"/",HOME:"/home/web_user",LANG:lang}'
        js = "gameplay_menu_browser.js"
        self.assertFalse(stage._rollback_native_has_local_path(js, virtual, self.source))
        for path in (b'/home/web_user', b'/home/web_user/secret', b'/home/developer',
                     b'/Users/developer', b'/Volumes/AgentStorage/private'):
            with self.subTest(path=path):
                self.assertTrue(stage._rollback_native_has_local_path(js, virtual + path, self.source))
        self.assertTrue(stage._rollback_native_has_local_path("gameplay_menu_browser.wasm",
                                                             virtual, self.source))

    def test_rollback_rejects_stale_producer_artifact_and_table(self):
        options = self._rollback_fixture()
        build, linked = options["rollback_build"], options["rollback_linked"]
        args = self.source, "b" * 40, self.base_files
        inputs = stage._rollback_inputs(build, linked, options["rollback_linked_receipt_sha256"])
        with self.assertRaisesRegex(stage.StageError, "exact staging producer"):
            stage._rollback_package(self.source, "c" * 40, self.base_files, inputs)
        for path, error in ((build / "gameplay_menu_browser.wasm", "artifact differs"),
                            (linked / "linked-statics.json", "statics differs")):
            with self.subTest(path=path.name):
                data = path.read_bytes()
                path.write_bytes(data + b" ")
                with self.assertRaisesRegex(stage.StageError, error):
                    stage._rollback_package(*args, inputs)
                path.write_bytes(data)

    def test_frozen_audio_overlay_preserves_distinct_source_and_backend(self):
        options = self._rollback_fixture()
        base_source = "a" * 40
        base_runtime = "1234567890abcdef"
        deployment_id = "11111111-2222-3333-4444-555555555555"
        origin = "https://11111111.webmelee-staging.pages.dev"
        proof = self.root / "audio-proof"
        proof.mkdir()
        backend = self.base.with_name(self.base.name + ".functions")
        backend.mkdir()
        routes = {"include": ["/api/diagnostics", "/api/diagnostics/*"], "exclude": []}
        backend_bytes = {"_routes.json": stage._canonical_json_bytes(routes),
                         "functions/api/diagnostics.js": b"frozen diagnostics"}
        for name, raw in backend_bytes.items():
            path = backend / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        graph_hash = _diagnostics_graph_hash(backend_bytes)
        identity = {"audio_policy": stage.FROZEN_AUDIO_POLICY, "artifacts": []}
        identity_sha = stage._sha256_bytes(stage._canonical_json_bytes(identity))
        manifest = {
            "schema": "melee-web-audio-player-package-v1", "profile": "audio-player", "project": "webmelee",
            "source_sha": base_source, "runtime_hash": base_runtime, "identity_sha256": identity_sha,
            "files": stage._records(self.base_files),
            "backend": {"directory": backend.name, "routes": routes, "graph_sha256": graph_hash,
                        "files": stage._records(backend_bytes)},
        }
        self.base_manifest.write_bytes(stage._canonical_json_bytes(manifest))
        digest = stage._sha256_bytes(self.base_manifest.read_bytes())
        pin = {"source_sha": base_source, "runtime_hash": base_runtime, "deployment_id": deployment_id,
               "manifest_sha256": digest, "backend_sha256": graph_hash}
        receipts = {
            "deployment-receipt.json": dict(pin, project=stage.STAGING_PROJECT, branch=stage.STAGING_BRANCH,
                                            immutable_url=origin),
            "immutable-http.json": {"origin": origin, "result": "pass", "manifest_sha256": digest},
            "functions-proof.json": {"validated": True},
            "runtime-audio-preview-identity.json": identity,
        }
        for name, data in receipts.items():
            (proof / name).write_bytes(stage._canonical_json_bytes(data))
        proof_pins = {name: stage._sha256_bytes((proof / name).read_bytes()) for name in receipts}
        output, receipt = self.root / "online-output", self.root / "online-receipt.json"
        with patch.object(stage, "FROZEN_AUDIO_BASE", pin), patch.object(stage, "FROZEN_AUDIO_PROOFS", proof_pins), \
                patch.object(stage, "_validate_source_sha"), patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit") as source_audit:
            result = stage.prepare_staging(
                self.source, "b" * 40, output, receipt, reuse_output=self.base, reuse_manifest=self.base_manifest,
                base_manifest_sha256=digest, frozen_audio_proof=proof, **options)
            source_audit.assert_not_called()
            overlay = json.loads(result["manifest"].read_bytes())
            self.assertEqual(overlay["source_sha"], base_source)
            self.assertEqual(overlay["rollback"]["player"]["sourceSha"], "b" * 40)
            self.assertEqual(overlay["backend"], manifest["backend"])
            self.assertEqual(stage.validate_receipt(receipt)["rollback"]["audio_base"], pin)
            (backend / "functions/api/diagnostics.js").write_bytes(b"unexpected Functions")
            with self.assertRaisesRegex(stage.StageError, "Functions bytes changed"):
                stage.audit_staging(self.source, "b" * 40, self.base, self.base_manifest, digest, output,
                                    result["manifest"], frozen_audio_proof=proof, **options)

    def test_reuse_inputs_are_all_or_none(self):
        output = self.root / "new-output"
        receipt = self.root / "new-receipt.json"
        with self.assertRaisesRegex(stage.StageError, "all-or-none"):
            stage.prepare_staging(
                self.source, "b" * 40, output, receipt,
                reuse_output=self.base,
            )
        self.assertFalse(output.exists())
        self.assertFalse(receipt.exists())

    def test_audit_staging_accepts_label_overlay_and_receipt(self):
        output, manifest, _ = self._write_overlay()
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"):
            result = stage.audit_staging(
                self.source, "b" * 40, self.base, self.base_manifest,
                self.base_digest, output, manifest, label_staging=True,
            )
        self.assertEqual(
            [item["path"] for item in result["changed_files"]],
            sorted(stage.OVERLAY_CHANGED_PATHS),
        )
        self.assertEqual(result["staging_mode"], "label")
        self.assertEqual(result["source_sha"], "b" * 40)
        self.assertEqual(result["runtime"]["hash"], "1234567890abcdef")
        receipt = stage._receipt_payload(result)
        self.assertEqual(stage.validate_receipt(receipt)["project"], stage.STAGING_PROJECT)

    def test_tampered_overlay_html_is_rejected_even_with_rewritten_inventory(self):
        output, manifest, _ = self._write_overlay()
        tampered = output / "index.html"
        tampered.write_bytes(tampered.read_bytes() + b"<!-- tampered -->\n")
        value = json.loads(manifest.read_text())
        files = stage._walk_files(output, "tampered output")
        value["files"] = stage._records(files)
        manifest.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"), \
                self.assertRaisesRegex(stage.StageError, "deterministic"):
            stage.audit_staging(
                self.source, "b" * 40, self.base, self.base_manifest,
                self.base_digest, output, manifest, label_staging=True,
            )

    def test_protected_runtime_and_links_are_rejected(self):
        output, manifest, _ = self._write_overlay()
        (output / "runtime/1234567890abcdef/gameplay_public.wasm").write_bytes(b"changed\n")
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"), \
                self.assertRaisesRegex(stage.StageError, "inventory"):
            stage.audit_staging(
                self.source, "b" * 40, self.base, self.base_manifest,
                self.base_digest, output, manifest, label_staging=True,
            )

        output2, manifest2, _ = self._write_overlay("overlay-extra")
        (output2 / "extra.bin").write_bytes(b"unexpected")
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"), \
                self.assertRaisesRegex(stage.StageError, "inventory"):
            stage.audit_staging(
                self.source, "b" * 40, self.base, self.base_manifest,
                self.base_digest, output2, manifest2, label_staging=True,
            )

        output4, manifest4, _ = self._write_overlay("overlay-dir")
        (output4 / "empty-directory").mkdir()
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"), \
                self.assertRaisesRegex(stage.StageError, "directory inventory"):
            stage.audit_staging(
                self.source, "b" * 40, self.base, self.base_manifest,
                self.base_digest, output4, manifest4, label_staging=True,
            )

        output3, manifest3, _ = self._write_overlay("overlay-link")
        (output3 / "linked-runtime").symlink_to(output3 / "index.html")
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"), \
                self.assertRaisesRegex(stage.StageError, "symlink"):
            stage.audit_staging(
                self.source, "b" * 40, self.base, self.base_manifest,
                self.base_digest, output3, manifest3, label_staging=True,
            )

    def test_exact_mode_preserves_base_bytes_and_rejects_tampering(self):
        output = self.root / "exact"
        receipt_path = self.root / "exact.receipt.json"
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"):
            result = stage.prepare_staging(
                self.source, "b" * 40, output, receipt_path,
                reuse_output=self.base, reuse_manifest=self.base_manifest,
                base_manifest_sha256=self.base_digest,
            )
        manifest = result["manifest"]
        self.assertEqual(result["staging_mode"], "exact")
        self.assertEqual(result["changed_files"], [])
        self.assertEqual(result["overlay_manifest_sha256"], self.base_digest)
        self.assertEqual(manifest.read_bytes(), self.base_manifest.read_bytes())
        self.assertEqual(stage._walk_files(output, "exact output"), self.base_files)
        receipt = stage._receipt_payload(result)
        self.assertEqual(receipt["staging_mode"], "exact")
        self.assertEqual(receipt["overlay_version"], stage.EXACT_VERSION)
        stage.validate_receipt(receipt, base_manifest_sha256=self.base_digest,
                                overlay_manifest_sha256=self.base_digest)

        (output / "licenses/runtime-third-party.txt").write_bytes(b"tampered\n")
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"), \
                self.assertRaisesRegex(stage.StageError, "inventory"):
            stage.audit_staging(
                self.source, "b" * 40, self.base, self.base_manifest,
                self.base_digest, output, manifest, label_staging=False,
            )

    def test_receipt_rejects_private_native_artifact_path(self):
        output, manifest, _ = self._write_overlay()
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_source_audit"):
            result = stage.audit_staging(
                self.source, "b" * 40, self.base, self.base_manifest,
                self.base_digest, output, manifest, label_staging=True,
            )
        receipt = stage._receipt_payload(result)
        receipt["native_artifacts"] = [{
            "path": "/Users/" + "example/private/native.wasm",
            "bytes": 1,
            "sha256": "b" * 64,
        }]
        with self.assertRaisesRegex(stage.StageError, "private path"):
            stage.validate_receipt(receipt)

    def test_failed_fresh_outputs_are_retained_for_diagnosis(self):
        generated_base = self.root / "generated-base"
        generated_base.mkdir()
        stage._write_package(dict(self.base_files), generated_base)
        generated_manifest = self.root / "generated-base.manifest.json"
        generated_manifest.write_bytes(self.base_manifest.read_bytes())
        output = self.root / "failed-output"
        receipt = self.root / "failed-receipt.json"
        with patch.object(stage, "_validate_source_sha"), \
                patch.object(stage, "_validate_pr15_guard"), \
                patch.object(stage, "_build_base", return_value=(generated_base, generated_manifest)), \
                patch.object(stage, "_source_audit", side_effect=[None, stage.StageError("forced failure")]), \
                self.assertRaisesRegex(stage.StageError, "forced failure"):
            stage.prepare_staging(self.source, "b" * 40, output, receipt)
        self.assertTrue((output / "index.html").is_file())
        self.assertTrue((output.with_name(output.name + ".manifest.json")).is_file())
        self.assertTrue((generated_base / "index.html").is_file())
        self.assertTrue(generated_manifest.is_file())


if __name__ == "__main__":
    unittest.main()
