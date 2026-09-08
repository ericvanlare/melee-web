#!/usr/bin/env python3
"""Prepare a checked downstream source tree while retaining pristine dependencies."""
from pathlib import Path
import subprocess
import os
import tempfile

from bootstrap import apply_patch, ensure_repository, patch_state, read_lock, verify_sources
from gameplay_bool import composed_patch

ROOT = Path(__file__).resolve().parents[1]


def transition_patch(repository, previous, updated):
    """Apply only old→new differences after exact ownership verification.

    Reversing/reapplying the entire composed ABI patch would touch hundreds of
    unchanged sources on each small adapter edit. Isolated indexes establish
    both expected trees, then Git validates the minimal worktree delta.
    """
    with tempfile.TemporaryDirectory(prefix="melee-source-transition-") as temporary:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / "index"))
        trees = []
        for patch in (previous, updated):
            subprocess.run(["git", "read-tree", "HEAD"], cwd=repository, env=env, check=True)
            subprocess.run(["git", "apply", "--cached", str(patch.resolve())], cwd=repository, env=env, check=True)
            trees.append(subprocess.check_output(["git", "write-tree"], cwd=repository, env=env).strip())
        delta = subprocess.check_output(["git", "diff", "--no-ext-diff", "--no-color", "--binary",
                                         trees[0].decode(), trees[1].decode()], cwd=repository)
        if delta:
            subprocess.run(["git", "apply", "--check", "-"], input=delta, cwd=repository, check=True)
            subprocess.run(["git", "apply", "-"], input=delta, cwd=repository, check=True)


def prepare_sources(root=ROOT, lock=None):
    lock = lock or read_lock(root)
    verify_sources(root, lock)
    build = root / "build"
    if build.is_symlink():
        raise ValueError("Build output must be a local directory, not a symlink")
    build.mkdir(exist_ok=True)
    # Reuse the exact-patch verification already used for Aurora. A separate
    # local Git checkout preserves mtimes between builds and rejects unexpected
    # edits, staged changes or a different source pin instead of overwriting them.
    generated = ensure_repository(build, "gameplay-source", {
        "url": str((root / ".deps/melee").resolve()),
        "commit": lock["repositories"]["melee"]["commit"],
    })
    reviewed = root / "patches/melee-gameplay.patch"
    patch = composed_patch(generated, reviewed)
    previous = generated / ".git/melee-web-gameplay.patch"
    if previous.is_file() and previous.read_bytes() != patch.read_bytes():
        # Only reverse the exact previous generated patch, never an unexplained
        # developer edit. Validate the new patch against HEAD before mutation.
        try:
            patch_state(generated, patch)
        except ValueError:
            if patch_state(generated, previous) == "applied":
                transition_patch(generated, previous, patch)
    apply_patch(generated, patch)
    if not previous.is_file() or previous.read_bytes() != patch.read_bytes():
        previous.write_bytes(patch.read_bytes())
    return generated / "src"


if __name__ == "__main__":
    print(prepare_sources())
