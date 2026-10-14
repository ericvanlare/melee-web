"""Focused controls for the isolated Workers Static Assets room package."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from package_staging_room import PackageError, prepare_package  # noqa: E402


WORKER = "melee-web-room-relay-staging-288"
OLD_VERSION = "version-8fb9"
OLD_DEPLOYMENT = "deployment-824f"
VERSION = "version-2667"
DEPLOYMENT = "deployment-a61f"
NAMESPACE = "6644841b09974b46822753ce5cc49965"
ORIGIN = f"https://{WORKER}.mywebcomic.workers.dev"
WSS = f"wss://{WORKER}.mywebcomic.workers.dev"


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _write_json(path: Path, value: object) -> bytes:
    raw = (json.dumps(value, sort_keys=True, indent=2) + "\n").encode()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)
    return raw


class StagingRoomPackageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="staging-room-package-")
        self.base = Path(self.temp.name)
        self.source = self.base / "source"
        self.source.mkdir()
        (self.source / "build" / "browser-release").mkdir(parents=True)
        (self.source / "online" / "relay").mkdir(parents=True)
        (self.source / "tools").mkdir()
        (self.source / "docs" / "licenses").mkdir(parents=True)
        (self.source / "LICENSE").write_text("Melee Web license notice\n")
        (self.source / "LICENSE_SCOPE.md").write_text("Redistribution scope\n")
        (self.source / "docs" / "licenses" / "runtime-third-party.txt").write_text(
            "Runtime third-party notices\n")
        self.worker = b"export class RoomRelay {}\n"
        (self.source / "online" / "relay" / "worker.mjs").write_bytes(self.worker)
        self.artifacts = {
            "runtime.html": b"<!doctype html><title>runtime</title>\n",
            "runtime-development.mjs": b"export const room = true;\n",
            "staging-room.mjs": b"export function setup() {}\n",
            "staging-room.css": b".room { display: block; }\n",
        }
        names = list(self.artifacts)
        (self.source / "tools" / "browser_build_artifacts.json").write_text(
            json.dumps(names, indent=2) + "\n")
        for name, data in self.artifacts.items():
            (self.source / "build" / "browser-release" / name).write_bytes(data)

        self._git("init", "-q")
        self._git("config", "user.email", "test@example.invalid")
        self._git("config", "user.name", "Package fixture")
        self._git("add", "LICENSE", "LICENSE_SCOPE.md", "docs", "online", "tools")
        self._git("commit", "-qm", "fixture source")
        commit = self._git("rev-parse", "HEAD")
        tree = self._git("rev-parse", "HEAD^{tree}")

        self.config = {
            "name": WORKER,
            "main": "/historical/online/relay/worker.mjs",
            "compatibility_date": "2026-09-18",
            "durable_objects": {"bindings": [{"name": "ROOMS", "class_name": "RoomRelay"}]},
            "exports": {"RoomRelay": {"type": "durable-object", "storage": "sqlite"}},
            "vars": {"RELAY_ALLOWED_ORIGINS": "http://127.0.0.1:8787,http://localhost:8787"},
            "workers_dev": True,
            "preview_urls": False,
            "routes": [],
            "account_id": "f32b448c0131060ac901fa6f4ca1b0e1",
        }
        self.config_path = self.base / "provider-config.json"
        config_raw = _write_json(self.config_path, self.config)
        self.binding = {
            "schema": "staging-relay-live-deployment-binding-v1",
            "read_only": True,
            "worker_name": WORKER,
            "environment": "staging",
            "owner_plan": "Workers Free",
            "spend_limit_usd": 0,
            "workers_dev": True,
            "preview_urls": False,
            "routes": [],
            "namespace_id": NAMESPACE,
            "export": {"RoomRelay": {"type": "durable-object", "storage": "sqlite"}},
            "version_id": VERSION,
            "deployment_id": DEPLOYMENT,
            "endpoint": {"https": ORIGIN, "wss": WSS, "version": VERSION},
            "config_sha256": _sha(config_raw),
            "script_sha256": _sha(self.worker),
            "remote_etag_opaque": "etag-current",
            "allowed_origins": ["http://127.0.0.1:8787", "http://localhost:8787"],
        }
        self.binding_path = self.base / "baseline-binding.json"
        _write_json(self.binding_path, self.binding)
        self.version = {
            "id": VERSION,
            "resources": {
                "script": {"etag": "etag-current"},
                "script_runtime": {
                    "exports": self.config["exports"],
                    "compatibility_date": self.config["compatibility_date"],
                    "usage_model": "standard",
                },
                "bindings": [
                    {"name": "ROOMS", "type": "durable_object_namespace",
                     "class_name": "RoomRelay", "namespace_id": NAMESPACE},
                    {"name": "RELAY_ALLOWED_ORIGINS", "type": "plain_text",
                     "text": self.config["vars"]["RELAY_ALLOWED_ORIGINS"]},
                ],
            },
        }
        self.version_path = self.base / "live-version.json"
        _write_json(self.version_path, self.version)
        self.deployments = [
            {"id": OLD_DEPLOYMENT, "created_on": "2026-10-12T20:09:55.597522Z",
             "strategy": "percentage", "versions": [
                 {"version_id": OLD_VERSION, "percentage": 100}
             ]},
            {"id": DEPLOYMENT, "created_on": "2026-10-14T01:57:35.260554Z",
             "strategy": "percentage", "versions": [
                 {"version_id": VERSION, "percentage": 100}
             ]},
        ]
        self.deployments_path = self.base / "live-deployments.json"
        _write_json(self.deployments_path, self.deployments)
        source_inputs = {"files_sha256": {
            "online/relay/worker.mjs": _sha(self.worker),
            "tools/browser_build_artifacts.json": _sha(
                (self.source / "tools" / "browser_build_artifacts.json").read_bytes()),
        }}
        self.build_receipt_path = self.base / "build-receipt.json"
        _write_json(self.build_receipt_path, {
            "schema": "melee-web-two-computer-alpha-build-v1",
            "state": "PASS",
            "build_exit_code": 0,
            "source": {"commit": commit, "tree": tree, "clean": True},
            "source_unchanged": True,
            "source_inputs": source_inputs,
            "build": {"target": "runtime", "configuration": "Release",
                      "public_runtime": False, "artifact_directory": "build/browser-release"},
            "toolchain": {"name": "fixture"},
            "artifacts": [{"path": name, "bytes": len(data), "sha256": _sha(data)}
                           for name, data in self.artifacts.items()],
        })
        self.output = self.base / "package-output"

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _git(self, *args: str) -> str:
        return subprocess.run(["git", *args], cwd=self.source, check=True,
                              capture_output=True, text=True).stdout.strip()

    def _prepare(self) -> dict:
        return prepare_package(
            self.source, self.build_receipt_path, self.binding_path,
            self.version_path, self.deployments_path, self.config_path, self.output,
        )

    def test_packages_exact_ordinary_runtime_and_staging_policy(self) -> None:
        manifest = self._prepare()
        site = self.output / "site"
        self.assertEqual((site / "index.html").read_bytes(), self.artifacts["runtime.html"])
        self.assertEqual((site / "LICENSE").read_text(), "Melee Web license notice\n")
        self.assertEqual((self.output / "worker.mjs").read_bytes(), self.worker)
        config = json.loads((self.output / "wrangler.jsonc").read_text())
        self.assertEqual(config["name"], WORKER)
        self.assertEqual(config["workers_dev"], True)
        self.assertEqual(config["preview_urls"], False)
        self.assertEqual(config["routes"], [])
        self.assertEqual(config["assets"]["run_worker_first"], ["/v1/rooms/*"])
        self.assertEqual(config["durable_objects"], self.config["durable_objects"])
        self.assertEqual(config["vars"]["RELAY_ALLOWED_ORIGINS"],
                         "http://127.0.0.1:8787,http://localhost:8787," + ORIGIN)
        headers = (site / "_headers").read_text()
        self.assertIn("Cross-Origin-Opener-Policy: same-origin", headers)
        self.assertIn("Cross-Origin-Embedder-Policy: require-corp", headers)
        self.assertIn(f"connect-src 'self' {WSS}", headers)
        self.assertNotIn("connect-src *", headers)
        self.assertEqual(manifest["staging_worker"]["room_namespace_id"], NAMESPACE)
        self.assertEqual(manifest["staging_worker"]["allowed_origins_after"][-1], ORIGIN)
        history = manifest["staging_worker"]["deployment_history"]
        self.assertEqual([row["id"] for row in history], [OLD_DEPLOYMENT, DEPLOYMENT])
        self.assertEqual([row["selected"] for row in history], [False, True])
        self.assertEqual(manifest["package"]["file_count_excluding_manifest"], 12)

    def test_rejects_non_100_percent_or_wrong_live_version(self) -> None:
        self.deployments[1]["versions"][0]["percentage"] = 50
        _write_json(self.deployments_path, self.deployments)
        with self.assertRaisesRegex(PackageError, "100%"):
            self._prepare()

    def test_does_not_fall_back_to_historical_matching_deployment(self) -> None:
        self.deployments[1]["id"] = "newer-deployment-with-unbound-version"
        self.deployments[1]["versions"][0]["version_id"] = "newer-unbound-version"
        _write_json(self.deployments_path, self.deployments)
        with self.assertRaisesRegex(PackageError, "latest deployment id.*stale"):
            self._prepare()

    def test_rejects_ambiguous_or_malformed_latest_history(self) -> None:
        duplicate_latest = dict(self.deployments[1])
        duplicate_latest["id"] = "duplicate-latest-deployment"
        self.deployments.append(duplicate_latest)
        _write_json(self.deployments_path, self.deployments)
        with self.assertRaisesRegex(PackageError, "ambiguous newest"):
            self._prepare()

        self.deployments.pop()
        self.deployments[1]["created_on"] = "not-a-timestamp"
        _write_json(self.deployments_path, self.deployments)
        with self.assertRaisesRegex(PackageError, "malformed created_on"):
            self._prepare()

    def test_rejects_namespace_replacement(self) -> None:
        self.version["resources"]["bindings"][0]["namespace_id"] = "0" * 32
        _write_json(self.version_path, self.version)
        with self.assertRaisesRegex(PackageError, "retained RoomRelay namespace"):
            self._prepare()

    def test_rejects_artifact_receipt_mismatch(self) -> None:
        artifact = self.source / "build" / "browser-release" / "runtime.html"
        artifact.write_bytes(b"tampered\n")
        with self.assertRaisesRegex(PackageError, "differs from build receipt"):
            self._prepare()

    def test_rejects_symlink_runtime_artifact(self) -> None:
        artifact = self.source / "build" / "browser-release" / "runtime.html"
        artifact.unlink()
        artifact.symlink_to(self.base / "outside")
        with self.assertRaisesRegex(PackageError, "symlink"):
            self._prepare()

    def test_rejects_unexpected_build_artifact(self) -> None:
        receipt = json.loads(self.build_receipt_path.read_text())
        receipt["artifacts"].append({"path": "extra.data", "bytes": 1, "sha256": _sha(b"x")})
        _write_json(self.build_receipt_path, receipt)
        with self.assertRaisesRegex(PackageError, "inventory mismatch"):
            self._prepare()

    def test_rejects_dirty_tracked_source_and_existing_output(self) -> None:
        (self.source / "LICENSE").write_text("changed\n")
        with self.assertRaisesRegex(PackageError, "tracked source is dirty"):
            self._prepare()
        self.output.mkdir()
        with self.assertRaisesRegex(PackageError, "new directory"):
            self._prepare()


if __name__ == "__main__":
    unittest.main()
