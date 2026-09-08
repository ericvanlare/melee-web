#!/usr/bin/env python3
"""Prepare a checked downstream source tree while retaining pristine dependencies."""
from pathlib import Path
import subprocess

from bootstrap import apply_patch, ensure_repository, patch_state, read_lock, verify_sources

ROOT = Path(__file__).resolve().parents[1]


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
    patch = root / "patches/melee-gameplay.patch"
    previous = generated / ".git/melee-web-gameplay.patch"
    if previous.is_file() and previous.read_bytes() != patch.read_bytes():
        # Only reverse the exact previous generated patch, never an unexplained
        # developer edit. Validate the new patch against HEAD before mutation.
        try:
            patch_state(generated, patch)
        except ValueError:
            if patch_state(generated, previous) == "applied":
                subprocess.run(["git", "apply", "--reverse", str(previous)], cwd=generated, check=True)
    apply_patch(generated, patch)
    previous.write_bytes(patch.read_bytes())
    return generated / "src"


if __name__ == "__main__":
    print(prepare_sources())
