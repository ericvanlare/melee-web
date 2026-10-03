#!/usr/bin/env python3
"""Move STATUS.md sections that a branch added or changed into docs/status/ entries.

STATUS.md used to hold every evidence section. A branch started before the
split edits STATUS.md and conflicts with the index. Run this on the branch,
before or during the merge with main, and resolve STATUS.md to main's version:

    python3 scripts/migrate_status_sections.py --base $(git merge-base HEAD origin/main) --head HEAD
    git checkout origin/main -- STATUS.md

A section the branch added becomes a new dated entry. A section it changed
replaces the entry with the same title. Relative links are rebased.
"""
from datetime import date
from pathlib import Path
import argparse
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
STATUS = ROOT / "docs/status"
KEEP = {"Current status", "Adding or updating evidence", "Current acceptance boundaries"}
LINK = re.compile(r"(\]\()([^)\s#]+)((?:#[^)\s]*)?\))")


def sections(text):
    parts = re.split(r"(?m)^(?=## )", text)
    return {part.split("\n", 1)[0][3:].strip(): part for part in parts[1:]}


def slug(heading):
    return re.sub(r"[^a-z0-9]+", "-", heading.lower()).strip("-")[:60].rstrip("-")


def entry_text(section, moves=None):
    moves = moves or {}
    def rebase(match):
        target = match.group(2)
        if re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("<"):
            return match.group(0)
        source = Path(os.path.normpath(ROOT / target))
        destination = moves.get(source, source)
        rel = os.path.relpath(destination, STATUS)
        return f"{match.group(1)}{Path(rel).as_posix()}{match.group(3)}"
    return LINK.sub(rebase, "# " + section[3:]).rstrip("\n") + "\n"


def existing_entries():
    found = {}
    for path in STATUS.glob("*.md"):
        first = path.read_text(encoding="utf-8").split("\n", 1)[0]
        if first.startswith("# "):
            found[first[2:].strip()] = path
    return found


def show(revision):
    return subprocess.run(["git", "show", f"{revision}:STATUS.md"], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout


def documentation_moves(base):
    """Follow Git's detected documentation renames in the receiving tree."""
    fields = subprocess.check_output(
        ["git", "diff", "--name-status", "-z", "--find-renames", base, "--", "docs"],
        cwd=ROOT).decode().split("\0")
    moves = {}
    index = 0
    while index < len(fields) and fields[index]:
        status = fields[index]
        if status.startswith(("R", "C")):
            old, new = fields[index + 1:index + 3]
            if status.startswith("R"):
                moves[ROOT / old] = ROOT / new
            index += 3
        else:
            index += 2
    return moves


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", required=True, help="revision before the branch's STATUS edits")
    parser.add_argument("--head", default="HEAD", help="revision with the branch's STATUS edits")
    parser.add_argument("--date", default=date.today().isoformat(), help="date for new entries")
    args = parser.parse_args(argv)
    before, after = sections(show(args.base)), sections(show(args.head))
    entries = existing_entries()
    moves = documentation_moves(args.base)
    written = []
    for heading, section in after.items():
        if heading in KEEP or before.get(heading) == section:
            continue
        path = entries.get(heading) or STATUS / f"{args.date}-{slug(heading)}.md"
        path.write_text(entry_text(section, moves), encoding="utf-8")
        written.append(path)
    removed = [heading for heading in before if heading not in after and heading not in KEEP]
    for path in written:
        print(path.relative_to(ROOT))
    if removed:
        print("Sections removed on the branch (handle their entries by hand): " + "; ".join(removed),
              file=sys.stderr)
    if not written:
        print("No added or changed STATUS sections", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
