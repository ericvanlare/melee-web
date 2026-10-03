#!/usr/bin/env python3
"""List STATUS evidence entries in docs/status/, newest first."""
from pathlib import Path
import argparse
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
ENTRY = re.compile(r"^(\d{4}-\d{2}-\d{2})-[a-z0-9-]+\.md$")
LABELS = re.compile(r"^\*\*([A-Z][A-Za-z ]+(?: / [A-Z][A-Za-z ]+)*)\*\*")


def entries(directory=ROOT / "docs/status"):
    """Return (date, title, labels, path) tuples, newest first; reject bad names."""
    found, invalid = [], []
    for path in sorted(directory.glob("*.md")):
        match = ENTRY.match(path.name)
        lines = path.read_text(encoding="utf-8").splitlines()
        if not match or not lines or not lines[0].startswith("# "):
            invalid.append(path)
            continue
        labels = next((m.group(1) for line in lines[1:12] if (m := LABELS.match(line))), "")
        found.append((match.group(1), lines[0][2:].strip(), labels, path))
    if invalid:
        raise ValueError("Invalid STATUS entries (need YYYY-MM-DD-slug.md and a '# Title' first line): "
                         + ", ".join(str(path.relative_to(ROOT)) for path in invalid))
    return sorted(found, key=lambda entry: (entry[0], entry[3].name), reverse=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--markdown", action="store_true", help="print a Markdown list with links")
    args = parser.parse_args(argv)
    for date, title, labels, path in entries():
        relative = path.relative_to(ROOT).as_posix()
        suffix = f" — {labels}" if labels else ""
        print(f"- {date} [{title}]({relative}){suffix}" if args.markdown else f"{date}  {title}{suffix}\n            {relative}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
