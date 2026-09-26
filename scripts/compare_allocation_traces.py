#!/usr/bin/env python3
"""Compare retained original and browser allocation traces."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.allocation_trace_compare import main


if __name__ == "__main__":
    raise SystemExit(main())
