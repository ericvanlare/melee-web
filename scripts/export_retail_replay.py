#!/usr/bin/env python3
"""Export two repeatable retail replay candidates as an MWRC v1 recipe."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from retail_replay_recipe import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
