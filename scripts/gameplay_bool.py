"""Preserve the original MSL int boolean in generated gameplay source only.

SDK/libc headers and pristine graphics source retain their native bool. This
small lexical transform edits identifier tokens, never comments or literals.
The composed diff is built in an isolated index/worktree and then checked by
bootstrap.patch_state exactly like the reviewed downstream patch.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import tempfile

ALIAS = "melee_source_bool"
ALIAS_DECLARATION = "typedef int melee_source_bool; /* Original MSL bool ABI; SDK bool is unchanged. */"


def replace_bool_tokens(text):
    # C translation phase2 removes backslash-newline before recognizing tokens.
    # Keep an origin map so comments/literals remain byte-for-byte unchanged.
    if "bool" not in text and "\\\n" not in text and "\\\r\n" not in text:
        return text
    logical, origins = [], []
    cursor = 0
    while cursor < len(text):
        if text.startswith("\\\r\n", cursor):
            cursor += 3
        elif text.startswith("\\\n", cursor):
            cursor += 2
        else:
            logical.append(text[cursor])
            origins.append(cursor)
            cursor += 1
    code = "".join(logical)
    replacements = []
    i = 0
    while i < len(code):
        start = i
        if code.startswith("//", i):
            end = code.find("\n", i + 2)
            i = len(code) if end < 0 else end
        elif code.startswith("/*", i):
            end = code.find("*/", i + 2)
            if end < 0:
                raise ValueError("Unterminated source comment in boolean migration")
            i = end + 2
        elif code[i] in "RuUL" and re.match(r'(?:u8|u|U|L)?R"', code[i:i + 4]):
            opening = code.find("(", i)
            quote = code.find('"', i)
            delimiter = code[quote + 1:opening]
            if opening < 0 or len(delimiter) > 16 or any(c.isspace() or c in "()\\" for c in delimiter):
                raise ValueError("Malformed raw source literal in boolean migration")
            end = code.find(")" + delimiter + '"', opening + 1)
            if end < 0:
                raise ValueError("Unterminated raw source literal in boolean migration")
            i = end + len(delimiter) + 2
        elif code[i] in "\"'":
            quote = code[i]
            i += 1
            while i < len(code):
                if code[i] == "\\":
                    i += 2
                elif code[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
            else:
                raise ValueError("Unterminated source literal in boolean migration")
        elif code[i].isdigit() or (code[i] == "." and i + 1 < len(code) and code[i + 1].isdigit()):
            # A preprocessing number may contain identifier characters (e.g.
            # exponent suffixes); it is not a standalone bool identifier.
            i += 1
            while i < len(code) and (code[i].isalnum() or code[i] in "_." or
                                    (code[i] in "+-" and code[i - 1] in "eEpP")):
                i += 1
        elif code[i].isalpha() or code[i] == "_":
            i += 1
            while i < len(code) and (code[i].isalnum() or code[i] == "_"):
                i += 1
            if code[start:i] == "bool":
                begin, end = origins[start], origins[i - 1] + 1
                splices = "".join(re.findall(r"\\(?:\r\n|\n)", text[begin:end]))
                replacements.append((begin, end, ALIAS + splices))
        else:
            i += 1
    output, cursor = [], 0
    for begin, end, replacement in replacements:
        output.extend((text[cursor:begin], replacement))
        cursor = end
    output.append(text[cursor:])
    return "".join(output)


def transform_source(path, text):
    """Melee/HSD implementation and declarations share one explicit int type."""
    path = Path(path)
    selected = (path.suffix in {".c", ".h"} and
                (path.parts[:2] in {("src", "melee"), ("src", "sysdolphin")} or
                 path.as_posix() == "src/Runtime/platform.h"))
    if not selected:
        return text
    result = replace_bool_tokens(text)
    if path.as_posix() == "src/Runtime/platform.h":
        marker = "#define RUNTIME_PLATFORM_H"
        if result.count(marker) != 1:
            raise ValueError("Source Runtime/platform.h guard changed; review boolean alias insertion")
        if ALIAS_DECLARATION not in result:
            result = result.replace(marker, marker + "\n\n" + ALIAS_DECLARATION, 1)
    return result


def composed_patch(repository, reviewed_patch):
    """Return deterministic HEAD→reviewed patch→boolean migration diff.

    Generated working files/index are not touched while constructing or caching
    this patch. Existing exact-diff verification decides whether it may apply.
    """
    repository, reviewed_patch = Path(repository), Path(reviewed_patch)
    if not reviewed_patch.is_file():
        raise ValueError(f"Missing reviewed patch: {reviewed_patch}")
    reviewed_bytes = reviewed_patch.read_bytes()
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repository).strip().decode()
    key = {
        "head": head,
        "reviewed": hashlib.sha256(reviewed_bytes).hexdigest(),
        "transform": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    target = repository / ".git/melee-web-composed.patch"
    manifest = repository / ".git/melee-web-composed.json"
    if target.is_file() and manifest.is_file():
        try:
            cached = json.loads(manifest.read_text())
        except (ValueError, OSError):
            cached = {}
        if cached.get("key") == key and cached.get("patch_sha256") == hashlib.sha256(target.read_bytes()).hexdigest():
            return target
    with tempfile.TemporaryDirectory(prefix="melee-bool-source-") as temporary:
        temp = Path(temporary)
        worktree = temp / "tree"
        worktree.mkdir()
        env = dict(os.environ, GIT_INDEX_FILE=str(temp / "index"), GIT_WORK_TREE=str(worktree))
        subprocess.run(["git", "read-tree", "HEAD"], cwd=repository, env=env, check=True)
        # Validate against HEAD before any generated checkout may be reversed.
        subprocess.run(["git", "apply", "--cached", str(reviewed_patch.resolve())], cwd=repository, env=env, check=True)
        subprocess.run(["git", "checkout-index", "--all", "--force"], cwd=repository, env=env, check=True)
        listing = subprocess.check_output(["git", "ls-files", "-z"], cwd=repository, env=env)
        for encoded in listing.split(b"\0"):
            if not encoded:
                continue
            relative = Path(os.fsdecode(encoded))
            path = worktree / relative
            if path.suffix not in {".c", ".h"} or path.is_symlink():
                continue
            # newline='' preserves every non-token byte, including CRLF.
            with path.open(encoding="utf-8", errors="surrogateescape", newline="") as stream:
                before = stream.read()
            after = transform_source(relative, before)
            if after != before:
                with path.open("w", encoding="utf-8", errors="surrogateescape", newline="") as stream:
                    stream.write(after)
        subprocess.run(["git", "add", "--all", "--", "."], cwd=worktree, env=dict(env, GIT_DIR=str(repository / ".git")), check=True)
        result = subprocess.check_output(["git", "diff", "--no-ext-diff", "--no-color", "--cached", "--binary", "HEAD"], cwd=repository, env=env)
    if not target.is_file() or target.read_bytes() != result:
        staged = target.with_suffix(".patch.tmp")
        staged.write_bytes(result)
        staged.replace(target)
    payload = json.dumps({"key": key, "patch_sha256": hashlib.sha256(result).hexdigest()}, sort_keys=True) + "\n"
    manifest.write_text(payload)
    return target
