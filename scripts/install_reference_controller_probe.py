#!/usr/bin/env python3
"""Install a pinned controller probe into the private capture environment."""
import argparse
from pathlib import Path
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / "tools"), str(Path(__file__).resolve().parent)]
from reference_capture_environment import support_root
from reference_controller_probe import install_probe
from install_reference_capture import installation_guard


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=support_root())
    args = parser.parse_args()
    with installation_guard(args.root):
        installed = install_probe(args.manifest, args.root)
    print("Installed pinned controller discovery helper:", installed["binary_sha256"])


if __name__ == "__main__":
    main()
