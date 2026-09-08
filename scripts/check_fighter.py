#!/usr/bin/env python3
"""Run the original Mario constructor, neutral processes and restart gate."""
import argparse
from pathlib import Path
import subprocess

from check_gameplay import ROOT, node_runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path, help="Local extracted runtime asset directory")
    args = parser.parse_args()
    try:
        assets = args.assets.expanduser().resolve(strict=True)
        required = ("PlCo.dat", "PlMr.dat", "PlMrNr.dat", "PlMrAJ.dat", "GrNLa.dat",
                    "ItCo.usd", "EfMrData.dat", "EfCoData.dat", "PdPm.dat", "sislib_font.bin")
        missing = [name for name in required if not (assets / name).is_file()]
        if missing:
            raise ValueError("Missing local runtime assets: " + ", ".join(missing))
        target = ROOT / "build/browser/fighter_runtime_probe.js"
        if not target.is_file():
            raise ValueError("Run scripts/build.py --target fighter first")
        subprocess.run([str(node_runtime()), str(target), str(assets)], cwd=ROOT,
                       check=True, timeout=120)
    except (OSError, ValueError, SyntaxError, subprocess.SubprocessError) as error:
        parser.exit(1, f"fighter checks: {error}\n")


if __name__ == "__main__":
    main()
