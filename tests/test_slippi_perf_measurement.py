# SPDX-License-Identifier: GPL-3.0-or-later
"""CI discovery wrapper for the private HSD performance measurement control."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src/slippi_profile/tests"))

from test_perf_measurement import *  # noqa: F401,F403,E402
