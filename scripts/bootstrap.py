#!/usr/bin/env python3
"""Fetch pinned dependencies and apply reviewed patches without replacing local edits."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import venv

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from workspace_resources import operation

# Recognized predecessors for the combined Future/object and browser-owner
# patch. The first is the installed earlier owner diagnostic tree observed in
# this checkout; the second is the clean-main predecessor retained by the
# Future/object bootstrap path. Both are exact isolated-index trees, never an
# arbitrary locally modified checkout.
AURORA_PREVIOUS_PATCH_TREES = (
    "980aec703ceb03604f7692cb4efd91779f920078",
    "ba1bbed8acbd886ed263468e3d9148163e4afc68",
    # Current main carries the Future/object patch without the downstream
    # browser-owner additions. This is the exact tree obtained by applying
    # that main patch to the pinned pristine Aurora checkout.
    "ef139550ae1ff9e167cdbf0b8d2b18c4933c3b5a",
    # Exact renderer diagnostic tree from this fighter checkout's c3ddd39 build.
    "734ea922f3e6c1ff53ebf356714f30682f7a7abe",
)


def run(*args, cwd=ROOT):
    subprocess.run([str(arg) for arg in args], cwd=cwd, check=True)


def output(*args, cwd=ROOT):
    return subprocess.check_output([str(arg) for arg in args], cwd=cwd, text=True).strip()


def read_lock(root=ROOT):
    lock = json.loads((root / "dependencies.lock.json").read_text(encoding="utf-8"))
    if not isinstance(lock, dict) or lock.get("schema") != 1:
        raise ValueError("Unsupported dependency lock schema")
    repositories = lock.get("repositories")
    if not isinstance(repositories, dict) or not {"aurora", "melee", "emsdk"} <= repositories.keys():
        raise ValueError("Dependency lock must include aurora, melee, and emsdk")
    for name, spec in repositories.items():
        if not re.fullmatch(r"[a-z][a-z0-9_-]*", name):
            raise ValueError(f"Invalid dependency directory name: {name}")
        if not isinstance(spec, dict) or not isinstance(spec.get("url"), str) or not spec["url"]:
            raise ValueError(f"{name}: missing repository URL")
        if not re.fullmatch(r"[0-9a-f]{40}", str(spec.get("commit", ""))):
            raise ValueError(f"{name}: commit must be a full lowercase Git SHA-1")
    if not re.fullmatch(r"\d+\.\d+\.\d+", str(lock.get("emscripten", ""))):
        raise ValueError("Emscripten must be pinned to an exact release")
    packages = lock.get("python_build_packages")
    if not isinstance(packages, list) or not packages or any(
        not isinstance(package, str)
        or not re.fullmatch(r"[A-Za-z0-9_.-]+==[A-Za-z0-9_.+-]+", package)
        for package in packages
    ):
        raise ValueError("Python build packages must use exact name==version pins")
    reference_tools = lock.get("reference_tools", {})
    if not isinstance(reference_tools, dict):
        raise ValueError("Reference tools must be an object")
    for name, spec in reference_tools.items():
        if not re.fullmatch(r"[a-z][a-z0-9_-]*", name):
            raise ValueError(f"Invalid reference tool name: {name}")
        if not isinstance(spec, dict) or not isinstance(spec.get("url"), str):
            raise ValueError(f"{name}: missing reference tool URL")
        if not re.fullmatch(r"[0-9a-f]{40}", str(spec.get("commit", ""))):
            raise ValueError(f"{name}: reference tool commit must be a full lowercase Git SHA-1")
        if spec.get("kind") == "source":
            continue
        if spec.get("kind") not in (None, "package"):
            raise ValueError(f"{name}: unsupported reference tool kind")
        if not isinstance(spec.get("package"), str) or not spec["package"]:
            raise ValueError(f"{name}: missing reference package name")
        if not re.fullmatch(r"\d+\.\d+\.\d+", str(spec.get("version", ""))):
            raise ValueError(f"{name}: reference package must use an exact version")
    return lock


def verify_repository(path, commit):
    if path.is_symlink() or not path.is_dir():
        raise ValueError(f"{path}: expected a local dependency directory, not a symlink")
    top = Path(output("git", "rev-parse", "--show-toplevel", cwd=path)).resolve()
    if top != path.resolve():
        raise ValueError(f"{path}: not a standalone Git checkout")
    head = output("git", "rev-parse", "HEAD", cwd=path)
    if head != commit:
        raise ValueError(f"{path.name}: expected {commit}, got {head}. Refusing to overwrite.")


def ensure_repository(deps, name, spec):
    path = deps / name
    if not path.exists() and not path.is_symlink():
        # Publish only a complete checkout. A failed download remains safe to retry.
        with tempfile.TemporaryDirectory(prefix=f".{name}-", dir=deps) as temporary:
            checkout = Path(temporary) / "checkout"
            run("git", "init", "--quiet", checkout)
            run("git", "remote", "add", "origin", spec["url"], cwd=checkout)
            run("git", "fetch", "--no-tags", "--depth", "1", "origin", spec["commit"], cwd=checkout)
            run("git", "checkout", "--quiet", "--detach", spec["commit"], cwd=checkout)
            verify_repository(checkout, spec["commit"])
            if path.exists() or path.is_symlink():
                raise ValueError(f"{path}: appeared during download; refusing to overwrite")
            checkout.rename(path)
    verify_repository(path, spec["commit"])
    return path


def require_clean(path):
    if output("git", "status", "--porcelain", "--untracked-files=all", cwd=path):
        raise ValueError(f"{path.name}: contains local changes; refusing to overwrite or build")


def _patch_trees(path, patch):
    """Identify pristine, reviewed and actual trees without changing the real index."""
    if not patch.is_file():
        raise ValueError(f"Missing reviewed patch: {patch}")
    if output("git", "diff", "--cached", "--name-only", cwd=path):
        raise ValueError(f"{path.name}: contains staged changes; refusing to apply or build")
    # An isolated index computes the exact expected tree, without touching the
    # checkout's working files or index. Reverse-apply alone accepts unrelated edits.
    with tempfile.TemporaryDirectory(prefix="melee-web-patch-") as temporary:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / "index"))
        subprocess.run(["git", "read-tree", "HEAD"], cwd=path, env=env, check=True)
        pristine = subprocess.check_output(["git", "write-tree"], cwd=path, env=env, text=True).strip()
        subprocess.run(["git", "apply", "--cached", str(patch.resolve())], cwd=path, env=env, check=True)
        expected = subprocess.check_output(
            ["git", "write-tree"], cwd=path, env=env, text=True,
        ).strip()
        subprocess.run(["git", "read-tree", "HEAD"], cwd=path, env=env, check=True)
        # Include untracked additions in the comparison: a reviewed patch may add
        # files, while unrelated local files still make the comparison fail.
        subprocess.run(["git", "add", "--all", "--", "."], cwd=path, env=env, check=True)
        actual = subprocess.check_output(
            ["git", "write-tree"], cwd=path, env=env, text=True,
        ).strip()
    return pristine, expected, actual


def patch_state(path, patch):
    """Return clean/applied, refusing any changes beyond the reviewed patch."""
    pristine, expected, actual = _patch_trees(path, patch)
    if actual == expected:
        return "applied"
    if actual == pristine:
        return "clean"
    raise ValueError(f"{path.name}: changes differ from {patch.name}; refusing to overwrite or build")


def apply_patch(path, patch, *, previous_tree=None):
    pristine, expected, actual = _patch_trees(path, patch)
    if actual == expected:
        return
    if previous_tree is None:
        recognized_predecessors = set()
    elif isinstance(previous_tree, str):
        recognized_predecessors = {previous_tree}
    else:
        recognized_predecessors = set(previous_tree)
    if actual != pristine and actual not in recognized_predecessors:
        raise ValueError(f"{path.name}: changes differ from {patch.name}; refusing to overwrite or build")
    # Apply only the delta from an explicitly recognized tree. Reversing the
    # whole old patch would touch unchanged sources and invalidate build caches.
    delta = subprocess.check_output(
        ["git", "diff", "--no-ext-diff", "--no-color", "--binary", actual, expected], cwd=path)
    subprocess.run(["git", "apply", "--check", "-"], input=delta, cwd=path, check=True)
    subprocess.run(["git", "apply", "-"], input=delta, cwd=path, check=True)
    if patch_state(path, patch) != "applied":
        raise ValueError(f"{path.name}: patch did not produce the exact reviewed tree")


def verify_sources(root, lock):
    deps = root / ".deps"
    if deps.is_symlink():
        raise ValueError(".deps must be a local directory, not a symlink")
    for name, spec in lock["repositories"].items():
        path = deps / name
        verify_repository(path, spec["commit"])
        if name != "aurora":
            require_clean(path)
    if patch_state(deps / "aurora", root / "patches/aurora-browser.patch") != "applied":
        raise ValueError("Aurora patch is missing. Run python3 scripts/bootstrap.py first.")


def _bootstrap(root=ROOT):
    lock = read_lock(root)
    patch = root / "patches/aurora-browser.patch"
    if not patch.is_file():
        raise ValueError(f"Missing reviewed patch: {patch}")
    deps = root / ".deps"
    if deps.is_symlink():
        raise ValueError(".deps must be a local directory, not a symlink")
    deps.mkdir(exist_ok=True)
    for name, spec in lock["repositories"].items():
        path = ensure_repository(deps, name, spec)
        if name != "aurora":
            require_clean(path)
    apply_patch(deps / "aurora", patch, previous_tree=AURORA_PREVIOUS_PATCH_TREES)

    env_dir = root / ".venv"
    if env_dir.is_symlink():
        raise ValueError(".venv must be a local directory, not a symlink")
    if not env_dir.exists():
        venv.create(env_dir, with_pip=True)
    py = env_dir / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
    run(py, "-m", "pip", "install", "--disable-pip-version-check", *lock["python_build_packages"])
    run(sys.executable, deps / "emsdk/emsdk.py", "install", lock["emscripten"])
    run(sys.executable, deps / "emsdk/emsdk.py", "activate", lock["emscripten"])
    print("Ready. Run python3 scripts/build.py")


def bootstrap(root=ROOT):
    with operation(root, "bootstrap"):
        return _bootstrap(root)


def main():
    try:
        bootstrap()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"bootstrap: {error}") from error


if __name__ == "__main__":
    main()
