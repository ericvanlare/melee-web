#!/usr/bin/env python3
"""Rewrite the reviewed gameplay patch in canonical Git order.

The reviewed patch is an input to source preparation, not a log. Appending new
file diffs at its end makes unrelated pull requests conflict on the same final
lines, and repeated diffs of one file make review harder. The canonical form is
`git diff` of the pinned Melee tree against that tree with the patch applied:
one diff per file, sorted by path, full blob indexes and fixed diff options.

Canonicalizing never changes the prepared source tree. The command verifies
that the old and new patches produce the same Git tree before it writes.
Objects are written to a temporary directory; the pristine checkout is read
only.
"""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile

from bootstrap import read_lock

ROOT = Path(__file__).resolve().parents[1]
PATCH = Path("patches/melee-gameplay.patch")
DIFF = ["-c", "core.quotePath=false", "-c", "diff.noprefix=false",
        "-c", "diff.mnemonicPrefix=false", "-c", "diff.renames=false",
        "diff", "--no-ext-diff", "--no-color", "--binary", "--full-index",
        "--no-renames", "--diff-algorithm=myers", "--src-prefix=a/", "--dst-prefix=b/"]


def _environment(repository, temporary):
    objects = temporary / "objects"
    objects.mkdir(exist_ok=True)
    source = subprocess.check_output(["git", "rev-parse", "--git-path", "objects"],
                                     cwd=repository, text=True).strip()
    return dict(os.environ, GIT_INDEX_FILE=str(temporary / "index"),
                GIT_OBJECT_DIRECTORY=str(objects),
                GIT_ALTERNATE_OBJECT_DIRECTORIES=str((Path(repository) / source).resolve()))


def applied_tree(repository, commit, patch_bytes, env):
    """Return the tree produced by applying patch bytes to the pinned commit."""
    subprocess.run(["git", "read-tree", commit], cwd=repository, env=env, check=True)
    subprocess.run(["git", "apply", "--cached", "-"], input=patch_bytes,
                   cwd=repository, env=env, check=True)
    return subprocess.check_output(["git", "write-tree"], cwd=repository, env=env).strip().decode()


def canonical_patch(repository, commit, patch_bytes):
    """Return (canonical bytes, tree) for a patch against the pinned commit."""
    with tempfile.TemporaryDirectory(prefix="melee-canonical-patch-") as temporary:
        env = _environment(repository, Path(temporary))
        base = subprocess.check_output(["git", "rev-parse", f"{commit}^{{tree}}"],
                                       cwd=repository, env=env).strip().decode()
        tree = applied_tree(repository, commit, patch_bytes, env)
        canonical = subprocess.check_output(["git", *DIFF, base, tree], cwd=repository, env=env)
        if applied_tree(repository, commit, canonical, env) != tree:
            raise ValueError("Canonical patch does not reproduce the reviewed source tree")
        return canonical, tree


def merge_patches(repository, commit, base_bytes, ours_bytes, theirs_bytes):
    """Three-way merge three patch versions as patched source trees.

    Text-merging two patch files conflicts whenever both sides change the same
    file diff, even for disjoint source edits. Merging the trees they produce
    conflicts only where the source edits overlap. Returns (canonical bytes,
    conflicted paths); bytes are None when the source edits conflict.
    """
    with tempfile.TemporaryDirectory(prefix="melee-merge-patch-") as temporary:
        env = _environment(repository, Path(temporary))
        base_tree = subprocess.check_output(["git", "rev-parse", f"{commit}^{{tree}}"],
                                            cwd=repository, env=env).strip().decode()
        trees = [applied_tree(repository, commit, data, env) for data in (base_bytes, ours_bytes, theirs_bytes)]
        result = subprocess.run(["git", "merge-tree", "--write-tree", "--name-only", "--no-messages",
                                 f"--merge-base={trees[0]}", trees[1], trees[2]],
                                cwd=repository, env=env, capture_output=True, text=True)
        lines = result.stdout.splitlines()
        if result.returncode == 1:
            return None, [line for line in lines[1:] if line]
        if result.returncode != 0:
            raise ValueError(result.stderr.strip() or "git merge-tree failed")
        merged = lines[0]
        canonical = subprocess.check_output(["git", *DIFF, base_tree, merged], cwd=repository, env=env)
        return canonical, []


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repository", type=Path, default=ROOT / ".deps/melee",
                        help="pristine Melee checkout containing the pinned commit")
    parser.add_argument("--patch", type=Path, default=ROOT / PATCH)
    parser.add_argument("--check", action="store_true",
                        help="exit 1 when the patch is not canonical; do not write")
    parser.add_argument("--merge", nargs=3, metavar=("BASE", "OURS", "THEIRS"),
                        help="Git revisions whose patches are merged as source trees into --patch; "
                             "for a branch behind main: --merge $(git merge-base HEAD origin/main) HEAD origin/main")
    args = parser.parse_args(argv)
    commit = read_lock(ROOT)["repositories"]["melee"]["commit"]
    if not (args.repository / ".git").exists():
        parser.error(f"{args.repository} is not a Git checkout; run scripts/bootstrap.py")
    if args.merge:
        versions = [subprocess.check_output(["git", "show", f"{revision}:{PATCH.as_posix()}"], cwd=ROOT)
                    for revision in args.merge]
        merged, conflicts = merge_patches(args.repository, commit, *versions)
        if merged is None:
            print("Source edits conflict in: " + ", ".join(conflicts), file=sys.stderr)
            return 1
        args.patch.write_bytes(merged)
        print(f"{args.patch}: merged {' + '.join(args.merge[1:])} as source trees")
        return 0
    current = args.patch.read_bytes()
    canonical, tree = canonical_patch(args.repository, commit, current)
    if canonical == current:
        print(f"{args.patch}: canonical (tree {tree})")
        return 0
    if args.check:
        print(f"{args.patch}: not canonical; run python3 scripts/canonicalize_gameplay_patch.py",
              file=sys.stderr)
        return 1
    args.patch.write_bytes(canonical)
    print(f"{args.patch}: rewritten in canonical order (tree {tree} unchanged)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
