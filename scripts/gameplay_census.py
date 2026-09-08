#!/usr/bin/env python3
"""Compile original game sources and report real Wasm portability/link failures.

This diagnostic does not replace the gameplay executable. No unresolved symbol
is stubbed or ignored. Reports and object files stay under ignored build/.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from bootstrap import read_lock, verify_sources
from gameplay_sources import prepare_sources

ROOT = Path(__file__).resolve().parents[1]
CORE = (
    "melee/ft/fighter.c", "melee/ft/ftanim.c", "melee/ft/ftaction.c",
    "melee/ft/ftparts.c", "melee/ft/ftcommon.c", "melee/ft/ftdata.c",
    "melee/ft/ftmotionstates.c", "melee/ft/ftcoll.c",
    "melee/ft/kinds/ftCommon/ftCo_Wait.c", "melee/ft/kinds/ftCommon/ftCo_Walk.c",
    "melee/ft/kinds/ftCommon/ftCo_Jump.c", "melee/ft/kinds/ftCommon/ftCo_Fall.c",
    "melee/pl/player.c", "melee/mp/mpcoll.c", "melee/mp/mplib.c",
    "melee/gr/ground.c", "melee/gr/stage.c", "melee/gr/grlast.c",
    "melee/lb/lbfile.c", "melee/lb/lbmemory.c", "melee/lb/lbheap.c",
    "melee/lb/lbarchive.c", "melee/lb/lbanim.c", "melee/lb/lbcommand.c",
    "sysdolphin/baselib/gobj.c", "sysdolphin/baselib/gobjinit.c",
    "sysdolphin/baselib/gobjproc.c", "sysdolphin/baselib/gobjplink.c",
    "sysdolphin/baselib/gobjgxlink.c", "sysdolphin/baselib/gobjobject.c",
    "sysdolphin/baselib/gobjuserdata.c", "sysdolphin/baselib/archive.c",
    "sysdolphin/baselib/objalloc.c", "sysdolphin/baselib/memory.c",
)


def source_list(root, scope, explicit):
    source_root = root / ".deps/melee/src"
    if explicit:
        names = explicit
    elif scope == "core":
        names = CORE
    elif scope == "native":
        text = (root / ".deps/melee/.nix/CMakeLists.txt").read_text()
        blocks = re.findall(r"set\(SOURCES\s+(.*?)\)", text, re.S)
        if len(blocks) != 1:
            raise ValueError("Expected one original native SOURCES list")
        names = []
        for token in blocks[0].split():
            if not token.startswith("src/") or not token.endswith(".c"):
                raise ValueError(f"Unsupported original source-list token: {token}")
            names.append(token.removeprefix("src/"))
    else:
        names = [str(path.relative_to(source_root)) for directory in ("melee", "sysdolphin/baselib")
                 for path in sorted((source_root / directory).rglob("*.c"))]
    if len(set(names)) != len(names):
        raise ValueError("Duplicate source entries")
    for name in names:
        path = Path(name)
        if (path.is_absolute() or ".." in path.parts or path.suffix != ".c" or
                not (source_root / path).resolve().is_relative_to(source_root.resolve()) or
                not (source_root / path).is_file()):
            raise ValueError(f"Invalid or missing original C source: {name}")
    return list(names)


def publish_report(path, report):
    """Readers see either the previous complete JSON document or the new one."""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=".report-", suffix=".json", delete=False) as file:
            temporary = Path(file.name)
            json.dump(report, file, indent=2)
            file.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def census(root, names, jobs, link_root):
    # Reject bad requests before generated-source preparation or output mutation.
    if link_root is not None and not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", link_root):
        raise ValueError("Link root must be a C symbol")
    if jobs < 1:
        raise ValueError("Census jobs must be positive")
    lock = read_lock(root)
    verify_sources(root, lock)
    source_root = prepare_sources(root, lock)
    sdk = root / ".deps/emsdk"
    compiler = sdk / "upstream/emscripten/emcc.py"
    version = (compiler.parent / "emscripten-version.txt").read_text().strip().strip('"')
    if version != lock["emscripten"]:
        raise ValueError("Emscripten version differs from the dependency lock")
    output = root / "build/gameplay-census"
    if (root / "build").is_symlink() or output.is_symlink():
        raise ValueError("Census output must be a local directory")
    env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
               EM_CACHE=str(compiler.parent / "cache"), EMSDK_PYTHON=sys.executable)
    common = [sys.executable, str(compiler), "-std=c11", "-DTARGET_PC", "-O1",
              "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
              "-I", str(source_root), "-I", str(root / ".deps/aurora/include"),
              "-include", str(root / "src/gameplay_compat.h")]
    normalized = lambda value: str(value).replace(str(root), "${ROOT}")
    # Dependency pins alone do not identify downstream compiler inputs. Hash
    # both reviewed patches and the complete local forced-include chain.
    inputs = ("patches/melee-gameplay.patch", "patches/aurora-browser.patch",
              "src/gameplay_compat.h", "src/hsd_probe_compat.h")
    report = {"schema": 1, "kind": "source-compilation-census", "status": "in_progress",
              "dependencies": lock,
              "provenance": {
                  "sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                             for name in inputs},
                  "compiler_command": ["${PYTHON}", *map(normalized, common[1:])],
                  "link_command": None,
              },
              "requested_sources": list(names), "requested_link_root": link_root,
              "source_count": len(names), "compiled_count": None,
              "units": [], "defined_symbols": {}, "unresolved_object_symbols": [],
              "runtime_validated": False, "link": None}
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "report.json"
    # Invalidate old success before replacing any objects/logs. Even a hard
    # interruption leaves an explicitly unfinished run, never stale success.
    publish_report(report_path, report)

    def compile_one(name):
        # Keep source subdirectories to avoid basename collisions.
        target = output / "objects" / (name + ".o")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.unlink(missing_ok=True)
        run = subprocess.run([*common, "-c", str(source_root / name), "-o", str(target)],
                             env=env, capture_output=True, text=True, timeout=120)
        diagnostic = (run.stdout + run.stderr).replace(str(root), "${ROOT}")
        target.with_suffix(".log").write_text(diagnostic)
        return {"source": name, "compiled": run.returncode == 0,
                "diagnostics": diagnostic,
                "object": str(target.relative_to(output)) if run.returncode == 0 else None}

    try:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            units = list(pool.map(compile_one, names))
        compiled = [unit for unit in units if unit["compiled"]]
        report.update(units=units, compiled_count=len(compiled))
        # This inventory includes references in discarded functions. Only the
        # optional strict link answers reachability for the requested root.
        symbols = {}
        nm = sdk / "upstream/bin/llvm-nm"
        undefined = set()
        for unit in compiled:
            run = subprocess.run([str(nm), "--format=posix", str(output / unit["object"])],
                                 check=True, capture_output=True, text=True, timeout=30)
            for line in run.stdout.splitlines():
                parts = line.split()
                if len(parts) < 2:
                    continue
                symbol, kind = parts[:2]
                if kind == "U":
                    undefined.add(symbol)
                elif kind.isupper() and kind not in {"N"}:
                    symbols.setdefault(symbol, []).append(unit["source"])
        report.update(defined_symbols=symbols,
                      unresolved_object_symbols=sorted(undefined - symbols.keys()))
        if link_root:
            link_output = output / "retained-root.wasm"
            link_output.unlink(missing_ok=True)
            command = [sys.executable, str(compiler), "-O1", "--no-entry",
                       "-sERROR_ON_UNDEFINED_SYMBOLS=1", f"-Wl,--export={link_root}",
                       "-Wl,--error-limit=0", *[str(output / unit["object"]) for unit in compiled],
                       "-o", str(link_output)]
            report["provenance"]["link_command"] = ["${PYTHON}", *map(normalized, command[1:])]
            run = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
            diagnostic = normalized(run.stdout + run.stderr)
            (output / "link.log").write_text(diagnostic)
            report["link"] = {"root": link_root, "linked": run.returncode == 0,
                              "all_requested_sources_compiled": len(compiled) == len(units),
                              "diagnostics": diagnostic,
                              "scope": "Selected game objects plus libc; no Aurora/system adapters linked"}
        # Complete means the diagnostic finished; compile/link failures remain
        # failures in their fields and exit status, never runtime validation.
        report["status"] = "complete"
        publish_report(report_path, report)
    except BaseException as error:
        report["status"] = "interrupted" if isinstance(error, KeyboardInterrupt) else "failed"
        report["failure"] = {"type": type(error).__name__, "message": normalized(error)}
        publish_report(report_path, report)
        raise
    print(f"Original sources compiled: {len(compiled)}/{len(units)}")
    for unit in units:
        if not unit["compiled"]:
            errors = [line for line in unit["diagnostics"].splitlines() if "error:" in line]
            print(f"  {unit['source']}: {errors[0] if errors else 'compiler failed; see log'}")
    print(f"Object references without a provider in this selection: {len(report['unresolved_object_symbols'])}")
    if report["link"]:
        print(f"Strict retained-root link: {'passed' if report['link']['linked'] else 'failed; see link.log'}")
    print("Report: build/gameplay-census/report.json (compilation evidence; no runtime validation)")
    return len(compiled) == len(units) and (not report["link"] or report["link"]["linked"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", choices=("core", "native", "all"), default="core")
    parser.add_argument("--source", action="append", help="Explicit path relative to original src/; repeatable")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 6))
    parser.add_argument("--link-root", help="Attempt a strict link retaining this original function")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        okay = census(ROOT, source_list(ROOT, args.scope, args.source), args.jobs, args.link_root)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit(f"gameplay census: {error}") from error
    raise SystemExit(0 if okay else 1)


if __name__ == "__main__":
    main()
