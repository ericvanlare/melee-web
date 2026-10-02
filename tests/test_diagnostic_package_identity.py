"""Focused checks for the safe identity embedded in public player HTML."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("build_public", ROOT / "scripts" / "build_public.py")
public = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(public)


class DiagnosticPackageIdentityTests(unittest.TestCase):
    def setUp(self):
        self.identity = public._diagnostic_identity(
            source_commit="a" * 40,
            runtime_hash="b" * 16,
            build_profile="player",
        )
        self.template = (
            b'<!doctype html><html lang="en"><head><title>WebMelee</title>'
            b'<link rel="stylesheet" href="{{STYLE_URL}}">'
            b'<script type="module" src="{{SCRIPT_URL}}"></script></head>'
            b'<body>{{OPERATOR_NAME}} {{CONTACT_EMAIL}}</body></html>'
        )

    def test_identity_is_exact_and_source_bound(self):
        rendered = public._replace_html(
            self.template, "Release Operator", "rights@example.test",
            "/runtime/bbbbbbbbbbbbbbbb/player/player.css",
            "/runtime/bbbbbbbbbbbbbbbb/player/player-shell.mjs", "production",
            self.identity,
        )
        self.assertEqual(public._read_diagnostic_meta(rendered), self.identity)
        self.assertIn(b'id="runtime-diagnostic-identity"', rendered)
        self.assertNotIn(b"/Users/", rendered)
        self.assertNotIn(b"release-manifest", rendered)
        self.assertNotIn(b"data-environment", rendered.split(b'id="runtime-diagnostic-identity"', 1)[1].split(b">", 1)[0])

    def test_identity_tampering_is_rejected(self):
        rendered = public._replace_html(
            self.template, "Release Operator", "rights@example.test", "/style.css", "/player.mjs",
            "production", self.identity,
        )
        marker = b'&quot;runtime_hash&quot;:&quot;' + b"b" * 16 + b'&quot;'
        tampered = rendered.replace(marker, b'&quot;runtime_hash&quot;:&quot;' + b"c" * 16 + b'&quot;')
        self.assertNotEqual(public._read_diagnostic_meta(tampered), self.identity)

        extra = rendered.replace(
            b'&quot;schema_version&quot;:1,',
            b'&quot;extra&quot;:true,&quot;schema_version&quot;:1,',
        )
        with self.assertRaisesRegex(ValueError, "unexpected or missing"):
            public._read_diagnostic_meta(extra)

        malformed = rendered.replace(b'&quot;schema_version&quot;:1', b'&quot;schema_version&quot;:2')
        with self.assertRaisesRegex(ValueError, "schema version"):
            public._read_diagnostic_meta(malformed)

        duplicate = rendered.replace(
            b"</head>",
            b'<meta content="{}" id="runtime-diagnostic-identity"></head>',
        )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            public._read_diagnostic_meta(duplicate)

        malformed_attributes = rendered.replace(
            b' id="runtime-diagnostic-identity"',
            b' id="runtime-diagnostic-identity" id="runtime-diagnostic-identity"',
        )
        with self.assertRaisesRegex(ValueError, "malformed"):
            public._read_diagnostic_meta(malformed_attributes)

        with self.assertRaisesRegex(ValueError, "allowed public profile"):
            public._diagnostic_identity(
                source_commit="a" * 40, runtime_hash="b" * 16,
                build_profile=[],
            )

    def test_diagnostic_module_is_part_of_the_graph_hash(self):
        self.assertIn("runtime-diagnostics.mjs", public.PLAYER_SOURCE_RUNTIME_FILES)
        base = {"gameplay_public.js": b"loader", "runtime-diagnostics.mjs": b"diagnostics"}
        changed = dict(base, **{"runtime-diagnostics.mjs": b"diagnostics changed"})
        self.assertNotEqual(public._runtime_graph_hash(base), public._runtime_graph_hash(changed))

    def test_delivery_settings_and_shared_schema_are_in_the_audited_graph(self):
        expected = {
            'runtime-diagnostics-delivery.mjs',
            'diagnostics-settings.mjs',
            'diagnostics-schema.mjs',
        }
        self.assertTrue(expected.issubset(public.PLAYER_RUNTIME_FILES))
        self.assertTrue(expected.issubset(public.PLAYER_SOURCE_RUNTIME_FILES))
        self.assertTrue({f'web/{name}' for name in expected}.issubset(public.RUNTIME_SOURCE_FILES))
        self.assertEqual(
            (ROOT / 'web/diagnostics-schema.mjs').read_text().rstrip() + '\\n',
            (ROOT / 'diagnostics/schema.mjs').read_text().rstrip() + '\\n',
        )
        delivery = (ROOT / 'web/runtime-diagnostics-delivery.mjs').read_text()
        self.assertIn("from './diagnostics-schema.mjs'", delivery)
        self.assertNotIn("from '../diagnostics/schema.mjs'", delivery)
        cmake = (ROOT / 'cmake/FighterRuntime.cmake').read_text()
        for name in expected:
            self.assertIn(name.removesuffix('.mjs'), cmake)

    def test_commit_identity_rejects_modified_or_untracked_runtime_source(self):
        with tempfile.TemporaryDirectory(prefix="diagnostic-source-identity-") as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "web").mkdir()
            source = root / "web/runtime.mjs"
            source.write_text("export const version = 1;\n")
            subprocess.run(["git", "add", "web"], cwd=root, check=True)
            subprocess.run(["git", "-c", "user.name=Test", "-c", "user.email=test@example.test",
                            "commit", "-q", "-m", "fixture"], cwd=root, check=True)
            commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
            with patch.object(public, "ROOT", root):
                self.assertEqual(public._source_sha(), commit)
                source.write_text("export const version = 2;\n")
                with self.assertRaisesRegex(ValueError, "commit source changes"):
                    public._source_sha()
                source.write_text("export const version = 1;\n")
                (root / "web/untracked.mjs").write_text("export const untracked = true;\n")
                with self.assertRaisesRegex(ValueError, "uncommitted runtime source"):
                    public._source_sha()

    def test_missing_checkout_cannot_substitute_a_fingerprint_for_a_commit(self):
        with tempfile.TemporaryDirectory(prefix="diagnostic-no-git-") as temporary:
            with patch.object(public, "ROOT", Path(temporary)):
                with self.assertRaisesRegex(ValueError, "committed source checkout"):
                    public._source_sha()


if __name__ == "__main__":
    unittest.main()
