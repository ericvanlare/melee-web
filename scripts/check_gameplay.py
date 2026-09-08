#!/usr/bin/env python3
"""Run the separate Wasm gameplay checks and optional local-data probe."""
import argparse
import ast
import os
from pathlib import Path
import subprocess

from bootstrap import read_lock

ROOT = Path(__file__).resolve().parents[1]


def node_runtime(root=ROOT):
    sdk = root / ".deps/emsdk"
    config = sdk / ".emscripten"
    lock = read_lock(root)
    version = (sdk / "upstream/emscripten/emscripten-version.txt").read_text().strip().strip('"')
    if version != lock["emscripten"]:
        raise ValueError("Project Emscripten version differs from dependency lock")
    settings = [ast.literal_eval(statement.value) for statement in ast.parse(config.read_text()).body
                if isinstance(statement, ast.Assign) and any(isinstance(target, ast.Name)
                    and target.id == "NODE_JS" for target in statement.targets)]
    if len(settings) != 1 or not isinstance(settings[0], str):
        raise ValueError("Expected one project-local Node setting")
    node = Path(settings[0].replace("$CFGDIR", str(sdk))).resolve()
    if not node.is_relative_to(sdk.resolve()) or not node.is_file():
        raise ValueError("Configured Node must belong to the project SDK")
    return node


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--common", type=Path, help="Optional local PlCo.dat")
    parser.add_argument("--stage", type=Path, help="Optional local stage DAT for collision decoding")
    parser.add_argument("--stage-kind", type=int, help="Explicit original GrKind to run static collision queries (FD: 37)")
    parser.add_argument("--fighter", type=Path, help="Optional local fighter metadata DAT")
    parser.add_argument("--fighter-symbol", help="Exact source costume model symbol for fighter metadata")
    parser.add_argument("--animations", type=Path, help="Optional local full fighter animation container")
    parser.add_argument("--motion", type=int, help="Exact source motion ID to decode from the container")
    parser.add_argument("--costume", type=Path, help="Optional local costume DAT for original native HSD loading")
    args = parser.parse_args()
    try:
        if args.stage_kind is not None and args.stage is None:
            raise ValueError("--stage-kind requires --stage")
        if args.costume is not None and (args.common is None or not args.fighter_symbol):
            raise ValueError("--costume requires --common and --fighter-symbol")
        node = node_runtime()
        build = ROOT / "build/browser"
        if (ROOT / "build").is_symlink() or build.is_symlink():
            raise ValueError("Build output must be a local directory")
        targets = [build / (name + ".js") for name in
                   ("gameplay_abi_trace", "gameplay_scheduler_trace", "gameplay_collision_trace", "hsd_native_trace", "gameplay_probe")]
        if not all(target.is_file() for target in targets):
            raise ValueError("Gameplay checks are not built; run scripts/build.py --target gameplay")
        arguments = []
        for option, path in (("--common", args.common), ("--stage", args.stage),
                             ("--fighter", args.fighter), ("--animations", args.animations)):
            if path is not None:
                arguments.extend((option, str(path.expanduser().resolve(strict=True))))
        if args.stage_kind is not None:
            arguments.extend(("--stage-kind", str(args.stage_kind)))
        if args.fighter_symbol is not None:
            arguments.extend(("--fighter-symbol", args.fighter_symbol))
        if args.motion is not None:
            arguments.extend(("--motion", str(args.motion)))
        env = dict(os.environ)
        for target in targets:
            subprocess.run([str(node), str(target), *(arguments if target == targets[-1] else [])],
                           cwd=ROOT, env=env, check=True, timeout=60)
        if args.common is not None:
            native_arguments = [str(args.common.expanduser().resolve(strict=True))]
            if args.costume is not None:
                native_arguments += [str(args.costume.expanduser().resolve(strict=True)), args.fighter_symbol]
            subprocess.run([str(node), str(build / "hsd_native_dat_probe.js"), *native_arguments],
                           cwd=ROOT, env=env, check=True, timeout=60)
    except (OSError, ValueError, SyntaxError, subprocess.SubprocessError) as error:
        parser.exit(1, f"gameplay checks: {error}\n")


if __name__ == "__main__":
    main()
