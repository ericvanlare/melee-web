"""Source text of the native menu browser runtime for source-shape tests.

The runtime is split by scene into gameplay_menu_browser*.cpp units that share
gameplay_menu_browser_state.hpp. Each unit is appended whole, core first, so
order checks inside one function keep working wherever it lives. Diagnostics
precede Results, matching the original file's raw-PAD/schedule order.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MENU_BROWSER_SOURCES = (
    "src/gameplay_menu_browser.cpp",
    "src/gameplay_menu_browser_state.hpp",
    "src/gameplay_menu_browser_menu.cpp",
    "src/gameplay_menu_browser_match.cpp",
    "src/gameplay_menu_browser_diagnostics.cpp",
    "src/gameplay_menu_browser_results.cpp",
    "src/gameplay_menu_browser_observers.cpp",
)


def menu_browser_source() -> str:
    present = {path.relative_to(ROOT).as_posix() for path in (ROOT / "src").glob("gameplay_menu_browser*")}
    if present != set(MENU_BROWSER_SOURCES):
        raise AssertionError(f"menu browser units changed; update MENU_BROWSER_SOURCES: {sorted(present)}")
    return "".join((ROOT / path).read_text(encoding="utf-8") for path in MENU_BROWSER_SOURCES)
