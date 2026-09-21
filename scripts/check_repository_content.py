#!/usr/bin/env python3
"""Check the Git index, or a committed tree, for publication-sensitive content.

Only Git objects are read: unstaged edits, ignored inputs and symlink targets
cannot replace the snapshot being checked. Findings never print matched values.
This is a bounded repository guard, not an exhaustive secrets or history audit.
"""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
POLICY_PATH = ".github/repository-content-policy.json"
SCHEMA = "melee-web-repository-content-policy-v1"
MAX_BLOB_BYTES = 8 * 1024 * 1024
ASSET_SUFFIXES = frozenset("""
.iso .ciso .gcm .rvz .wia .wbfs .dol .elf .o .a .wasm .dat .usd .ssm .hps
.mth .thp .sem .dsp .dtm .gci .raw .sav .slp .mwri .bin .zip .gz .tar .7z .rar
""".split())
PRIVATE_DIRECTORIES = frozenset({
    "assets-local", "work", ".deps", ".venv", ".cache", "build", ".wrangler",
    ".ssh", ".aws", ".azure", ".kube", "__pycache__",
    # Local state also excluded by the reference-capture distribution checks.
    "captures", "private", "secrets", "save-states", "states", "memorycards",
    "config", "configuration",
})
PRIVATE_NAMES = frozenset({
    ".npmrc", ".pypirc", ".netrc", "credentials", "credentials.json",
    "id_rsa", "id_ed25519", "id_ecdsa", "service-account.json",
    "config.json", "dolphin.ini", "gcpadnew.ini",
})
FILE_EXCEPTION_RULES = frozenset({"binary_content", "encoded_payload", "asset_extension"})
SECRET_RULES = {
    "private_key": re.compile(rb"-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY-----"),
    "aws_access_key": re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b"),
    "github_token": re.compile(rb"\b(?:gh[pousr]_[A-Za-z0-9]{36}|github_pat_[A-Za-z0-9_]{50,})\b"),
    "slack_token": re.compile(rb"\bxox[baprs]-[A-Za-z0-9-]{20,}\b"),
    "api_secret": re.compile(rb"\b(?:sk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{40,}|sk_live_[A-Za-z0-9]{16,})\b"),
    "sensitive_assignment": re.compile(
        rb"(?im)(?:^|[\s\"'])(?:api[_-]?key|access[_-]?token|auth[_-]?token|client[_-]?secret|password)"
        rb"[\"']?\s*[:=]\s*[\"']([A-Za-z0-9_+/=.-]{20,})[\"']"
    ),
}
PERSONAL_PATH = re.compile(rb"/(?:Users|home)/[A-Za-z0-9_.-]+/")
ENCODED_PAYLOAD = re.compile(rb"(?<![A-Za-z0-9+/])[A-Za-z0-9+/]{512,}={0,2}(?![A-Za-z0-9+/=])")


class CheckError(ValueError):
    """A snapshot or policy could not be checked completely."""


@dataclass(frozen=True)
class Entry:
    path: str
    mode: str
    oid: str


@dataclass(frozen=True)
class Finding:
    path: str
    rule: str
    line: int | None = None


def git(repo: Path, *args: str, input: bytes | None = None) -> bytes:
    result = subprocess.run(["git", "-C", str(repo), *args], input=input,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise CheckError("Git could not read the requested snapshot")
    return result.stdout


def snapshot(repo: Path, ref: str | None) -> tuple[str, list[Entry]]:
    if ref is None:
        label = "index"
        records = git(repo, "ls-files", "--stage", "-z")
    else:
        label = git(repo, "rev-parse", "--verify", "--end-of-options", ref + "^{commit}").decode().strip()
        records = git(repo, "ls-tree", "-r", "-z", "--full-tree", label)
    entries = []
    for record in records.split(b"\0"):
        if not record:
            continue
        metadata, raw_path = record.split(b"\t", 1)
        mode, middle, last = metadata.decode("ascii").split()
        if ref is None:
            if last != "0":
                raise CheckError("Resolve unmerged index entries before checking")
            oid = middle
        else:
            oid = last
        try:
            path = raw_path.decode("utf-8")
        except UnicodeDecodeError:
            raise CheckError("A tracked path is not UTF-8") from None
        entries.append(Entry(path, mode, oid))
    if not entries:
        raise CheckError("The requested snapshot has no tracked files")
    return label, entries


def object_sizes(repo: Path, entries: list[Entry]) -> dict[str, int]:
    ids = sorted({entry.oid for entry in entries if entry.mode in {"100644", "100755"}})
    result = git(repo, "cat-file", "--batch-check=%(objectname) %(objecttype) %(objectsize)",
                 input="".join(oid + "\n" for oid in ids).encode())
    sizes = {}
    for row in result.decode("ascii").splitlines():
        oid, kind, size = row.split()
        if kind != "blob":
            raise CheckError("A regular tracked file is not a readable blob")
        sizes[oid] = int(size)
    if set(sizes) != set(ids):
        raise CheckError("Git returned an incomplete object inventory")
    return sizes


class BlobReader:
    def __init__(self, repo: Path):
        self.process = subprocess.Popen(["git", "-C", str(repo), "cat-file", "--batch"],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL)

    def read(self, oid: str, expected_size: int) -> bytes:
        self.process.stdin.write((oid + "\n").encode())
        self.process.stdin.flush()
        header = self.process.stdout.readline().decode("ascii").strip().split()
        if header != [oid, "blob", str(expected_size)]:
            raise CheckError("Git returned an unexpected object header")
        data = self.process.stdout.read(expected_size)
        if len(data) != expected_size or self.process.stdout.read(1) != b"\n":
            raise CheckError("Git returned an incomplete blob")
        return data

    def close(self):
        self.process.stdin.close()
        self.process.stdout.close()
        if self.process.wait() != 0:
            raise CheckError("Git object reader failed")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise CheckError("Policy contains duplicate keys")
        result[key] = value
    return result


def read_policy(data: bytes) -> dict:
    try:
        policy = json.loads(data.decode("utf-8"), object_pairs_hook=unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError):
        raise CheckError("Policy must be valid UTF-8 JSON") from None
    if not isinstance(policy, dict) or set(policy) != {"schema", "file_exceptions", "synthetic_secrets"} or policy["schema"] != SCHEMA:
        raise CheckError("Policy schema or fields are invalid")
    for key in ("file_exceptions", "synthetic_secrets"):
        if not isinstance(policy[key], list):
            raise CheckError("Policy exception lists are invalid")
        seen = set()
        for item in policy[key]:
            if not isinstance(item, dict) or set(item) != {"path", "rule", "sha256", "reason"}:
                raise CheckError("Policy exception fields are invalid")
            path, rule, digest, reason = (item[name] for name in ("path", "rule", "sha256", "reason"))
            if not all(isinstance(value, str) for value in (path, rule, digest, reason)):
                raise CheckError("Policy exception values must be strings")
            parts = PurePosixPath(path).parts
            if not parts or path.startswith("/") or ".." in parts or "\\" in path or any(ord(c) < 32 for c in path) or PurePosixPath(path).as_posix() != path:
                raise CheckError("Policy exception path is invalid")
            allowed = FILE_EXCEPTION_RULES if key == "file_exceptions" else SECRET_RULES.keys() - {"private_key"}
            if rule not in allowed or not re.fullmatch(r"[0-9a-f]{64}", digest) or not reason.strip():
                raise CheckError("Policy exception rule, digest or rationale is invalid")
            identity = (path, rule, digest)
            if identity in seen:
                raise CheckError("Policy contains a duplicate exception")
            seen.add(identity)
    return policy


def inspect(entry: Entry, data: bytes, policy: dict) -> list[Finding]:
    path = PurePosixPath(entry.path)
    lower_parts = {part.lower() for part in path.parts}
    name = path.name.lower()
    findings = []
    digest = hashlib.sha256(data).hexdigest()
    exceptions = {(item["path"], item["rule"], item["sha256"]) for item in policy["file_exceptions"]}

    def add(rule, offset=None):
        if (entry.path, rule, digest) not in exceptions:
            findings.append(Finding(entry.path, rule, None if offset is None else data.count(b"\n", 0, offset) + 1))

    if lower_parts & PRIVATE_DIRECTORIES or name in PRIVATE_NAMES or name == ".env" or (name.startswith(".env.") and name != ".env.example"):
        add("private_path")
    if any(ord(c) < 32 for c in entry.path) or "\\" in entry.path:
        add("unsafe_path")
    if any(pattern.search(entry.path.encode()) for pattern in SECRET_RULES.values()):
        add("sensitive_filename")
    if path.suffix.lower() in ASSET_SUFFIXES:
        add("asset_extension")
    if b"\0" in data:
        add("binary_content")
    else:
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            add("binary_content")
    # Long encoded blobs need the same exact-file review as binary files.
    # This is intentionally not a general decoder for arbitrary encodings.
    for match in ENCODED_PAYLOAD.finditer(data):
        token = match.group()
        try:
            base64.b64decode(token + b"=" * (-len(token) % 4), validate=True)
        except ValueError:
            continue
        add("encoded_payload", match.start())
        break
    if path.suffix.lower() == ".b64" and not any(f.rule == "encoded_payload" for f in findings):
        add("encoded_payload")
    synthetic = {(item["path"], item["rule"], item["sha256"]) for item in policy["synthetic_secrets"]}
    for rule, pattern in SECRET_RULES.items():
        for match in pattern.finditer(data):
            token = match.group(1) if rule == "sensitive_assignment" else match.group()
            if (entry.path, rule, hashlib.sha256(token).hexdigest()) not in synthetic:
                add(rule, match.start())
    for match in PERSONAL_PATH.finditer(data):
        add("personal_path", match.start())
    return findings


def check(repo: Path, ref: str | None = None) -> tuple[str, int, list[Finding]]:
    label, entries = snapshot(repo, ref)
    sizes = object_sizes(repo, entries)
    policies = [entry for entry in entries if entry.path == POLICY_PATH and entry.mode == "100644"]
    if len(policies) != 1 or sizes[policies[0].oid] > MAX_BLOB_BYTES:
        raise CheckError("The snapshot must contain a regular, bounded repository-content policy")
    reader = BlobReader(repo)
    try:
        policy = read_policy(reader.read(policies[0].oid, sizes[policies[0].oid]))
        findings = []
        for entry in entries:
            if entry.mode not in {"100644", "100755"}:
                findings.append(Finding(entry.path, "non_regular_entry"))
            elif sizes[entry.oid] > MAX_BLOB_BYTES:
                findings.append(Finding(entry.path, "large_blob"))
            else:
                findings.extend(inspect(entry, reader.read(entry.oid, sizes[entry.oid]), policy))
    finally:
        reader.close()
    return label, len(entries), findings


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=ROOT)
    parser.add_argument("--ref", help="Check this commit's tree and policy instead of the Git index")
    args = parser.parse_args(argv)
    try:
        label, count, findings = check(args.repo, args.ref)
    except (CheckError, OSError, ValueError) as exc:
        # Errors describe the rule or operation, never raw object/credential data.
        print(f"Repository content check incomplete: {exc}", file=sys.stderr)
        return 2
    for finding in findings:
        secret_name = any(pattern.search(finding.path.encode()) for pattern in SECRET_RULES.values())
        location = json.dumps("<redacted path>" if secret_name else finding.path, ensure_ascii=True)
        if finding.line is not None:
            location += f":{finding.line}"
        print(f"{location}: {finding.rule}")
    print(f"Repository content: {count} tracked entries checked ({label}); {len(findings)} findings.")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
