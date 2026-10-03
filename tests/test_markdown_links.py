import os
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"\]\(([^)\s]+)\)")
FENCE = re.compile(r"```.*?```", re.S)
# Ignored local evidence directories are linked from notes but never tracked.
LOCAL_ONLY = ("work", "runs", "assets-local")


class MarkdownLinkTests(unittest.TestCase):
    def test_relative_links_resolve(self):
        files = subprocess.run(["git", "ls-files", "-z", "*.md"], cwd=ROOT, capture_output=True,
                               check=True).stdout.decode().split("\0")
        missing = []
        for name in filter(None, files):
            path = ROOT / name
            if not path.is_file():
                continue
            text = FENCE.sub("", path.read_text(encoding="utf-8", errors="replace"))
            for target in LINK.findall(text):
                target = target.split("#", 1)[0]
                if not target or re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("<"):
                    continue
                resolved = Path(os.path.normpath(path.parent / target))
                relative = os.path.relpath(resolved, ROOT).split(os.sep)
                if relative[0] in LOCAL_ONLY:
                    continue
                if not resolved.exists():
                    missing.append(f"{name}: {target}")
        self.assertEqual(missing, [], "Broken relative Markdown links")


if __name__ == "__main__":
    unittest.main()
