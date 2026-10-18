#!/usr/bin/env python3
"""Prepare a checked downstream source tree while retaining pristine dependencies."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
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


def compose_slippi_profile_patch(repository, base_patch, profile_patch):
    """Compose one opt-in Slippi render-dependency overlay onto the normal tree.

    The result is still a single exact HEAD→target patch, so the existing
    patch-state checks can reject any generated-checkout drift. Its cache is
    keyed by both reviewed inputs and never affects the ordinary source tree.
    """
    repository, base_patch, profile_patch = map(Path, (repository, base_patch, profile_patch))
    if not profile_patch.is_file():
        raise ValueError(f"Missing Slippi profile patch: {profile_patch}")
    target = repository / ".git/melee-web-slippi-profile-composed.patch"
    manifest = repository / ".git/melee-web-slippi-profile-composed.json"
    key = {
        "base_patch_sha256": hashlib.sha256(base_patch.read_bytes()).hexdigest(),
        "profile_patch_sha256": hashlib.sha256(profile_patch.read_bytes()).hexdigest(),
    }
    if target.is_file() and manifest.is_file():
        try:
            cached = json.loads(manifest.read_text())
        except (ValueError, OSError):
            cached = {}
        if (cached.get("key") == key and
                cached.get("patch_sha256") == hashlib.sha256(target.read_bytes()).hexdigest()):
            return target

    temporary = Path(tempfile.mkdtemp(
        prefix="melee-slippi-profile-source-", dir=repository / ".git"))
    try:
        temp = temporary
        worktree = temp / "tree"
        worktree.mkdir()
        env = dict(os.environ, GIT_INDEX_FILE=str(temp / "index"),
                   GIT_WORK_TREE=str(worktree))
        subprocess.run(["git", "read-tree", "HEAD"], cwd=repository, env=env, check=True)
        subprocess.run(["git", "apply", "--cached", str(base_patch.resolve())],
                       cwd=repository, env=env, check=True)
        subprocess.run(["git", "checkout-index", "--all", "--force"],
                       cwd=repository, env=env, check=True)
        subprocess.run(["git", "apply", "--check", str(profile_patch.resolve())],
                       cwd=worktree, check=True)
        subprocess.run(["git", "apply", str(profile_patch.resolve())],
                       cwd=worktree, check=True)
        subprocess.run(["git", "add", "--all", "--", "."], cwd=worktree,
                       env=dict(env, GIT_DIR=str(repository / ".git")), check=True)
        result = subprocess.check_output(
            ["git", "diff", "--no-ext-diff", "--no-color", "--cached", "--binary", "HEAD"],
            cwd=repository, env=env)
        if not target.is_file() or target.read_bytes() != result:
            staged = target.with_suffix(".patch.tmp")
            staged.write_bytes(result)
            staged.replace(target)
        payload = json.dumps({
            "key": key,
            "patch_sha256": hashlib.sha256(result).hexdigest(),
        }, sort_keys=True) + "\n"
        manifest.write_text(payload)
    except Exception as error:
        raise RuntimeError(
            f"Slippi profile patch composition failed; scratch retained at {temporary}"
        ) from error
    else:
        shutil.rmtree(temporary)
    return target


def prepare_sources(root=ROOT, lock=None, *, slippi_profile=False):
    lock = lock or read_lock(root)
    verify_sources(root, lock)
    build = root / "build"
    if build.is_symlink():
        raise ValueError("Build output must be a local directory, not a symlink")
    build.mkdir(exist_ok=True)
    # Reuse the exact-patch verification already used for Aurora. A separate
    # local Git checkout preserves mtimes between builds and rejects unexpected
    # edits, staged changes or a different source pin instead of overwriting them.
    source_checkout = "gameplay-source-slippi-profile" if slippi_profile else "gameplay-source"
    generated = ensure_repository(build, source_checkout, {
        "url": str((root / ".deps/melee").resolve()),
        "commit": lock["repositories"]["melee"]["commit"],
    })
    reviewed = root / "patches/melee-gameplay.patch"
    patch = composed_patch(generated, reviewed)
    if slippi_profile:
        patch = compose_slippi_profile_patch(
            generated, patch, root / "src/slippi_profile/patches/melee-profile-hooks.patch")
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
