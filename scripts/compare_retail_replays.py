#!/usr/bin/env python3
"""Compare two independent pinned retail replay candidate captures."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from retail_replay_validation import cli_main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(cli_main())
