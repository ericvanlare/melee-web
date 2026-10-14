#!/usr/bin/env python3
"""Prepare a source-bound static room UI on the existing staging relay Worker.

This is intentionally separate from the public Pages player packager.  It
accepts only an ordinary, non-public Release runtime and a read-only receipt
for the existing Workers-Free staging relay, then writes an external Worker
deployment directory.  It never deploys anything.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any
from urllib.parse import urlsplit


PACKAGE_SCHEMA = "melee-web-staging-room-package-v1"
BUILD_SCHEMA = "melee-web-two-computer-alpha-build-v1"
BINDING_SCHEMA = "staging-relay-live-deployment-binding-v1"
STAGING_WORKER = "melee-web-room-relay-staging-288"
ROOM_CLASS = "RoomRelay"
ROOM_BINDING = "ROOMS"
ORIGIN_VAR = "RELAY_ALLOWED_ORIGINS"
ASSET_ROUTE = "/v1/rooms/*"
MAX_STATIC_FILE_BYTES = 25 * 1024 * 1024
MAX_STATIC_ASSETS = 20_000
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
ACCOUNT_ID_RE = re.compile(r"^[0-9a-f]{32}$")


class PackageError(ValueError):
    """Fail-closed staging package error suitable for command-line output."""


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise PackageError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path, label: str) -> str:
    _regular_file(path, label)
    try:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError as exc:
        raise PackageError(f"cannot hash {label}: {exc}") from exc


def _regular_file(path: Path, label: str) -> None:
    if path.is_symlink() or not path.is_file():
        raise PackageError(f"{label} must be a regular non-symlink file: {path}")


def _directory(path: Path, label: str) -> None:
    if path.is_symlink() or not path.is_dir():
        raise PackageError(f"{label} must be a real directory: {path}")


def _canonical_rel(value: object, label: str) -> str:
    if not isinstance(value, str) or not value or "\x00" in value or "\\" in value:
        raise PackageError(f"{label} must be a canonical relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or path.as_posix() != value or any(part in ("", ".", "..") for part in path.parts):
        raise PackageError(f"{label} is not a safe relative path: {value!r}")
    return value


def _load_json_value(path: Path, label: str) -> tuple[Any, bytes]:
    _regular_file(path, label)
    try:
        raw = path.read_bytes()
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_unique_object)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, PackageError) as exc:
        raise PackageError(f"{label} is not valid JSON: {exc}") from exc
    return value, raw


def _load_json(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    value, raw = _load_json_value(path, label)
    if not isinstance(value, dict):
        raise PackageError(f"{label} must contain a JSON object")
    return value, raw


def _json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _git(root: Path, *args: str) -> str:
    try:
        result = subprocess.run(["git", *args], cwd=root, check=True, text=True,
                                capture_output=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise PackageError(f"cannot inspect source identity with git {args[0]}") from exc
    return result.stdout.strip()


def _source_identity(root: Path, receipt: dict[str, Any]) -> dict[str, str]:
    _directory(root, "source root")
    expected = receipt.get("source")
    if not isinstance(expected, dict) or expected.get("clean") is not True:
        raise PackageError("build receipt does not identify a clean source tree")
    head = _git(root, "rev-parse", "HEAD")
    tree = _git(root, "rev-parse", "HEAD^{tree}")
    if expected.get("commit") != head or expected.get("tree") != tree:
        raise PackageError("build receipt source commit/tree does not match this checkout")
    # Keep known task-owned untracked evidence out of the runtime source check,
    # while refusing any staged or unstaged tracked-source change.
    if _git(root, "status", "--porcelain=v1", "--untracked-files=no"):
        raise PackageError("tracked source is dirty; refusing staging package")
    if receipt.get("source_unchanged") is not True:
        raise PackageError("build receipt does not prove source remained unchanged")
    return {"commit": head, "tree": tree}


def _validate_source_inputs(root: Path, receipt: dict[str, Any], worker_sha: str) -> tuple[int, str]:
    source_inputs = receipt.get("source_inputs")
    hashes = source_inputs.get("files_sha256") if isinstance(source_inputs, dict) else None
    if not isinstance(hashes, dict) or not hashes:
        raise PackageError("build receipt has no source input hash map")
    for rel, digest in hashes.items():
        rel = _canonical_rel(rel, "source input path")
        if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
            raise PackageError(f"invalid source input hash for {rel}")
        path = root / rel
        _regular_file(path, f"tracked source input {rel}")
        try:
            _git(root, "ls-files", "--error-unmatch", "--", rel)
        except PackageError as exc:
            raise PackageError(f"source input is not tracked by git: {rel}") from exc
        if _sha256_file(path, rel) != digest:
            raise PackageError(f"source input differs from build receipt: {rel}")
    if hashes.get("online/relay/worker.mjs") != worker_sha:
        raise PackageError("build receipt does not bind the unchanged staging Worker source")
    return len(hashes), _sha256_bytes(_json_bytes(hashes))


def _read_inventory(root: Path) -> list[str]:
    path = root / "tools" / "browser_build_artifacts.json"
    _regular_file(path, "browser artifact inventory")
    try:
        names = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_object)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, PackageError) as exc:
        raise PackageError(f"browser artifact inventory is invalid: {exc}") from exc
    if not isinstance(names, list) or not names:
        raise PackageError("browser artifact inventory must be a nonempty file-name list")
    for name in names:
        if _canonical_rel(name, "browser artifact name") != name or "/" in name or ":" in name:
            raise PackageError(f"browser artifact must be a single file name: {name!r}")
    if len(set(names)) != len(names):
        raise PackageError("browser artifact inventory contains duplicate names")
    return names


def _origin(value: object, label: str, scheme: str | None = None) -> str:
    if not isinstance(value, str) or not value or value != value.strip():
        raise PackageError(f"{label} must be a URL origin")
    parsed = urlsplit(value)
    if (not parsed.scheme or not parsed.netloc or parsed.path not in ("", "/") or
            parsed.query or parsed.fragment or parsed.username or parsed.password or
            (scheme is not None and parsed.scheme != scheme)):
        raise PackageError(f"{label} must be a plain {scheme or 'HTTP'} origin")
    normalized = f"{parsed.scheme}://{parsed.netloc}"
    if normalized != value:
        raise PackageError(f"{label} is not in canonical origin form")
    return value


def _validate_provider_inputs(binding: dict[str, Any], version: dict[str, Any],
                              deployments: object, config: dict[str, Any],
                              worker_sha: str, config_raw: bytes,
                              version_raw: bytes, deployments_raw: bytes) -> dict[str, Any]:
    if binding.get("schema") != BINDING_SCHEMA or binding.get("read_only") is not True:
        raise PackageError("baseline binding must be a read-only staging relay receipt")
    if binding.get("worker_name") != STAGING_WORKER or binding.get("environment") != "staging":
        raise PackageError("baseline binding is not for the isolated staging relay")
    if (binding.get("owner_plan") != "Workers Free" or
            binding.get("spend_limit_usd") != 0):
        raise PackageError("staging relay is not bound to the approved Workers Free/$0 limit")
    if (binding.get("workers_dev") is not True or binding.get("preview_urls") is not False or
            binding.get("routes") != []):
        raise PackageError("staging relay must remain workers.dev-only, without routes or preview URLs")
    namespace = binding.get("namespace_id")
    if not isinstance(namespace, str) or not re.fullmatch(r"[0-9a-f]{32}", namespace):
        raise PackageError("provider binding has an invalid Durable Object namespace id")
    expected_export = {ROOM_CLASS: {"type": "durable-object", "storage": "sqlite"}}
    if binding.get("export") != expected_export:
        raise PackageError("provider binding does not identify the existing SQLite RoomRelay export")
    if not isinstance(binding.get("version_id"), str) or not binding["version_id"]:
        raise PackageError("baseline binding has no verified version id")
    if not isinstance(binding.get("deployment_id"), str) or not binding["deployment_id"]:
        raise PackageError("baseline binding has no verified deployment id")
    endpoint = binding.get("endpoint")
    if not isinstance(endpoint, dict):
        raise PackageError("provider binding has no workers.dev endpoint")
    ui_origin = _origin(endpoint.get("https"), "staging HTTPS endpoint", "https")
    relay_origin = _origin(endpoint.get("wss"), "staging WebSocket endpoint", "wss")
    if ui_origin.removeprefix("https://") != relay_origin.removeprefix("wss://"):
        raise PackageError("staging UI and WSS endpoint must share one workers.dev host")
    if endpoint.get("version") != binding.get("version_id"):
        raise PackageError("provider endpoint version differs from active deployment version")
    if binding.get("config_sha256") != _sha256_bytes(config_raw):
        raise PackageError("baseline binding does not bind the supplied staging Wrangler config")
    if binding.get("script_sha256") != worker_sha:
        raise PackageError("baseline binding does not bind the unchanged Worker source")

    config_origins = config.get("vars", {}).get(ORIGIN_VAR) if isinstance(config.get("vars"), dict) else None
    if not isinstance(config_origins, str):
        raise PackageError("staging Wrangler config has no relay origin allowlist")
    configured_origins = config_origins.split(",")
    allowed_origins = binding.get("allowed_origins")
    if (not isinstance(allowed_origins, list) or not allowed_origins or
            configured_origins != allowed_origins or len(set(allowed_origins)) != len(allowed_origins)):
        raise PackageError("baseline origin receipt does not match the supplied staging config")
    for item in allowed_origins:
        _origin(item, "allowed relay origin")

    if (config.get("name") != STAGING_WORKER or
            config.get("workers_dev") is not True or config.get("preview_urls") is not False or
            config.get("routes") != []):
        raise PackageError("supplied Wrangler config changes the isolated staging endpoint")
    configured_main = config.get("main")
    if not isinstance(configured_main, str) or Path(configured_main).name != "worker.mjs":
        raise PackageError("baseline Wrangler config does not identify worker.mjs")
    account_id = config.get("account_id")
    if not isinstance(account_id, str) or not ACCOUNT_ID_RE.fullmatch(account_id):
        raise PackageError("staging Wrangler config has an invalid account id")
    if config.get("exports") != expected_export:
        raise PackageError("staging Wrangler config changes the existing SQLite Durable Object export")
    durable = config.get("durable_objects")
    expected_binding = [{"name": ROOM_BINDING, "class_name": ROOM_CLASS}]
    if not isinstance(durable, dict) or durable.get("bindings") != expected_binding:
        raise PackageError("staging Wrangler config changes the existing Durable Object binding")
    if "migrations" in config:
        raise PackageError("staging Wrangler config must not add a Durable Object migration")
    if not isinstance(config.get("compatibility_date"), str):
        raise PackageError("staging Wrangler config has no compatibility date")

    if version.get("id") != binding["version_id"]:
        raise PackageError("live deployed version differs from the retained baseline version")
    if version.get("id") != binding.get("endpoint", {}).get("version"):
        raise PackageError("live version differs from the endpoint version in the baseline binding")
    if version.get("id") == "":
        raise PackageError("deployed-version receipt has an empty version id")
    if not isinstance(deployments, list) or len(deployments) != 1:
        raise PackageError("live deployment query must contain exactly the staging Worker")
    deployment = deployments[0]
    if not isinstance(deployment, dict) or deployment.get("id") != binding["deployment_id"]:
        raise PackageError("live deployment differs from the retained staging deployment")
    active_versions = deployment.get("versions")
    if (not isinstance(active_versions, list) or len(active_versions) != 1 or
            not isinstance(active_versions[0], dict) or
            active_versions[0].get("version_id") != version.get("id") or
            active_versions[0].get("percentage") != 100):
        raise PackageError("staging deployment is not 100% on the supplied live version")
    resources = version.get("resources")
    if not isinstance(resources, dict):
        raise PackageError("deployed-version receipt has no resources")
    runtime = resources.get("script_runtime")
    if (not isinstance(runtime, dict) or runtime.get("exports") != expected_export or
            runtime.get("compatibility_date") != config.get("compatibility_date")):
        raise PackageError("deployed Worker runtime/export differs from staging config")
    script = resources.get("script")
    if not isinstance(script, dict) or script.get("etag") != binding.get("remote_etag_opaque"):
        raise PackageError("live version script identity differs from the baseline binding")
    bindings = resources.get("bindings")
    if not isinstance(bindings, list):
        raise PackageError("deployed-version receipt has no bindings")
    do_rows = [item for item in bindings if isinstance(item, dict) and item.get("name") == ROOM_BINDING]
    origin_rows = [item for item in bindings if isinstance(item, dict) and item.get("name") == ORIGIN_VAR]
    if (len(do_rows) != 1 or do_rows[0].get("type") != "durable_object_namespace" or
            do_rows[0].get("class_name") != ROOM_CLASS or do_rows[0].get("namespace_id") != namespace):
        raise PackageError("deployed Worker is not bound to the retained RoomRelay namespace")
    if (len(origin_rows) != 1 or origin_rows[0].get("type") != "plain_text" or
            origin_rows[0].get("text") != config_origins):
        raise PackageError("deployed Worker origin allowlist differs from the staging config")

    merged_origins = list(allowed_origins)
    if ui_origin not in merged_origins:
        merged_origins.append(ui_origin)
    return {
        "worker_name": STAGING_WORKER,
        "ui_origin": ui_origin,
        "relay_origin": relay_origin,
        "account_id": account_id,
        "allowed_origins_before": allowed_origins,
        "allowed_origins_after": merged_origins,
        "namespace_id": namespace,
        "deployment_id": binding["deployment_id"],
        "version_id": binding["version_id"],
        "compatibility_date": config["compatibility_date"],
        "usage_model": runtime.get("usage_model"),
        "owner_plan": binding["owner_plan"],
        "spend_limit_usd": binding["spend_limit_usd"],
        "binding_sha256": _sha256_bytes(_json_bytes(binding)),
        "deployed_version_sha256": _sha256_bytes(_json_bytes(version)),
        "deployed_version_raw_sha256": _sha256_bytes(version_raw),
        "deployments_raw_sha256": _sha256_bytes(deployments_raw),
        "source_config_sha256": _sha256_bytes(config_raw),
    }


def _headers(ui_origin: str) -> bytes:
    host = ui_origin.removeprefix("https://")
    csp = (
        "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; "
        "style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
        f"connect-src 'self' wss://{host}; font-src 'self'; object-src 'none'; "
        "base-uri 'none'; form-action 'none'; frame-ancestors 'none'; "
        "worker-src 'self' blob:; media-src 'self' blob:; manifest-src 'none'"
    )
    lines = [
        "/*",
        f"  Content-Security-Policy: {csp}",
        "  Cross-Origin-Opener-Policy: same-origin",
        "  Cross-Origin-Embedder-Policy: require-corp",
        "  Cross-Origin-Resource-Policy: same-origin",
        "  X-Content-Type-Options: nosniff",
        "  Referrer-Policy: no-referrer",
        "  X-Frame-Options: DENY",
        "  X-Robots-Tag: noindex, nofollow, noarchive",
        "  Permissions-Policy: fullscreen=(self), camera=(), microphone=(), geolocation=(), payment=(), usb=()",
        "/licenses/*",
        "  Cache-Control: public, max-age=0, must-revalidate",
    ]
    return ("\n".join(lines) + "\n").encode("utf-8")


def _artifact_records(root: Path, receipt: dict[str, Any], names: list[str]) -> tuple[Path, list[dict[str, Any]]]:
    build = receipt.get("build")
    if (not isinstance(build, dict) or build.get("target") != "runtime" or
            build.get("configuration") != "Release" or build.get("public_runtime") is not False or
            build.get("artifact_directory") != "build/browser-release"):
        raise PackageError("build receipt is not the ordinary Release runtime producer")
    build_root = root / "build" / "browser-release"
    _directory(build_root, "ordinary runtime build directory")
    raw_rows = receipt.get("artifacts")
    if not isinstance(raw_rows, list):
        raise PackageError("build receipt artifacts must be a list")
    rows: dict[str, dict[str, Any]] = {}
    for row in raw_rows:
        if not isinstance(row, dict) or set(row) != {"path", "bytes", "sha256"}:
            raise PackageError("build artifact records must contain path, bytes and sha256")
        name = _canonical_rel(row.get("path"), "build artifact path")
        if "/" in name or ":" in name or name in rows:
            raise PackageError(f"invalid or duplicate build artifact path: {name!r}")
        size, digest = row.get("bytes"), row.get("sha256")
        if type(size) is not int or size < 1 or not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
            raise PackageError(f"invalid build artifact record for {name}")
        rows[name] = row
    if set(rows) != set(names):
        missing, extra = sorted(set(names) - set(rows)), sorted(set(rows) - set(names))
        raise PackageError(f"build artifact inventory mismatch (missing={missing}, extra={extra})")
    result: list[dict[str, Any]] = []
    for name in names:
        path = build_root / name
        if path.is_symlink() or not path.is_file():
            raise PackageError(f"runtime artifact is missing or a symlink: {name}")
        row = rows[name]
        size = path.stat().st_size
        if size != row["bytes"] or _sha256_file(path, name) != row["sha256"]:
            raise PackageError(f"runtime artifact differs from build receipt: {name}")
        if size > MAX_STATIC_FILE_BYTES:
            raise PackageError(f"runtime artifact exceeds Workers Static Assets 25 MiB limit: {name}")
        result.append({"path": name, "bytes": size, "sha256": row["sha256"]})
    return build_root, result


def _copy_checked(source: Path, destination: Path, label: str) -> dict[str, Any]:
    digest = _sha256_file(source, label)
    size = source.stat().st_size
    if size > MAX_STATIC_FILE_BYTES:
        raise PackageError(f"static asset exceeds Workers Static Assets 25 MiB limit: {label}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    if destination.stat().st_size != size or _sha256_file(destination, f"copied {label}") != digest:
        raise PackageError(f"copied package file differs from validated source: {label}")
    return {"path": destination.as_posix(), "bytes": size, "sha256": digest}


def _file_record(path: Path, rel: str) -> dict[str, Any]:
    return {"path": rel, "bytes": path.stat().st_size, "sha256": _sha256_file(path, rel)}


def _verify_tree(package_root: Path, expected_paths: set[str]) -> list[dict[str, Any]]:
    actual: set[str] = set()
    for path in package_root.rglob("*"):
        if path.is_symlink():
            raise PackageError(f"package contains a symlink: {path.relative_to(package_root)}")
        if path.is_file():
            if path.stat().st_size > MAX_STATIC_FILE_BYTES:
                raise PackageError(f"package file exceeds Workers Static Assets 25 MiB limit: {path.relative_to(package_root)}")
            actual.add(path.relative_to(package_root).as_posix())
        elif not path.is_dir():
            raise PackageError(f"package contains a non-file entry: {path.relative_to(package_root)}")
    if actual != expected_paths:
        raise PackageError(f"package file inventory mismatch (missing={sorted(expected_paths-actual)}, "
                           f"extra={sorted(actual-expected_paths)})")
    rows = [_file_record(package_root / rel, rel) for rel in sorted(actual)]
    if len(rows) > MAX_STATIC_ASSETS:
        raise PackageError("staging package exceeds Workers Free static asset count")
    return rows


def prepare_package(source_root: Path, build_receipt_path: Path,
                    binding_receipt_path: Path, deployed_version_path: Path,
                    deployments_path: Path, staging_config_path: Path,
                    output_path: Path) -> dict[str, Any]:
    if source_root.is_symlink():
        raise PackageError("source root must not be a symlink")
    root = source_root.resolve()
    output = output_path.resolve(strict=False)
    if output.exists() or output.is_symlink():
        raise PackageError("package output must be a new directory")
    if not output.parent.is_dir() or output.is_relative_to(root):
        raise PackageError("package output parent must exist and output must be outside the source checkout")

    receipt, receipt_raw = _load_json(build_receipt_path, "ordinary runtime build receipt")
    if receipt.get("schema") != BUILD_SCHEMA or receipt.get("state") != "PASS" or receipt.get("build_exit_code") != 0:
        raise PackageError("build receipt is not a successful ordinary runtime build")
    source = _source_identity(root, receipt)
    binding, binding_raw = _load_json(binding_receipt_path, "read-only staging binding receipt")
    version, version_raw = _load_json(deployed_version_path, "read-only deployed Worker version")
    deployments, deployments_raw = _load_json_value(deployments_path, "read-only staging deployments")
    config, config_raw = _load_json(staging_config_path, "staging Worker Wrangler config")

    worker_path = root / "online" / "relay" / "worker.mjs"
    worker_sha = _sha256_file(worker_path, "staging relay Worker source")
    provider = _validate_provider_inputs(binding, version, deployments, config, worker_sha,
                                         config_raw, version_raw, deployments_raw)
    input_count, input_hash = _validate_source_inputs(root, receipt, worker_sha)
    names = _read_inventory(root)
    build_root, artifact_rows = _artifact_records(root, receipt, names)

    temp = Path(tempfile.mkdtemp(prefix=f".{output.name}.staging-", dir=output.parent))
    try:
        site = temp / "site"
        site.mkdir()
        for row in artifact_rows:
            copied = _copy_checked(build_root / row["path"], site / row["path"], row["path"])
            if copied["bytes"] != row["bytes"] or copied["sha256"] != row["sha256"]:
                raise PackageError(f"copied runtime artifact differs from build receipt: {row['path']}")
        runtime_html = site / "runtime.html"
        if not runtime_html.is_file():
            raise PackageError("runtime artifact inventory has no runtime.html entry")
        shutil.copyfile(runtime_html, site / "index.html")
        _copy_checked(root / "LICENSE", site / "LICENSE", "LICENSE")
        _copy_checked(root / "LICENSE_SCOPE.md", site / "LICENSE_SCOPE.md", "LICENSE_SCOPE.md")
        _copy_checked(root / "docs" / "licenses" / "runtime-third-party.txt",
                      site / "licenses" / "runtime-third-party.txt", "runtime-third-party.txt")
        (site / "_headers").write_bytes(_headers(provider["ui_origin"]))
        (site / "robots.txt").write_text("User-agent: *\nDisallow: /\n", encoding="utf-8")

        copied_worker = _copy_checked(worker_path, temp / "worker.mjs", "worker.mjs")
        if copied_worker["sha256"] != worker_sha:
            raise PackageError("copied Worker differs from validated source")
        new_config = {
            "name": STAGING_WORKER,
            "main": "./worker.mjs",
            "compatibility_date": provider["compatibility_date"],
            "durable_objects": config["durable_objects"],
            "exports": config["exports"],
            "vars": {**config["vars"], ORIGIN_VAR: ",".join(provider["allowed_origins_after"])},
            "workers_dev": True,
            "preview_urls": False,
            "routes": [],
            "account_id": provider["account_id"],
            "assets": {"directory": "./site", "run_worker_first": [ASSET_ROUTE]},
        }
        (temp / "wrangler.jsonc").write_bytes(_json_bytes(new_config))

        expected_site = set(names) | {
            "index.html", "LICENSE", "LICENSE_SCOPE.md", "licenses/runtime-third-party.txt",
            "_headers", "robots.txt",
        }
        _verify_tree(site, expected_site)
        expected_root = {f"site/{rel}" for rel in expected_site} | {"worker.mjs", "wrangler.jsonc"}
        file_rows = _verify_tree(temp, expected_root)
        artifacts_bytes = sum(row["bytes"] for row in artifact_rows)
        manifest = {
            "schema": PACKAGE_SCHEMA,
            "source": {**source, "clean_tracked": True,
                       "source_inputs_count": input_count, "source_inputs_sha256": input_hash,
                       "worker_source_sha256": worker_sha},
            "build": {
                "receipt_sha256": _sha256_bytes(receipt_raw),
                "target": "runtime", "configuration": "Release", "public_runtime": False,
                "artifact_directory": "build/browser-release",
                "artifact_count": len(artifact_rows), "artifact_bytes": artifacts_bytes,
                "artifacts": artifact_rows,
                "toolchain": receipt.get("toolchain"),
            },
            "staging_worker": {
                "name": provider["worker_name"], "ui_origin": provider["ui_origin"],
                "relay_origin": provider["relay_origin"], "account_id_sha256": _sha256_bytes(
                    provider["account_id"].encode("ascii")),
                "owner_plan": provider["owner_plan"], "spend_limit_usd": provider["spend_limit_usd"],
                "workers_dev": True, "preview_urls": False, "routes": [],
                "deployment_id": provider["deployment_id"], "prior_version_id": provider["version_id"],
                "room_class": ROOM_CLASS, "room_binding": ROOM_BINDING,
                "room_namespace_id": provider["namespace_id"], "usage_model": provider["usage_model"],
                "compatibility_date": provider["compatibility_date"],
                "allowed_origins_before": provider["allowed_origins_before"],
                "allowed_origins_after": provider["allowed_origins_after"],
                "binding_receipt_sha256": provider["binding_sha256"],
                "deployed_version_receipt_sha256": provider["deployed_version_sha256"],
                "deployed_version_raw_sha256": provider["deployed_version_raw_sha256"],
                "deployments_raw_sha256": provider["deployments_raw_sha256"],
                "source_config_sha256": provider["source_config_sha256"],
                "static_asset_route_policy": {"run_worker_first": [ASSET_ROUTE],
                                               "spa_fallback": False},
            },
            "package": {"file_count_excluding_manifest": len(file_rows),
                        "total_bytes_excluding_manifest": sum(row["bytes"] for row in file_rows),
                        "files": file_rows},
        }
        (temp / "package-manifest.json").write_bytes(_json_bytes(manifest))
        # Recheck the identities after copying so a producer swap during
        # preparation cannot leave a package that claims older bytes.
        if _source_identity(root, receipt) != source:
            raise PackageError("source identity changed while preparing the package")
        final_input_count, final_input_hash = _validate_source_inputs(root, receipt, worker_sha)
        if (final_input_count, final_input_hash) != (input_count, input_hash):
            raise PackageError("source input identity changed while preparing the package")
        for row in artifact_rows:
            if _sha256_file(build_root / row["path"], row["path"]) != row["sha256"]:
                raise PackageError(f"runtime artifact changed while preparing package: {row['path']}")
        for path, raw, label in (
            (build_receipt_path, receipt_raw, "build receipt"),
            (binding_receipt_path, binding_raw, "baseline binding receipt"),
            (deployed_version_path, version_raw, "deployed version"),
            (deployments_path, deployments_raw, "deployments receipt"),
            (staging_config_path, config_raw, "staging config"),
        ):
            if _sha256_file(path, label) != _sha256_bytes(raw):
                raise PackageError(f"{label} changed while preparing package")
        if output.exists() or output.is_symlink():
            raise PackageError("package output appeared while preparing; refusing overwrite")
        temp.replace(output)
        return manifest
    except Exception:
        if temp.exists() and temp.is_dir() and not temp.is_symlink():
            shutil.rmtree(temp)
        raise


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--binding-receipt", type=Path, required=True)
    parser.add_argument("--deployed-version", type=Path, required=True)
    parser.add_argument("--deployments", type=Path, required=True,
                        help="read-only exact-name Wrangler deployments JSON")
    parser.add_argument("--staging-config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True,
                        help="new external package directory containing site/, worker.mjs and Wrangler config")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    try:
        manifest = prepare_package(args.source_root, args.build_receipt,
                                  args.binding_receipt, args.deployed_version,
                                  args.deployments, args.staging_config, args.output)
    except PackageError as exc:
        print(f"staging room package refused: {exc}", file=sys.stderr)
        return 2
    print(json.dumps({"state": "PASS", "output": str(args.output),
                      "files": manifest["package"]["file_count_excluding_manifest"],
                      "bytes": manifest["package"]["total_bytes_excluding_manifest"],
                      "ui_origin": manifest["staging_worker"]["ui_origin"],
                      "manifest": str(args.output / "package-manifest.json")}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
