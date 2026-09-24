import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


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
