"""Checkout mutation safety and a journal of disposable compiler products.

There are no host-wide compute limits, queues, or disk admission floors.
"""
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import stat
import subprocess
import sys


CHECKOUTS_ENV = "MELEE_WORKSPACE_CHECKOUTS"


def state_directory():
    path = Path(os.environ.get("MELEE_RESOURCE_STATE", Path.home() / ".local/state/melee-web"))
    if path.is_symlink() or path.resolve() != path.absolute():
        raise ValueError("resource state must be an owned, real directory")
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    info = path.stat()
    if info.st_uid != os.getuid() or info.st_mode & 0o022:
        raise ValueError("resource state must be an owned, real directory")
    return path


def atomic_json(path, value):
    path = Path(path)
    if path.parent.resolve() != path.parent.absolute() or path.is_symlink():
        raise ValueError("refusing redirected resource metadata")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + secrets.token_hex(8) + ".tmp")
    try:
        with temporary.open("x") as handle:
            json.dump(value, handle, indent=2)
            handle.write("\n")
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def disk_status(root):
    usage = shutil.disk_usage(root)
    return {"free_gb": round(usage.free / 1e9, 3), "total_gb": round(usage.total / 1e9, 3)}


def report_space(root):
    """Report available space; the advisory threshold never blocks an operation."""
    status = disk_status(root)
    default = 5 if os.environ.get("CI") else 30
    try:
        threshold = float(os.environ.get("MELEE_WARN_FREE_GB", default))
        if not 0 < threshold < 100000:
            raise ValueError("invalid warning threshold")
    except ValueError:
        threshold = default
        print(f"workspace: invalid MELEE_WARN_FREE_GB; using {default} GB warning threshold", file=sys.stderr)
    if status["free_gb"] < threshold:
        print(f"workspace: WARNING: {status['free_gb']:.1f} GB available "
              f"(below {threshold:g} GB warning threshold); continuing. "
              "Consider retiring completed builds before creating more output.", file=sys.stderr)
    return status


def _open_lock(path):
    fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    handle = os.fdopen(fd, "r+")
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1:
        handle.close()
        raise ValueError("unsafe resource lock")
    return handle


def _try_lock(handle):
    import fcntl
    try:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return True
    except BlockingIOError:
        return False


def _live_ancestor_lock(path, token):
    """Only a matching held lock from this process or a live ancestor is reusable."""
    import fcntl
    try:
        if not isinstance(token, dict):
            return False
        with _open_lock(path) as handle:
            if _try_lock(handle):
                fcntl.flock(handle, fcntl.LOCK_UN)
                return False
            if json.load(handle) != token:
                return False
        pid = os.getpid()
        for _ in range(64):
            if pid == token.get("pid"):
                return True
            if pid <= 1:
                break
            pid = int(subprocess.check_output(["ps", "-p", str(pid), "-o", "ppid="], text=True))
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    return False


def _write_lock(handle, token):
    handle.seek(0)
    handle.truncate()
    json.dump(token, handle)
    handle.flush()


@contextmanager
def operation(root, label, *, reuse=True):
    """Fail fast on conflicting checkout mutations; never schedule host compute.

    Nested build steps may reuse a live ancestor's checkout mutex. Maintenance
    must pass reuse=False so even cleanup invoked by an active build refuses.
    """
    root = Path(root).resolve(strict=True)
    if os.name != "posix":
        if not reuse:
            raise ValueError("safe checkout maintenance requires macOS or Linux locks")
        report_space(root)
        yield
        return
    directory = state_directory()
    key = hashlib.sha256(os.fsencode(root)).hexdigest()
    checkout_path = directory / f"checkout-{key}.lock"
    try:
        checkouts = json.loads(os.environ.get(CHECKOUTS_ENV, "{}"))
    except ValueError:
        checkouts = {}
    if not isinstance(checkouts, dict):
        checkouts = {}
    if reuse and _live_ancestor_lock(checkout_path, checkouts.get(str(root))):
        yield
        return
    with _open_lock(checkout_path) as checkout:
        if not _try_lock(checkout):
            raise ValueError(f"{label}: checkout is busy; no changes made by this operation")
        previous = os.environ.get(CHECKOUTS_ENV)
        token = {"pid": os.getpid(), "root": str(root), "nonce": secrets.token_hex(16)}
        _write_lock(checkout, token)
        checkouts[str(root)] = token
        os.environ[CHECKOUTS_ENV] = json.dumps(checkouts)
        try:
            report_space(root)
            yield
        finally:
            if previous is None:
                os.environ.pop(CHECKOUTS_ENV, None)
            else:
                os.environ[CHECKOUTS_ENV] = previous


def _journal(root):
    return Path(root) / ".cache/workspace/builds.json"


def file_hash(path):
    result = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def fingerprint(info):
    # ctime detects writes even when a caller restores the prior mtime. This is
    # only a hash-cache key; retirement still verifies the actual file content.
    return [info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns]


def read_journal(root):
    path = _journal(root)
    if path.is_symlink() or path.resolve() != path:
        raise ValueError("refusing redirected build journal")
    journal = json.loads(path.read_text()) if path.exists() else {"root": str(root), "builds": {}}
    if not isinstance(journal, dict) or journal.get("root") != str(root):
        raise ValueError("build journal belongs to another checkout")
    if not isinstance(journal.get("builds"), dict):
        raise ValueError("invalid build journal records")
    for record in journal["builds"].values():
        if (not isinstance(record, dict) or type(record.get("succeeded")) is not bool
                or not isinstance(record.get("products"), dict)):
            raise ValueError("invalid build journal record")
        if not isinstance(record.get("fingerprints", {}), dict):
            raise ValueError("invalid build journal fingerprints")
        for expected in record["products"].values():
            if (not isinstance(expected, list) or len(expected) != 4
                    or any(type(value) is not int for value in expected[:3])
                    or not isinstance(expected[3], str)):
                raise ValueError("invalid build product identity")
    return journal


def record_build(root, build_dir, succeeded):
    """Only compiler products actually listed by Ninja can become cleanup candidates."""
    supplied_root = Path(root).absolute()
    relative_build = Path(build_dir).absolute().relative_to(supplied_root)
    root = supplied_root.resolve()
    build_dir = root / relative_build
    if build_dir.resolve() != build_dir or not build_dir.is_relative_to(root / "build"):
        raise ValueError("build journal requires an unredirected local build directory")
    path = _journal(root)
    journal = read_journal(root)
    build_key = str(build_dir.relative_to(root))
    previous = journal["builds"].get(build_key, {"products": {}, "fingerprints": {}})
    if not succeeded:
        # Failed/configure-only builds are ineligible for retirement. Retain the
        # prior cache so the next success need not rehash unchanged products.
        journal["builds"][build_key] = dict(previous, succeeded=False)
        atomic_json(path, journal)
        return
    products = {}
    fingerprints = {}
    ninja_log = build_dir / ".ninja_log"
    if ninja_log.is_symlink() or ninja_log.resolve() != ninja_log:
        raise ValueError("refusing redirected Ninja output journal")
    if ninja_log.is_file():
        outputs = set()
        for line in ninja_log.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) != 5:
                continue
            output = Path(fields[3])
            if output.is_absolute() or ".." in output.parts or output.suffix not in {".o", ".a"}:
                continue
            outputs.add(output)
        for output in sorted(outputs):
            product = build_dir / output
            if product.is_symlink() or product.resolve() != product or not product.is_file():
                continue
            info = product.stat()
            name = str(product.relative_to(root))
            key = fingerprint(info)
            expected = previous["products"].get(name)
            if (expected is not None and previous.get("fingerprints", {}).get(name) == key
                    and expected[:3] == [info.st_ino, info.st_size, info.st_mtime_ns]):
                digest = expected[3]
            else:
                digest = file_hash(product)
            if fingerprint(product.stat()) != key:
                continue  # A changing file is never journaled as disposable.
            products[name] = [info.st_ino, info.st_size, info.st_mtime_ns, digest]
            fingerprints[name] = key
    journal["builds"][build_key] = {
        "succeeded": True, "products": products, "fingerprints": fingerprints,
    }
    atomic_json(path, journal)


def retirement_plan(root):
    root = Path(root).resolve()
    journal = read_journal(root)
    tracked = set(subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0"))
    result = []
    for build, record in journal["builds"].items():
        relative_build = Path(build)
        if (relative_build.is_absolute() or ".." in relative_build.parts
                or relative_build.parts[:1] != ("build",) or len(relative_build.parts) < 2):
            raise ValueError("invalid build journal path")
        if not record["succeeded"]:
            continue
        for name, expected in record["products"].items():
            relative = Path(name)
            p = root / relative
            if (relative.is_absolute() or ".." in relative.parts or str(relative) in tracked
                    or not p.is_relative_to(root / relative_build)
                    or p.suffix not in {".o", ".a"} or not p.is_file()
                    or p.is_symlink() or p.resolve() != p):
                continue
            info = p.stat()
            if ([info.st_ino, info.st_size, info.st_mtime_ns] == expected[:3]
                    and file_hash(p) == expected[3]):
                result.append({"path": name, "identity": expected, "bytes": info.st_size})
    return result


def open_files():
    if not shutil.which("lsof"):
        raise ValueError("lsof is required to check for older, uncoordinated processes")
    opened = subprocess.run(["lsof", "-nP", "-F", "n"], capture_output=True, text=True, timeout=30)
    if opened.returncode not in (0, 1) or not opened.stdout:
        raise ValueError("could not inspect open files; no cleanup performed")
    return {line[1:] for line in opened.stdout.splitlines() if line.startswith("n/")}


def refuse_open_paths(paths):
    prefixes = tuple(str(path) for path in paths)
    if any(name == prefix or name.startswith(prefix + "/")
           for name in open_files() for prefix in prefixes):
        raise ValueError("storage is still in use; no maintenance performed")


def retire_builds(root, *, apply=False):
    root = Path(root).resolve()
    with operation(root, "retire build intermediates", reuse=False):
        plan = retirement_plan(root)
        if apply and plan:
            refuse_open_paths([root / "build"])
            for row in plan:
                p = root / row["path"]
                info = p.lstat()
                if (p.resolve() != p or [info.st_ino, info.st_size, info.st_mtime_ns] != row["identity"][:3]
                        or file_hash(p) != row["identity"][3]):
                    raise ValueError("build output changed during cleanup; stopping")
                p.unlink()
        return {"applied": apply, "files": len(plan), "logical_bytes": sum(r["bytes"] for r in plan),
                "note": "APFS shared blocks may make actual reclaimed space smaller"}
