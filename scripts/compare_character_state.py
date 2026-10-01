#!/usr/bin/env python3
"""CLI wrapper for the exact character-state prefix comparator."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.character_state_compare import main  # noqa: E402


if __name__ == "__main__":
    sys.exit(main())
