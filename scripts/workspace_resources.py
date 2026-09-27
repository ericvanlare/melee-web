"""Cooperative local build leases and a journal of disposable compiler products.

Locks are per user across worktrees. They never stop processes or delete evidence.
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
import time


LEASE_ENV = "MELEE_WORKSPACE_LEASE"
DEFAULT_JOBS = 2


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


def check_space(root):
    # Hosted runners are disposable and smaller; local agents keep a larger reserve.
    reserve = float(os.environ.get("MELEE_MIN_FREE_GB", "5" if os.environ.get("CI") else "30"))
    if not 0 < reserve < 100000:
        raise ValueError("MELEE_MIN_FREE_GB must be a positive finite number")
    free = shutil.disk_usage(root).free / 1e9
    if free < reserve:
        raise ValueError(f"Only {free:.1f} GB free; this operation requires {reserve:g} GB. "
                         "Retire owned build output or free space before retrying.")


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


def _inherited_lease(root, directory):
    """A nested command may share its live ancestor's operation, never a stale token."""
    import fcntl
    try:
        token = json.loads(os.environ.get(LEASE_ENV, "null"))
        if not isinstance(token, dict) or token.get("root") != str(root):
            return False
        if token.get("slot") not in (0, 1):
            return False
        with _open_lock(directory / f"slot-{token['slot']}.lock") as handle:
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


@contextmanager
def operation(root, label, *, heavy=True, timeout=300):
    """Serialize a checkout and allow at most two cooperating heavy operations per user."""
    root = Path(root).resolve(strict=True)
    if os.name != "posix":
        if heavy:
            check_space(root)
        print("workspace: host-wide build locks require macOS or Linux", file=sys.stderr)
        yield
        return
    directory = state_directory()
    if _inherited_lease(root, directory):
        yield
        return
    key = hashlib.sha256(os.fsencode(root)).hexdigest()
    started = time.monotonic()
    locks = []
    previous = os.environ.get(LEASE_ENV)
    before = disk_status(root)
    print(f"workspace: {label}: {before['free_gb']:.1f} GB free", file=sys.stderr)
    try:
        checkout = _open_lock(directory / f"checkout-{key}.lock")
        locks.append(checkout)
        while not _try_lock(checkout):
            if time.monotonic() - started >= timeout:
                raise ValueError("checkout is busy; retry after its current operation finishes")
            time.sleep(0.2)
        if heavy:
            check_space(root)
            slots = []
            for n in range(2):
                handle = _open_lock(directory / f"slot-{n}.lock")
                slots.append(handle)
                locks.append(handle)
            while True:
                acquired = next((n for n, handle in enumerate(slots) if _try_lock(handle)), None)
                if acquired is not None:
                    break
                if time.monotonic() - started >= timeout:
                    raise ValueError("both build slots are occupied; retry after a build finishes")
                time.sleep(0.2)
            check_space(root)
            token = {"pid": os.getpid(), "root": str(root), "slot": acquired,
                     "nonce": secrets.token_hex(16)}
            handle = slots[acquired]
            handle.seek(0)
            handle.truncate()
            json.dump(token, handle)
            handle.flush()
            os.environ[LEASE_ENV] = json.dumps(token)
        yield
    finally:
        if previous is None:
            os.environ.pop(LEASE_ENV, None)
        else:
            os.environ[LEASE_ENV] = previous
        for handle in reversed(locks):
            handle.close()
        after = disk_status(root)
        print(f"workspace: {label}: finished with {after['free_gb']:.1f} GB free "
              f"({after['free_gb'] - before['free_gb']:+.1f} GB)", file=sys.stderr)


def _journal(root):
    return Path(root) / ".cache/workspace/builds.json"


def file_hash(path):
    result = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


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
    products = {}
    ninja_log = build_dir / ".ninja_log"
    if ninja_log.is_symlink() or ninja_log.resolve() != ninja_log:
        raise ValueError("refusing redirected Ninja output journal")
    if succeeded and ninja_log.is_file():
        for line in ninja_log.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) != 5:
                continue
            output = Path(fields[3])
            if output.is_absolute() or ".." in output.parts or output.suffix not in {".o", ".a"}:
                continue
            product = build_dir / output
            if product.is_symlink() or product.resolve() != product or not product.is_file():
                continue
            info = product.stat()
            products[str(product.relative_to(root))] = [info.st_ino, info.st_size, info.st_mtime_ns,
                                                       file_hash(product)]
    journal["builds"][str(build_dir.relative_to(root))] = {
        "succeeded": succeeded, "products": products,
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


def retire_builds(root, *, apply=False):
    root = Path(root).resolve()
    with operation(root, "retire build intermediates", heavy=False, timeout=0):
        plan = retirement_plan(root)
        if apply and plan:
            if any(name == str(root / "build") or name.startswith(str(root / "build") + "/")
                   for name in open_files()):
                raise ValueError("build directory is still in use; no cleanup performed")
            for row in plan:
                p = root / row["path"]
                info = p.lstat()
                if (p.resolve() != p or [info.st_ino, info.st_size, info.st_mtime_ns] != row["identity"][:3]
                        or file_hash(p) != row["identity"][3]):
                    raise ValueError("build output changed during cleanup; stopping")
                p.unlink()
        return {"applied": apply, "files": len(plan), "logical_bytes": sum(r["bytes"] for r in plan),
                "note": "APFS shared blocks may make actual reclaimed space smaller"}
