#!/usr/bin/env python3
"""Command-line entry point for the bounded allocation-history replay."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.allocation_history_replay import main


if __name__ == "__main__":
    raise SystemExit(main())
