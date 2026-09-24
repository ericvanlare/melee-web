"""Focused tests for ccache object seeding."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import ci_seed


class CiSeedTests(unittest.TestCase):
    def test_shards_form_a_disjoint_union(self):
        outputs = [f"CMakeFiles/target.dir/source-{index}.cpp.o" for index in range(32)]
        shards = [set(ci_seed.select_shard(outputs, shard)) for shard in range(ci_seed.SHARD_COUNT)]
        self.assertEqual(set().union(*shards), set(outputs))
        for left, right in zip(shards, shards[1:]):
            self.assertEqual(left & right, set())

    def test_compdb_filters_unique_relative_objects_only(self):
        rows = [
            {"output": "obj/a.o"},
            {"output": "obj/a.o"},
            {"output": "obj/a.d"},
            {"output": "obj/b.o"},
            {"output": "/outside/c.o"},
            {"output": "../outside/d.o"},
            {"file": "missing-output"},
        ]
        self.assertEqual(ci_seed.compdb_objects(rows), ["obj/a.o", "obj/b.o"])

    def test_tiny_ninja_graph_builds_selected_objects_with_dependencies(self):
        pinned_ninja = ROOT / ".venv/bin/ninja"
        ninja = str(pinned_ninja) if pinned_ninja.is_file() else shutil.which("ninja")
        if ninja is None or not Path(ninja).is_file():
            self.skipTest("ninja is unavailable")
        with tempfile.TemporaryDirectory(prefix="melee-ci-seed-ninja-") as temporary:
            root = Path(temporary)
            build = root / "build/browser"
            build.mkdir(parents=True)
            (build / "main.c").write_text('#include "generated.h"\nint main_value(void) { return GENERATED_VALUE; }\n')
            (build / "other.c").write_text('int other_value(void) { return 2; }\n')
            (build / "build.ninja").write_text(
                "rule gen\n"
                "  command = python3 -c 'open(\"generated.h\", \"w\").write(\"#define GENERATED_VALUE 1\\n\")'\n"
                "  generator = 1\n"
                "rule cc\n"
                f"  command = cc -I{build} -c $in -o $out\n"
                "build generated.h: gen\n"
                "build main.o: cc main.c | generated.h\n"
                "build other.o: cc other.c\n"
            )
            with patch.object(ci_seed, "GROUPS", {"tiny": ("main.o", "other.o")}):
                rows = ci_seed._compdb(root, {}, Path(ninja))
            objects = ci_seed.compdb_objects(rows)
            self.assertEqual(objects, ["main.o", "other.o"])
            selected = ci_seed.select_shard(objects, ci_seed.shard_for("main.o"))
            self.assertIn("main.o", selected)
            result = subprocess.run(
                [ninja, "-C", str(build), "-j", "2", *selected],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((build / "generated.h").is_file())
            self.assertTrue((build / "main.o").is_file())

    def test_uncacheable_pch_consumers_remain_in_full_inventory(self):
        rows = [
            {"output": "ordinary.o", "command": "emcc -c ordinary.c -o ordinary.o"},
            {"output": "pch.o", "command": "emcc -Xclang -include-pch -Xclang header.pch -c pch.c -o pch.o"},
        ]
        self.assertEqual(ci_seed.compdb_objects(rows), ["ordinary.o", "pch.o"])
        self.assertEqual(ci_seed.compdb_objects(rows, cacheable_only=True), ["ordinary.o"])

    def test_build_failure_writes_failed_report_and_propagates(self):
        temporary = tempfile.TemporaryDirectory(prefix="melee ci seed ")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        calls = []
        output = next(
            f"object-{index}.o" for index in range(ci_seed.SHARD_COUNT * 2)
            if ci_seed.shard_for(f"object-{index}.o") == 0
        )

        def run(command, **kwargs):
            calls.append(command)
            if "-j" in command:
                raise subprocess.CalledProcessError(1, command)
            stdout = json.dumps([{ "output": output }]) if "compdb-targets" in command else ""
            return subprocess.CompletedProcess(command, 0, stdout=stdout, stderr="")

        with patch.object(ci_seed, "ROOT", root), \
                patch.object(ci_seed, "_ninja_path", return_value=Path("ninja")), \
                patch.object(ci_seed.platform, "platform", return_value="test-platform"), \
                patch.object(ci_seed.subprocess, "check_output", return_value="commit\n"), \
                patch.object(ci_seed.subprocess, "run", side_effect=run):
            with self.assertRaises(subprocess.CalledProcessError):
                ci_seed.run_seed(0, 2)
        report = json.loads((root / "work/ci-seed/shard-0.json").read_text(encoding="utf-8"))
        self.assertEqual(report["status"], "failed")
        self.assertEqual(len(calls), 3)
        self.assertIn("configure", [phase["name"] for phase in report["phases"]])
        self.assertIn("compdb", [phase["name"] for phase in report["phases"]])
        self.assertIn("build", [phase["name"] for phase in report["phases"]])


if __name__ == "__main__":
    unittest.main()
