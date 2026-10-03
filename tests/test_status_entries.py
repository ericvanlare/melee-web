import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from status_index import entries  # noqa: E402

STATUS_SECTIONS = ["Adding or updating evidence", "Current acceptance boundaries"]


class StatusEntriesTests(unittest.TestCase):
    def test_status_page_stays_an_index(self):
        sections = re.findall(r"(?m)^## (.+)$", (ROOT / "STATUS.md").read_text(encoding="utf-8"))
        self.assertEqual(sections, STATUS_SECTIONS,
                         "Record new evidence as docs/status/YYYY-MM-DD-<slug>.md, not as a STATUS.md section")

    def test_entries_are_dated_and_titled(self):
        found = entries()
        self.assertTrue(found)
        titles = [title for _, title, _, _ in found]
        self.assertEqual(len(titles), len(set(titles)), "STATUS entry titles must be unique")

    def test_entry_links_resolve(self):
        link = re.compile(r"\]\(([^)#\s]+)")
        missing = []
        for _, _, _, path in entries():
            for target in link.findall(path.read_text(encoding="utf-8")):
                if re.match(r"^[a-z]+:", target) or target.startswith(("../../work/", "../../runs/")):
                    continue
                if not (path.parent / target).exists():
                    missing.append(f"{path.name}: {target}")
        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()
