"""Focused source HSD prefix plus deferred type-3 DevCom boundary checks."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))

from check_gameplay import node_runtime
from original_startup_fixture import fixture_arguments


def synthetic_context() -> dict:
    """Small source-shaped context; it is deliberately not an owned receipt."""
    return {
        "schema": "melee-web-original-boot-context",
        "version": 1,
        "derivation": "owned_disc_apploader_and_dol_no_capture_inputs",
        "root": {
            "arena_hi": 0x817F8000,
            "heap_max_num": 4,
            "arena_lo": 0x8065CC00,
            "audio_heap_size": 0x80000,
            "aram_base": 0x4000,
            "aram_size": 0x1000000,
        },
        "boot": {
            "memory_size": 0x01800000,
            "arena_hi": 0x817F8000,
            "aram_size": 0x1000000,
        },
        "stages": {
            "os_arena_lo": 0x804EEC00,
            "crash_allocation_size": 0x2000,
            "crash_allocation_alignment": 4,
            "crash_allocation_base": 0x804EEC00,
            "after_crash": 0x804F0C00,
            "framebuffer_count": 2,
            "framebuffer_size": 0x96000,
            "framebuffer_width": 640,
            "framebuffer_height": 480,
            "framebuffer_begin": 0x804F0C00,
            "after_xfb": 0x8061CC00,
            "fifo_size": 0x40000,
            "os_init_alloc_lo": 0x8065CC00,
        },
    }


class SourceDevComStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        candidates = []
        configured = os.environ.get("MELEE_WEB_SOURCE_DEVCOM_TRACE")
        if configured:
            candidates.append(Path(configured).expanduser())
        candidates.extend((ROOT / "build" / name / "source_devcom_trace.js"
                           for name in ("fixture", "browser", "browser-release")))
        cls.target = next((path for path in candidates if path.is_file()), None)
        if cls.target is None:
            raise unittest.SkipTest(
                "source_devcom_trace.js unavailable; build the bounded fixture target first"
            )
        try:
            cls.node = node_runtime(ROOT)
        except (OSError, ValueError, SyntaxError) as error:
            raise unittest.SkipTest(f"pinned local Node unavailable: {error}") from error

    def run_trace(self, arguments, *, check=True):
        result = subprocess.run(
            [str(self.node), str(self.target), *arguments],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=60,
        )
        if check and result.returncode != 0:
            self.fail(
                f"source DevCom fixture failed ({result.returncode}):\n"
                f"stdout={result.stdout}\nstderr={result.stderr}"
            )
        return result

    @staticmethod
    def records(output: str) -> list[dict]:
        records = []
        for line in output.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise AssertionError(f"fixture emitted non-JSON output: {line!r}") from error
            if not isinstance(record, dict):
                raise AssertionError("fixture JSON record is not an object")
            records.append(record)
        return records

    def test_source_prefix_and_deferred_devcom_recycling(self):
        # These arguments exercise source relations without claiming DOL/disc
        # provenance.  run_owned_fixture is reserved for an owned-input run.
        result = self.run_trace(fixture_arguments(synthetic_context()))
        self.assertEqual(result.stderr, "")
        records = self.records(result.stdout)
        self.assertEqual(len(records), 2)

        hsd, devcom = records
        self.assertEqual(hsd["schema"], "melee-web-original-startup-fixture")
        self.assertEqual(hsd["status"], "boundary_reached")
        self.assertEqual(hsd["boundary"], "hsd_os_id_obj")
        self.assertEqual(hsd["source_aram"],
                         {"base": 0x4000, "size": 0x1000000, "initialized": False})
        self.assertIn("ARInit", hsd["omitted_services"])

        self.assertEqual(devcom["schema"], "melee-web-source-devcom")
        self.assertEqual(devcom["version"], 1)
        self.assertEqual(devcom["status"], "boundary_reached")
        request = devcom["request"]
        self.assertEqual(
            {key: request[key] for key in ("first", "second", "lane", "type", "size")},
            {"first": 7, "second": 11, "lane": 3, "type": 3, "size": 1280},
        )
        self.assertEqual(request["first"] & 3, request["lane"])
        self.assertEqual(request["second"] & 3, request["lane"])
        self.assertEqual(request["second"] - request["first"], 4)
        self.assertEqual(request["callback_after_first"], 0)
        self.assertEqual(request["callback_after_second"], 1)

        relay = devcom["relay"]
        for field in ("first_zeroed", "aram_bytes_zeroed", "deferred", "recycled",
                      "masked_delivery_rejected", "duplicate_completion_rejected"):
            self.assertIs(relay[field], True, field)
        self.assertEqual(relay["size"], 0x4000)
        self.assertEqual(relay["buffers"], 2)

        heap = devcom["audio_heap"]
        self.assertEqual(heap["node_count"], 16)
        self.assertEqual(heap["node_size"], 0x24)
        self.assertEqual(heap["node_size"] % 4, 0)
        self.assertGreater(heap["free_before"], heap["free_after_node"])
        self.assertGreaterEqual(
            heap["free_before"] - heap["free_after_node"],
            heap["node_size"] * heap["node_count"],
        )
        self.assertEqual(heap["handle"], hsd["heap"]["audio_handle"])
        self.assertGreaterEqual(heap["block_payload_source"], hsd["heap"]["audio_begin"])
        self.assertLessEqual(heap["block_payload_source"] + heap["node_size"] * heap["node_count"],
                             hsd["heap"]["audio_end"])
        self.assertEqual(relay["cache_publish_count"], 2)
        self.assertGreaterEqual(heap["node_source"], 0x80000000)
        self.assertEqual(heap["node_source"] & 0x1F, 0)
        self.assertEqual(heap["block_payload_source"], heap["node_source"])

        aram = devcom["aram"]
        self.assertEqual(aram["base"], 0x4000)
        self.assertEqual(aram["size"], 0x1000000)
        self.assertEqual(aram["provider"], "checked_source_arq")

    def test_fresh_process_rejects_malformed_source_geometry(self):
        arguments = fixture_arguments(synthetic_context())
        bad = list(arguments)
        index = bad.index("--aram_base") + 1
        bad[index] = str(0x4001)
        result = self.run_trace(bad, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("melee-web-source-devcom", result.stdout)


if __name__ == "__main__":
    unittest.main()
