#!/usr/bin/env python3
"""CLI entry point for hash-bound MWRC save-profile preparation."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from retail_save_profile import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
