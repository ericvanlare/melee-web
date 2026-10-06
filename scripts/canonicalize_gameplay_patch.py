#!/usr/bin/env python3
"""Rewrite a reviewed dependency patch in canonical Git order.

A reviewed patch is an input to source preparation, not a log. Appending new
file diffs at its end makes unrelated pull requests conflict on the same final
lines, and repeated diffs of one file make review harder. The canonical form is
`git diff` of the pinned dependency tree against that tree with the patch
applied: one diff per file, sorted by path, full blob indexes and fixed diff
options.

`--target gameplay` (the default) selects patches/melee-gameplay.patch and the
pinned Melee checkout; `--target aurora` selects patches/aurora-browser.patch
and the pinned Aurora checkout.

Canonicalizing never changes the prepared source tree. The command verifies
that the old and new patches produce the same Git tree before it writes. The
CLI requires the dependency checkout to be standalone and at the lockfile
commit; the Melee checkout must also be clean. Objects and indexes are written
to temporary directories; the dependency checkout is read only.
"""
from pathlib import Path
from typing import NamedTuple
import argparse
import os
import stat
import subprocess
import sys
import tempfile

from bootstrap import read_lock, require_clean, verify_repository
from workspace_resources import operation

ROOT = Path(__file__).resolve().parents[1]


class PatchTarget(NamedTuple):
    patch: Path        # repository-relative reviewed patch
    dependency: str    # dependencies.lock.json repository and .deps/ directory
    clean: bool        # require a clean dependency working tree


TARGETS = {
    "gameplay": PatchTarget(Path("patches/melee-gameplay.patch"), "melee", True),
    # Bootstrap applies the Aurora patch to the working tree of .deps/aurora,
    # so that checkout is normally dirty. Only the pinned commit's objects are
    # read, through a temporary index and object directory, so its working tree
    # does not affect the result.
    "aurora": PatchTarget(Path("patches/aurora-browser.patch"), "aurora", False),
}
# Every option that changes `git diff` output is fixed, so a contributor's Git
# configuration (context, order file, prefixes, renames) cannot change the bytes.
DIFF = ["-c", "core.quotePath=false", "-c", "diff.noprefix=false",
        "-c", "diff.mnemonicPrefix=false", "-c", "diff.renames=false",
        "-c", "diff.suppressBlankEmpty=false",
        "diff", "--no-ext-diff", "--no-color", "--binary", "--full-index",
        "--no-renames", "--diff-algorithm=myers", "--indent-heuristic",
        "--unified=3", "--inter-hunk-context=0", "--no-relative", "-O/dev/null",
        "--src-prefix=a/", "--dst-prefix=b/"]


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


def _verify_pinned_repository(repository, commit, *, clean=True):
    """Require the source object database to be the pinned (and clean) checkout."""
    try:
        verify_repository(repository, commit)
        if clean:
            require_clean(repository)
    except (OSError, subprocess.SubprocessError) as error:
        raise ValueError(f"{repository}: unable to verify the pinned checkout") from error


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--target", choices=tuple(TARGETS), default="gameplay",
                        help="reviewed patch: gameplay (patches/melee-gameplay.patch, the default) "
                             "or aurora (patches/aurora-browser.patch)")
    parser.add_argument("--repository", type=Path,
                        help="standalone dependency checkout at the lockfile commit "
                             "(default: .deps/melee, clean; or .deps/aurora for --target aurora)")
    parser.add_argument("--patch", type=Path, help="patch file to rewrite (default: the target's reviewed patch)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help="exit 1 when the patch is not canonical; do not write")
    mode.add_argument("--merge", nargs=3, metavar=("BASE", "OURS", "THEIRS"),
                      help="Git revisions whose patches are merged as source trees into --patch; "
                           "for a branch behind main: --merge $(git merge-base HEAD origin/main) HEAD origin/main")
    args = parser.parse_args(argv)
    selected = TARGETS[args.target]
    repository = args.repository if args.repository is not None else ROOT / ".deps" / selected.dependency
    patch_path = args.patch if args.patch is not None else ROOT / selected.patch
    rerun = "python3 scripts/canonicalize_gameplay_patch.py" + (
        "" if args.target == "gameplay" else f" --target {args.target}")
    try:
        with operation(ROOT, f"canonical {args.target} patch"):
            commit = read_lock(ROOT)["repositories"][selected.dependency]["commit"]
            _verify_pinned_repository(repository, commit, clean=selected.clean)
            target = _validated_patch_target(patch_path)
            if args.merge:
                current, identity = _patch_snapshot(target)
                versions = [subprocess.check_output(["git", "show", f"{revision}:{selected.patch.as_posix()}"],
                                                    cwd=ROOT)
                            for revision in args.merge]
                merged, conflicts = merge_patches(repository, commit, *versions)
                if merged is None:
                    print("Source edits conflict in: " + ", ".join(conflicts), file=sys.stderr)
                    return 1
                _replace_patch(target, current, identity, merged)
                print(f"{target}: merged {' + '.join(args.merge[1:])} as source trees")
                return 0
            current, identity = _patch_snapshot(target)
            canonical, tree = canonical_patch(repository, commit, current)
            if canonical == current:
                print(f"{target}: canonical (tree {tree})")
                return 0
            if args.check:
                print(f"{target}: not canonical; run {rerun}", file=sys.stderr)
                return 1
            _replace_patch(target, current, identity, canonical)
            print(f"{target}: rewritten in canonical order (tree {tree} unchanged)")
            return 0
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    sys.exit(main())
