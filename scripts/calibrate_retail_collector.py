#!/usr/bin/env python3
"""CLI wrapper for the read-only retail collector calibration gate."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from retail_collector_calibration import cli_main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(cli_main())
