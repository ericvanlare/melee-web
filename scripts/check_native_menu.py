#!/usr/bin/env python3
"""Run the local original-HSD CSS descriptor/lifetime gate, not menu acceptance."""
import argparse
from pathlib import Path
import subprocess
from check_gameplay import node_runtime

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--css", required=True, type=Path, help="Owned local MnSlChr.usd")
    parser.add_argument("--sis", type=Path, help="Owned local SdSlChr.usd")
    parser.add_argument("--sss", type=Path, help="Owned local MnSlMap.usd (requires --sis)")
    parser.add_argument("--cards", nargs=2, type=Path, metavar=("ICONS", "SCENE"),
                        help="Owned LbMcGame.usd and NtMemAc.usd (requires --sss)")
    args = parser.parse_args()
    if args.cards and not args.sss: parser.error("--cards requires --sss")
    if args.sss and not args.sis: parser.error("--sss requires --sis")
    try:
        asset = args.css.expanduser().resolve(strict=True)
        target = ROOT / "build/browser-release/native_menu_scene_trace.js"
        if not target.is_file():
            raise ValueError("Build native_menu_scene_trace in build/browser-release first")
        subprocess.run([str(node_runtime()), str(target), str(asset),
                        *([str(args.sis.expanduser().resolve(strict=True))] if args.sis else []),
                        *([str(args.sss.expanduser().resolve(strict=True))] if args.sss else []),
                        *([str(p.expanduser().resolve(strict=True)) for p in args.cards] if args.cards else [])],
                       cwd=ROOT, check=True, timeout=60)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"native menu check: {error}\n")

if __name__ == "__main__":
    main()
