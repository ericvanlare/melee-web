"""Focused safety and format checks for the bounded ccache audit."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import audit_compiler_cache as AUDIT  # noqa: E402


def _wasm_relocatable() -> bytes:
    # Header plus one valid custom section whose name is "linking".
    return b"\x00asm\x01\x00\x00\x00\x00\x08\x07linking"


def _make_fake_ccache(directory: Path) -> Path:
    path = directory / "fake-ccache"
    path.write_text(
        "#!" + sys.executable + "\n"
        "import os\n"
        "from pathlib import Path\n"
        "import sys\n"
        "if '--version' in sys.argv:\n"
        "    print('ccache version 4.9.1')\n"
        "    raise SystemExit(0)\n"
        "mode = os.environ.get('FAKE_CCACHE_MODE', 'normal')\n"
        "entry = Path(sys.argv[-1])\n"
        "if '--inspect' in sys.argv:\n"
        "    if mode == 'timeout':\n"
        "        import time\n"
        "        time.sleep(3)\n"
        "    if mode == 'output':\n"
        "        sys.stdout.write('x' * 8192)\n"
        "        sys.stdout.flush()\n"
        "        raise SystemExit(0)\n"
        "    print('source=' + '/home/' + 'runner/work/project/src/a.c')\n"
        "    if mode == 'redact':\n"
        "        print('source=' + '/Users/' + 'alice/private/a.c')\n"
        "        print('password=\\\"' + 'S' * 24 + '\\\"')\n"
        "    raise SystemExit(0)\n"
        "if '--extract-result' in sys.argv:\n"
        "    if mode == 'fail':\n"
        "        print('decoder failed at private path ' + '/Users/' + 'alice/secret', file=sys.stderr)\n"
        "        raise SystemExit(7)\n"
        "    Path('ccache-result.o').write_bytes(" + repr(_wasm_relocatable()) + ")\n"
        "    if mode == 'redact':\n"
        "        Path('ccache-result.d').write_text('/Users/' + 'alice/assets-local/disc.d ' + '/home/' + 'runner/work/project/src/a.c:\\n')\n"
        "    raise SystemExit(0)\n"
        "raise SystemExit(2)\n",
        encoding="utf-8",
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    return path


def _cache_entry(cache: Path, *, digest_char: str = "a", kind: str = "R") -> Path:
    fanout = cache / digest_char / digest_char
    fanout.mkdir(parents=True, exist_ok=True)
    path = fanout / (digest_char * 2 + "v" * 28 + "u" + kind)
    path.write_bytes(b"synthetic ccache entry")
    return path


class CompilerCacheAuditTests(unittest.TestCase):
    def test_ccache_mixed_hex_base32_identity(self):
        # Regression from the real 4.9.1 runner: local names are not 40 hex
        # characters. The format uses base32hex after the four hex digits.
        digest = "abcd" + "v" * 28 + "u"
        for depth in (2, 3, 4):
            with self.subTest(depth=depth):
                self.assertEqual(AUDIT._entry_kind(Path(digest[depth:] + "R"), tuple(digest[:depth])), "result")
        self.assertIsNone(AUDIT._entry_kind(Path("a" * 38 + "R"), ("a", "a")))
        self.assertIsNone(AUDIT._entry_kind(Path(digest[2:-1] + "vR"), ("a", "b")))

    def test_empty_cache_is_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = _make_fake_ccache(root)
            (root / "cache").mkdir()
            report = AUDIT.audit(root / "cache", ccache=fake)
            self.assertEqual(report["status"], "incomplete")
            self.assertIn("empty_cache", {item["rule"] for item in report["findings"]})

    def test_unreadable_subtree_is_incomplete_and_cli_redacts_the_error(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            _cache_entry(cache, digest_char="a")
            hidden = _cache_entry(cache, digest_char="b")
            fake = _make_fake_ccache(root)
            readable = AUDIT.audit(cache, ccache=fake)
            self.assertEqual(readable["status"], "passed")
            self.assertEqual(readable["cache"]["regular_files"], 2)
            unreadable = hidden.parent
            original_mode = stat.S_IMODE(unreadable.stat().st_mode)
            unreadable.chmod(0)
            try:
                try:
                    list(unreadable.iterdir())
                except PermissionError:
                    pass
                else:
                    self.skipTest("This environment bypasses directory permissions")
                output = root / "audit.json"
                result = subprocess.run(
                    [sys.executable, str(ROOT / "scripts/audit_compiler_cache.py"),
                     "--cache-dir", str(cache), "--output", str(output), "--ccache", str(fake)],
                    text=True, capture_output=True, check=False,
                )
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                report = json.loads(output.read_text(encoding="utf-8"))
                self.assertEqual(report["status"], "incomplete")
                self.assertIn("audit_incomplete", {item["rule"] for item in report["findings"]})
                self.assertNotIn(str(root), result.stdout + result.stderr + json.dumps(report))
            finally:
                unreadable.chmod(original_mode)

    def test_missing_decode_is_incomplete_and_stderr_is_not_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            entry = _cache_entry(cache)
            fake = _make_fake_ccache(root)
            old_mode = os.environ.get("FAKE_CCACHE_MODE")
            os.environ["FAKE_CCACHE_MODE"] = "fail"
            try:
                report = AUDIT.audit(cache, ccache=fake)
            finally:
                if old_mode is None:
                    os.environ.pop("FAKE_CCACHE_MODE", None)
                else:
                    os.environ["FAKE_CCACHE_MODE"] = old_mode
            self.assertEqual(report["status"], "incomplete")
            self.assertIn("extract_failed", {item["rule"] for item in report["findings"]})
            encoded = json.dumps(report)
            self.assertNotIn("/Users/" + "alice/secret", encoded)
            self.assertIn(entry.name, encoded)

    def test_unsafe_entries_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            fanout = cache / "a" / "b"
            fanout.mkdir(parents=True)
            unknown = fanout / "unexpected"
            unknown.write_bytes(b"unknown")
            target = root / "outside"
            target.write_bytes(b"target")
            link = fanout / ("a" * 2 + "v" * 28 + "uR")
            try:
                link.symlink_to(target)
            except OSError:
                self.skipTest("symlinks are unavailable")
            fake = _make_fake_ccache(root)
            report = AUDIT.audit(cache, ccache=fake)
            rules = {item["rule"] for item in report["findings"]}
            self.assertEqual(report["status"], "incomplete")
            self.assertIn("symlink_entry", rules)
            self.assertIn("unknown_cache_file", rules)
            self.assertNotIn(str(target), json.dumps(report))

    def test_redaction_runner_paths_assets_and_decoded_types(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            _cache_entry(cache)
            # Metadata files are part of the cache inventory and are scanned.
            (cache / "a" / "CACHEDIR.TAG").write_bytes(b"/Users/" + b"alice/cache/\n")
            (cache / "ccache.conf").write_bytes(b"cache_dir=/Users/" + b"alice/cache\n")
            fake = _make_fake_ccache(root)
            old_mode = os.environ.get("FAKE_CCACHE_MODE")
            os.environ["FAKE_CCACHE_MODE"] = "redact"
            try:
                report = AUDIT.audit(cache, ccache=fake)
            finally:
                if old_mode is None:
                    os.environ.pop("FAKE_CCACHE_MODE", None)
                else:
                    os.environ["FAKE_CCACHE_MODE"] = old_mode
            self.assertEqual(report["status"], "review")
            rules = {item["rule"] for item in report["findings"]}
            self.assertIn("sensitive_assignment", rules)
            self.assertIn("personal_path", rules)
            self.assertIn("asset_or_capture_input", rules)
            self.assertEqual(report["decoded_type_counts"], {"depfile": 1, "wasm_relocatable": 1})
            self.assertGreater(report["scan_counts"]["runner_path"], 0)
            encoded = json.dumps(report)
            self.assertNotIn("/Users/" + "alice", encoded)
            self.assertNotIn("/home/" + "runner", encoded)
            self.assertNotIn("password=", encoded)

    def test_multiple_results_use_isolated_decode_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            _cache_entry(cache, digest_char="a")
            _cache_entry(cache, digest_char="b")
            fake = _make_fake_ccache(root)
            report = AUDIT.audit(cache, ccache=fake)
            self.assertEqual(report["status"], "passed")
            self.assertEqual(report["cache"]["result_entries"], 2)
            self.assertEqual([len(entry["decoded"]) for entry in report["entries"]], [1, 1])
            self.assertEqual(report["decoded_type_counts"], {"wasm_relocatable": 2})

    def test_cli_output_is_exclusive_and_summary_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            _cache_entry(cache)
            fake = _make_fake_ccache(root)
            output = root / "audit.json"
            command = [sys.executable, str(ROOT / "scripts/audit_compiler_cache.py"),
                       "--cache-dir", str(cache), "--output", str(output), "--ccache", str(fake)]
            first = subprocess.run(command, text=True, capture_output=True, check=False)
            second = subprocess.run(command, text=True, capture_output=True, check=False)
            self.assertEqual(first.returncode, 0)
            self.assertEqual(second.returncode, 2)
            self.assertIn('"status": "passed"', first.stdout)
            self.assertNotIn('"entries": [', first.stdout)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["status"], "passed")

    def test_decoder_timeout_is_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            _cache_entry(cache)
            fake = _make_fake_ccache(root)
            old_mode, old_timeout = os.environ.get("FAKE_CCACHE_MODE"), AUDIT.COMMAND_TIMEOUT_SECONDS
            os.environ["FAKE_CCACHE_MODE"] = "timeout"
            AUDIT.COMMAND_TIMEOUT_SECONDS = 1
            try:
                report = AUDIT.audit(cache, ccache=fake)
            finally:
                AUDIT.COMMAND_TIMEOUT_SECONDS = old_timeout
                if old_mode is None:
                    os.environ.pop("FAKE_CCACHE_MODE", None)
                else:
                    os.environ["FAKE_CCACHE_MODE"] = old_mode
            self.assertIn("inspect_timeout", {item["rule"] for item in report["findings"]})

    def test_inspect_output_limit_is_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            _cache_entry(cache)
            fake = _make_fake_ccache(root)
            old_mode, old_limit = os.environ.get("FAKE_CCACHE_MODE"), AUDIT.MAX_COMMAND_OUTPUT_BYTES
            os.environ["FAKE_CCACHE_MODE"] = "output"
            AUDIT.MAX_COMMAND_OUTPUT_BYTES = 1024
            try:
                report = AUDIT.audit(cache, ccache=fake)
            finally:
                AUDIT.MAX_COMMAND_OUTPUT_BYTES = old_limit
                if old_mode is None:
                    os.environ.pop("FAKE_CCACHE_MODE", None)
                else:
                    os.environ["FAKE_CCACHE_MODE"] = old_mode
            self.assertIn("inspect_output_limit", {item["rule"] for item in report["findings"]})

    @unittest.skipUnless(shutil.which("ccache") and (shutil.which("cc") or shutil.which("gcc")),
                         "ccache 4.9 and a C compiler are required")
    def test_real_ccache_result_when_available(self):
        ccache = shutil.which("ccache")
        compiler = shutil.which("cc") or shutil.which("gcc")
        assert ccache is not None and compiler is not None
        version = subprocess.run([ccache, "--version"], text=True, capture_output=True, check=False)
        if "ccache version 4.9" not in version.stdout:
            self.skipTest("installed ccache is not 4.9")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            source = root / "sample.c"
            source.write_text("int sample(void) { return 7; }\n", encoding="utf-8")
            output = root / "sample.o"
            env = {key: value for key, value in os.environ.items() if not key.startswith("CCACHE_")}
            env.update(CCACHE_DIR=str(cache), CCACHE_CONFIGPATH=os.devnull)
            built = subprocess.run([ccache, compiler, "-c", str(source), "-o", str(output), "-MMD"],
                                   env=env, text=True, capture_output=True, check=False)
            self.assertEqual(built.returncode, 0, built.stderr)
            report = AUDIT.audit(cache, ccache=ccache)
            self.assertEqual(report["status"], "passed", report)
            self.assertIn("elf_relocatable", report["decoded_type_counts"])


if __name__ == "__main__":
    unittest.main()
