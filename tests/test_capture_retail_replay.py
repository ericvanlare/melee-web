"""Focused checks for the bounded retail capture runner.

These tests exercise preparation and generated control files only.  They do
not launch Dolphin or GDB and therefore cannot claim a real retail capture.
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "capture_retail_replay", ROOT / "scripts/capture_retail_replay.py")
CAPTURE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(CAPTURE)


def _setup_files(root: Path) -> dict[str, Path | dict]:
    template = root / "template-user"
    config = template / "Config"
    config.mkdir(parents=True)
    (config / "Dolphin.ini").write_text(
        "[General]\nGDBSocket = /tmp/stale-gdb.sock\n\n[Core]\nCPUThread = True\n",
        encoding="utf-8")
    (config / "GCPadNew.ini").write_text(
        "[Pad1]\nDevice = Pipe/0/pad1\n[Pad2]\nDevice = Pipe/0/pad2\n",
        encoding="utf-8")
    stale_pipes = template / "Pipes"
    stale_pipes.mkdir()
    # A template may retain FIFOs from a prior run; preparation must skip them
    # and create fresh owned FIFOs instead of copying shared mutable endpoints.
    stale_pipe = stale_pipes / "pad1"
    stale_pipe.touch()

    checkpoint = root / "checkpoint-gc"
    (checkpoint / "nested").mkdir(parents=True)
    (checkpoint / "SRAM.raw").write_bytes(b"checkpoint bytes")
    (checkpoint / "nested" / "state.bin").write_bytes(b"nested state")

    dol = root / "GALE01r2.dol"
    dol.write_bytes(b"synthetic DOL used only for preparation tests")
    dolphin = root / "Dolphin"
    dolphin.write_bytes(b"synthetic Dolphin executable used only for tests")

    expected = dict(CAPTURE.EXPECTED_PROVENANCE)
    expected["dol_sha1"] = hashlib.sha1(dol.read_bytes()).hexdigest()
    expected["dolphin_binary_sha256"] = hashlib.sha256(dolphin.read_bytes()).hexdigest()
    expected["background_input"] = True
    provenance = root / "provenance.json"
    provenance.write_text(json.dumps(expected) + "\n", encoding="utf-8")
    return {
        "template": template,
        "checkpoint": checkpoint,
        "dol": dol,
        "dolphin": dolphin,
        "provenance": provenance,
        "expected": expected,
    }


class CaptureRunnerTests(unittest.TestCase):
    def test_launched_disc_must_contain_the_pinned_dol(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dol = root / "main.dol"
            dol.write_bytes(bytes(range(256)) * 2)
            disc = root / "game.iso"
            image = bytearray(0x1000)
            image[:8] = b"GALE01\x00\x02"
            image[0x1c:0x20] = bytes.fromhex("c2339f3d")
            image[0x420:0x424] = (0x800).to_bytes(4, "big")
            image[0x800:0xa00] = dol.read_bytes()
            disc.write_bytes(image)
            self.assertEqual(CAPTURE.verify_disc_dol(disc, dol),
                             hashlib.sha1(dol.read_bytes()).hexdigest())
            image[0x900] ^= 1
            disc.write_bytes(image)
            with self.assertRaisesRegex(CAPTURE.CaptureRunnerError, "does not match"):
                CAPTURE.verify_disc_dol(disc, dol)
            image[7] = 1
            disc.write_bytes(image)
            with self.assertRaisesRegex(CAPTURE.CaptureRunnerError, "revision 2"):
                CAPTURE.verify_disc_dol(disc, dol)

    def test_dolphin_command_pins_read_only_runtime_configuration(self):
        command = CAPTURE.dolphin_command(
            Path("/Dolphin"), Path("/owned/user"), Path("/owned/snapshot.sav"),
            Path("/readonly/game.iso"))
        self.assertEqual(command[0:8], [
            "/Dolphin", "-u", "/owned/user", "-d", "-s",
            "/owned/snapshot.sav", "-e", "/readonly/game.iso",
        ])
        for setting in (
            "Dolphin.Input.BackgroundInput=True",
            "Dolphin.Core.CPUCore=0",
            "Dolphin.Core.CPUThread=False",
            "Dolphin.Core.EnableCheats=False",
            "Dolphin.Core.EnableCustomRTC=True",
            f"Dolphin.Core.CustomRTCValue={CAPTURE.RTC}",
        ):
            self.assertIn(setting, command)

    def test_generated_gdb_control_counts_source_ticks_and_bounds_traps(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            helper = root / "gdb-control.py"
            pipes = root / "owned" / "Pipes"
            CAPTURE.write_control_helper(helper, pipes)
            text = helper.read_text(encoding="utf-8")
            self.assertIn(repr(str(pipes)), text)
            self.assertIn("SCENE_FRAME = 0x80479D58", text)
            self.assertIn("MAX_TRAPS_FACTOR = 8", text)
            self.assertIn("MAX_TRAPS_SLOP = 64", text)
            self.assertLess(text.index("before = source_frame()"),
                            text.index('gdb.execute("continue"'))
            self.assertLess(text.index('gdb.execute("continue"'),
                            text.index("after = source_frame()"))
            self.assertIn("source scene_frame jumped", text)
            self.assertIn("source scene_frame did not advance", text)

    def test_generated_gdb_script_has_pinned_procedure_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "gdb-commands.txt"
            CAPTURE.write_gdb_script(
                path, root / "gdb.sock", root / "gdb-control.py",
                root / "reference_replay_capture.py", root / "capture.jsonl", 240)
            lines = path.read_text(encoding="utf-8").splitlines()
            expected = [
                "set architecture powerpc:common",
                "set endian big",
                "continue",
                "source %s" % (root / "gdb-control.py"),
                "retail-step 12 1 SET MAIN 0.5 0.5",
                "source %s" % (root / "reference_replay_capture.py"),
                "retail-replay-arm \"%s\" 240" % (root / "capture.jsonl"),
                "retail-step 8 1 PRESS A",
                "retail-step 8 1 RELEASE A",
                "disable 1",
                "continue",
                "quit",
            ]
            positions = []
            cursor = -1
            for item in expected:
                cursor = lines.index(item, cursor + 1)
                positions.append(cursor)
            self.assertEqual(positions, sorted(positions))
            self.assertIn('target remote %s' % (root / "gdb.sock"), lines)
            self.assertIn("hbreak *0x80390eb4", lines)

    def test_prepare_run_copies_bytes_and_creates_fresh_owned_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = _setup_files(root)
            output = root / "captures" / "first.jsonl"
            with mock.patch.object(CAPTURE, "EXPECTED_PROVENANCE", files["expected"]):
                paths = CAPTURE.prepare_run(
                    files["template"], files["checkpoint"], files["provenance"],
                    files["dol"], files["dolphin"], output)
            user = paths["user"]
            self.assertTrue(stat.S_ISFIFO((user / "Pipes/pad1").stat().st_mode))
            self.assertTrue(stat.S_ISFIFO((user / "Pipes/pad2").stat().st_mode))
            self.assertFalse((user / "Pipes/pad1").samefile(files["template"] / "Pipes/pad1"))
            self.assertEqual((user / "GC/SRAM.raw").read_bytes(), b"checkpoint bytes")
            self.assertEqual((user / "GC/nested/state.bin").read_bytes(), b"nested state")
            dolphin_ini = (user / "Config/Dolphin.ini").read_text(encoding="utf-8")
            self.assertIn(f"GDBSocket = {paths['socket']}", dolphin_ini)
            self.assertNotIn("/tmp/stale-gdb.sock", dolphin_ini)
            boundary = paths["collector_boundary"]
            expected_hash = hashlib.sha256(
                paths["collector"].read_bytes() + b"\0" + boundary.read_bytes() + b"\0" +
                paths["collector"].with_name('retail_input_plan.py').read_bytes()).hexdigest()
            self.assertEqual(paths["collector_sha256"], expected_hash)

    def test_provenance_configuration_types_are_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = _setup_files(root)
            wrong = dict(files["expected"])
            wrong["cpu_thread"] = 0
            wrong_path = root / "wrong-provenance.json"
            wrong_path.write_text(json.dumps(wrong), encoding="utf-8")
            with mock.patch.object(CAPTURE, "EXPECTED_PROVENANCE", files["expected"]):
                with self.assertRaisesRegex(CAPTURE.CaptureRunnerError,
                                             "pinned provenance mismatch"):
                    CAPTURE.load_provenance(wrong_path, files["dol"], files["dolphin"])

    def test_existing_output_fails_before_any_child_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = _setup_files(root)
            output = root / "capture.jsonl"
            output.write_text("already present\n", encoding="utf-8")
            with mock.patch.object(CAPTURE.subprocess, "Popen") as popen:
                with self.assertRaisesRegex(CAPTURE.CaptureRunnerError, "already exists"):
                    CAPTURE.capture_replay(
                        dolphin=files["dolphin"], disc=files["dol"], dol=files["dol"],
                        template_user=files["template"], snapshot=files["dol"],
                        checkpoint_gc=files["checkpoint"], provenance=files["provenance"],
                        output=output)
                popen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
