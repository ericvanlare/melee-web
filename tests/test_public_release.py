"""Focused fail-closed tests for the static public shell release boundary."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from audit_public import AuditError, audit  # noqa: E402
from build_public import (  # noqa: E402
    BuildError,
    DEFAULT_SOURCE,
    MAX_FILE_BYTES,
    SOURCE_ALLOWLIST,
    build,
)


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

    def test_preview_build_is_auditable_and_uses_hashed_assets(self):
        output, manifest = self.paths()
        result = audit(output, manifest)
        self.assertEqual(result["mode"], "preview")
        self.assertIn("DRAFT PREVIEW", (output / "index.html").read_text())
        self.assertEqual(len(list((output / "assets").iterdir())), 2)
        self.assertFalse((output / "_redirects").exists())

    def test_production_requires_explicit_operator_and_contact(self):
        with self.assertRaisesRegex(BuildError, "requires explicit"):
            build(self.source, self.root / "missing-config", "production")
        output, manifest = self.paths("production")
        self.assertEqual(audit(output, manifest)["mode"], "production")
        self.assertNotIn("DRAFT PREVIEW", (output / "index.html").read_text())
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
