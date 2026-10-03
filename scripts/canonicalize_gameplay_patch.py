#!/usr/bin/env python3
"""Rewrite the reviewed gameplay patch in canonical Git order.

The reviewed patch is an input to source preparation, not a log. Appending new
file diffs at its end makes unrelated pull requests conflict on the same final
lines, and repeated diffs of one file make review harder. The canonical form is
`git diff` of the pinned Melee tree against that tree with the patch applied:
one diff per file, sorted by path, full blob indexes and fixed diff options.

Canonicalizing never changes the prepared source tree. The command verifies
that the old and new patches produce the same Git tree before it writes. The
CLI requires the dependency checkout to be standalone, clean, and at the
lockfile commit. Objects and indexes are written to temporary directories; the
pristine checkout is read only.
"""
from pathlib import Path
import argparse
import os
import stat
import subprocess
import sys
import tempfile

from bootstrap import read_lock, require_clean, verify_repository
from workspace_resources import operation

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


def _validated_patch_target(path):
    """Return a regular, non-symlink patch file suitable for replacement."""
    path = Path(path)
    absolute = path.absolute()
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"{path}: patch target must be an existing regular file, not a symlink")
    return absolute


def _patch_snapshot(path):
    """Read a patch and its identity so a concurrent edit cannot be overwritten."""
    path = _validated_patch_target(path)
    info = path.stat()
    return path.read_bytes(), (info.st_dev, info.st_ino, info.st_size,
                               info.st_mtime_ns, info.st_ctime_ns)


def _replace_patch(path, expected, identity, replacement):
    """Atomically replace an unchanged patch target with regular-file checks."""
    path = _validated_patch_target(path)

    def unchanged():
        current = _validated_patch_target(path)
        info = current.stat()
        fingerprint = (info.st_dev, info.st_ino, info.st_size,
                       info.st_mtime_ns, info.st_ctime_ns)
        return (current == path and fingerprint == identity and
                current.read_bytes() == expected)

    if not unchanged():
        raise ValueError(f"{path}: changed while preparing the canonical patch; refusing to overwrite")

    mode = stat.S_IMODE(path.stat().st_mode)
    temporary_name = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.",
                                                      dir=path.parent)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(replacement)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary_name, mode)
        if not unchanged():
            raise ValueError(f"{path}: changed while writing the canonical patch; refusing to overwrite")
        os.replace(temporary_name, path)
        temporary_name = None
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)


def _verify_pinned_repository(repository, commit):
    """Require the source object database to be the clean, pinned checkout."""
    try:
        verify_repository(repository, commit)
        require_clean(repository)
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError(f"{repository}: unable to verify the pinned checkout") from error


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repository", type=Path, default=ROOT / ".deps/melee",
                        help="clean standalone Melee checkout at the lockfile commit")
    parser.add_argument("--patch", type=Path, default=ROOT / PATCH)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help="exit 1 when the patch is not canonical; do not write")
    mode.add_argument("--merge", nargs=3, metavar=("BASE", "OURS", "THEIRS"),
                      help="Git revisions whose patches are merged as source trees into --patch; "
                           "for a branch behind main: --merge $(git merge-base HEAD origin/main) HEAD origin/main")
    args = parser.parse_args(argv)
    try:
        with operation(ROOT, "canonical gameplay patch"):
            commit = read_lock(ROOT)["repositories"]["melee"]["commit"]
            _verify_pinned_repository(args.repository, commit)
            target = _validated_patch_target(args.patch)
            if args.merge:
                current, identity = _patch_snapshot(target)
                versions = [subprocess.check_output(["git", "show", f"{revision}:{PATCH.as_posix()}"], cwd=ROOT)
                            for revision in args.merge]
                merged, conflicts = merge_patches(args.repository, commit, *versions)
                if merged is None:
                    print("Source edits conflict in: " + ", ".join(conflicts), file=sys.stderr)
                    return 1
                _replace_patch(target, current, identity, merged)
                print(f"{target}: merged {' + '.join(args.merge[1:])} as source trees")
                return 0
            current, identity = _patch_snapshot(target)
            canonical, tree = canonical_patch(args.repository, commit, current)
            if canonical == current:
                print(f"{target}: canonical (tree {tree})")
                return 0
            if args.check:
                print(f"{target}: not canonical; run python3 scripts/canonicalize_gameplay_patch.py",
                      file=sys.stderr)
                return 1
            _replace_patch(target, current, identity, canonical)
            print(f"{target}: rewritten in canonical order (tree {tree} unchanged)")
            return 0
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    sys.exit(main())
