"""Share verified identical installed toolchain bytes using APFS copy-on-write.

Only the current checkout is changed, after its normal pinned installation.
Other checkouts are read-only candidates; no hardlinks or writable shared SDKs.
"""
import ctypes
import errno
import hashlib
import os
from pathlib import Path
import secrets
import stat
import subprocess
import sys


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(8 * 1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def identity(path):
    s = path.lstat()
    return (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns,
            s.st_mode, s.st_uid, s.st_gid, getattr(s, "st_flags", 0), s.st_nlink)


def attributes(path):
    names = subprocess.check_output(["/usr/bin/xattr", str(path)], text=True).splitlines()
    if "com.apple.ResourceFork" in names or "com.apple.decmpfs" in names:
        raise ValueError("existing fork or compression")
    if len(subprocess.check_output(["ls", "-lde", str(path)], text=True).splitlines()) != 1:
        raise ValueError("nontrivial ACL")
    return {name: "".join(subprocess.check_output(
        ["/usr/bin/xattr", "-px", name, str(path)], text=True).split()) for name in names}


def clone_file(source, destination):
    library = ctypes.CDLL("/usr/lib/libSystem.B.dylib", use_errno=True)
    clone = library.clonefile
    clone.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    clone.restype = ctypes.c_int
    if clone(os.fsencode(source), os.fsencode(destination), 0):
        raise OSError(ctypes.get_errno(), "APFS clonefile failed")


def share_file(source, target):
    """Replace a target only when bytes, metadata and both stable identities agree."""
    source, target = Path(source), Path(target)
    source_before, target_before = identity(source), identity(target)
    s, t = source.lstat(), target.lstat()
    if (source.resolve() != source or target.resolve() != target
            or not stat.S_ISREG(s.st_mode) or not stat.S_ISREG(t.st_mode)
            or s.st_nlink != 1 or t.st_nlink != 1 or getattr(s, "st_flags", 0) or getattr(t, "st_flags", 0)
            or (s.st_dev, s.st_size, s.st_mode, s.st_uid, s.st_gid)
            != (t.st_dev, t.st_size, t.st_mode, t.st_uid, t.st_gid)):
        return False
    source_attrs, target_attrs = attributes(source), attributes(target)
    sha = digest(target)
    if source_attrs != target_attrs or digest(source) != sha:
        return False
    temporary = target.with_name(".share-" + secrets.token_hex(16) + ".tmp")
    try:
        clone_file(source, temporary)
        os.utime(temporary, ns=(t.st_atime_ns, t.st_mtime_ns))
        cloned = temporary.stat()
        if ((cloned.st_size, cloned.st_mode, cloned.st_uid, cloned.st_gid, cloned.st_mtime_ns)
                != (t.st_size, t.st_mode, t.st_uid, t.st_gid, t.st_mtime_ns)
                or attributes(temporary) != target_attrs or digest(temporary) != sha
                or cloned.st_ino == s.st_ino):
            raise ValueError("clone verification failed")
        with temporary.open("rb") as handle:
            os.fsync(handle.fileno())
        if identity(source) != source_before or identity(target) != target_before:
            raise ValueError("toolchain changed during sharing")
        os.utime(temporary, ns=(t.st_atime_ns, t.st_mtime_ns))
        temporary.replace(target)
        return True
    finally:
        temporary.unlink(missing_ok=True)


def share_installed_toolchain(root):
    root = Path(root).resolve()
    if sys.platform != "darwin":
        return {"shared_files": 0, "reason": "APFS optimization is macOS-only"}
    try:
        records = subprocess.check_output(["git", "worktree", "list", "--porcelain", "-z"], cwd=root)
    except subprocess.CalledProcessError:
        return {"shared_files": 0, "reason": "no sibling Git worktrees"}
    lock = (root / "dependencies.lock.json").read_bytes()
    peers = []
    for record in records.split(b"\0"):
        if record.startswith(b"worktree "):
            peer = Path(os.fsdecode(record[len(b"worktree "):]))
            if peer == root or peer.resolve() != peer:
                continue
            try:
                if (peer / "dependencies.lock.json").read_bytes() == lock:
                    peers.append(peer)
            except OSError:
                continue
    # Bound the search, but allow an incompatible first peer (permissions or
    # partial installs) without preventing useful sharing with the next one.
    # Hash equality, not the peer's cleanliness, is authority.
    peers = [p for p in peers if (p / ".deps/emsdk/upstream/bin/clang").exists()
             and (p / ".venv").is_dir()][:3]
    if not peers:
        return {"shared_files": 0, "reason": "no compatible installed peer"}
    shared = skipped = 0
    for directory in (root / ".deps/emsdk", root / ".venv"):
        for parent, directories, files in os.walk(directory, followlinks=False):
            directories[:] = [d for d in directories if d != ".git"]
            for name in files:
                target = Path(parent) / name
                if target.lstat().st_size < 1024 * 1024:
                    continue
                for peer in peers:
                    source = peer / target.relative_to(root)
                    try:
                        if source.is_file() and share_file(source, target):
                            shared += 1
                            break
                    except OSError as error:
                        if error.errno in (errno.EXDEV, errno.ENOTSUP):
                            return {"shared_files": shared, "reason": "filesystem does not support APFS clones"}
                    except (ValueError, subprocess.SubprocessError):
                        pass
                else:
                    skipped += 1
    return {"shared_files": shared, "skipped_files": skipped}
