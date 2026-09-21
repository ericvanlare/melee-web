#!/usr/bin/env python3
"""Audit explicit Git histories without printing matched contents or rewriting refs.

The policy comes from one pinned commit, including when older commits predate
it. Every historical path/blob pairing is checked, not just one name per blob.
The JSON report records findings for review; it never auto-clears old findings.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys

import check_repository_content as guard


def safe_path(value):
    if any(pattern.search(value.encode()) for pattern in guard.SECRET_RULES.values()):
        return "<redacted>"
    return value


def resolve_roots(repo, refs, namespaces):
    names = set(refs)
    for namespace in namespaces:
        if not namespace.startswith("refs/") or any(c.isspace() for c in namespace):
            raise guard.CheckError("Namespaces must be explicit refs/ prefixes")
        rows = guard.git(repo, "for-each-ref", "--format=%(refname)", namespace).decode().splitlines()
        if not rows:
            raise guard.CheckError("A requested namespace contains no refs")
        names.update(rows)
    if not names:
        raise guard.CheckError("Select at least one ref or namespace")
    roots = []
    for name in sorted(names):
        oid = guard.git(repo, "rev-parse", "--verify", "--end-of-options", name + "^{object}").decode().strip()
        commit = guard.git(repo, "rev-parse", "--verify", "--end-of-options", oid + "^{commit}").decode().strip()
        roots.append({"ref": safe_path(name), "object": oid, "commit": commit})
    return roots


def audit(repo, refs, namespaces=(), policy_ref="HEAD"):
    if guard.git(repo, "rev-parse", "--is-shallow-repository").strip() != b"false":
        raise guard.CheckError("A shallow repository cannot establish complete reachable history")
    if guard.git(repo, "for-each-ref", "--format=%(refname)", "refs/replace").strip():
        raise guard.CheckError("Remove replacement refs from the audit checkout before scanning")
    grafts = Path(guard.git(repo, "rev-parse", "--path-format=absolute", "--git-path", "info/grafts").decode().strip())
    if grafts.exists():
        raise guard.CheckError("A grafted repository cannot establish complete reachable history")
    roots = resolve_roots(repo, refs, namespaces)
    policy_commit, policy_entries = guard.snapshot(repo, policy_ref)
    policy_entry = next((entry for entry in policy_entries if entry.path == guard.POLICY_PATH and entry.mode == "100644"), None)
    if policy_entry is None:
        raise guard.CheckError("Policy commit has no regular repository-content policy")
    size = guard.object_sizes(repo, [policy_entry])[policy_entry.oid]
    if size > guard.MAX_BLOB_BYTES:
        raise guard.CheckError("Policy exceeds the content bound")
    reader = guard.BlobReader(repo)
    try:
        policy_bytes = reader.read(policy_entry.oid, size)
    finally:
        reader.close()
    policy = guard.read_policy(policy_bytes)
    revisions = sorted({root["commit"] for root in roots})
    commits = guard.git(repo, "rev-list", "--stdin", input=("\n".join(revisions) + "\n").encode()).decode().splitlines()
    entries = {}
    seen_trees = set()
    metadata = []
    email_hashes = set()
    # Commit objects are small text in this repository, but use the same bound
    # before reading them. Tag annotations are scanned as metadata as well.
    metadata_ids = set(commits) | {root["object"] for root in roots}
    for root in roots:
        oid = root["object"]
        while oid != root["commit"]:
            if guard.git(repo, "cat-file", "-t", oid).strip() != b"tag":
                raise guard.CheckError("A tag chain contains an unexpected object")
            if int(guard.git(repo, "cat-file", "-s", oid)) > guard.MAX_BLOB_BYTES:
                raise guard.CheckError("A tag annotation exceeds the content bound")
            tag = guard.git(repo, "cat-file", "tag", oid)
            match = re.match(rb"object ([0-9a-f]{40,64})\n", tag)
            if not match:
                raise guard.CheckError("A tag lacks a target identity")
            oid = match[1].decode()
            metadata_ids.add(oid)
    inventory = guard.git(repo, "cat-file", "--batch-check=%(objectname) %(objecttype) %(objectsize)",
                          input=("\n".join(sorted(metadata_ids)) + "\n").encode())
    for row in inventory.decode().splitlines():
        oid, kind, raw_size = row.split()
        size = int(raw_size)
        if kind not in {"commit", "tag"}:
            raise guard.CheckError("A selected metadata object is not a commit or tag")
        if size > guard.MAX_BLOB_BYTES:
            raise guard.CheckError("Oversized metadata prevents a complete history audit")
        data = guard.git(repo, "cat-file", kind, oid)
        for found in guard.inspect(guard.Entry("metadata.txt", "100644", oid), data,
                                   {"file_exceptions": [], "synthetic_secrets": []}):
            metadata.append({"object": oid, "kind": kind, "rule": found.rule, "line": found.line})
        for email in re.findall(rb"^(?:author|committer|tagger) [^\n]*<([^<>\n]+)>", data, re.M):
            email_hashes.add(hashlib.sha256(email).hexdigest())
        if kind == "commit":
            match = re.match(rb"tree ([0-9a-f]{40,64})\n", data)
            if not match:
                raise guard.CheckError("A commit lacks a tree identity")
            tree = match[1].decode()
            if tree not in seen_trees:
                seen_trees.add(tree)
                _, tree_entries = guard.snapshot(repo, oid, allow_empty=True)
                for entry in tree_entries:
                    entries.setdefault(entry, oid)
    sizes = guard.object_sizes(repo, list(entries))
    if any(size > guard.MAX_BLOB_BYTES for size in sizes.values()):
        raise guard.CheckError("Oversized blob prevents a complete history audit")
    findings = []
    reader = guard.BlobReader(repo)
    try:
        for entry, example_commit in sorted(entries.items(), key=lambda item: (item[0].oid, item[0].path, item[0].mode)):
            if entry.mode not in {"100644", "100755"}:
                result = [guard.Finding(entry.path, "non_regular_entry")]
            else:
                result = guard.inspect(entry, reader.read(entry.oid, sizes[entry.oid]), policy)
            for found in result:
                findings.append({**asdict(found), "path": safe_path(found.path),
                                 "blob": entry.oid, "example_commit": example_commit})
    finally:
        reader.close()
    return {
        "schema": "melee-web-history-audit-v1", "roots": roots,
        "observed_at": datetime.now(timezone.utc).isoformat(),
        "scanner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "content_guard_sha256": hashlib.sha256(Path(guard.__file__).read_bytes()).hexdigest(),
        "policy_commit": policy_commit, "policy_sha256": hashlib.sha256(policy_bytes).hexdigest(),
        "commits": len(commits), "trees": len(seen_trees), "path_blob_pairs": len(entries),
        "regular_blobs": len(sizes), "unique_regular_blob_bytes": sum(sizes.values()),
        "commit_email_sha256": sorted(email_hashes), "findings": findings,
        "metadata_findings": metadata,
        "finding_counts": dict(sorted(Counter(item["rule"] for item in findings + metadata).items())),
        "limits": ["Only the selected reachable Git histories and tag annotations are covered.",
                   "No reflogs, unreachable objects, server-only refs, discussions or Actions material.",
                   "The content guard's pattern and encoding limits also apply here.",
                   "Findings require review; an empty result is not rights or security clearance."],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=guard.ROOT)
    parser.add_argument("--ref", action="append", default=[])
    parser.add_argument("--namespace", action="append", default=[])
    parser.add_argument("--policy-ref", default="HEAD")
    parser.add_argument("--output", required=True, type=Path, help="New report path, normally under ignored work/")
    args = parser.parse_args(argv)
    try:
        report = audit(args.repo, args.ref, args.namespace, args.policy_ref)
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2, ensure_ascii=True)
            stream.write("\n")
    except guard.CheckError as error:
        print(f"History audit incomplete: {error}", file=sys.stderr)
        return 2
    except (OSError, ValueError) as error:
        print(f"History audit incomplete: {type(error).__name__}", file=sys.stderr)
        return 2
    count = len(report["findings"]) + len(report["metadata_findings"])
    print(f"History audit: {report['commits']} commits, {report['path_blob_pairs']} path/blob pairs; {count} findings requiring review.")
    return 1 if count else 0


if __name__ == "__main__":
    raise SystemExit(main())
