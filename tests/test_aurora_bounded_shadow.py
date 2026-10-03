#!/usr/bin/env python3
"""Compile/run the bounded shadow C++ fixture against the patched Aurora classes.

The companion C++ file is a test template: this runner injects ArrayRef and
ByteBuffer verbatim from the patched internal.hpp, so the test cannot pass
against a second hand-written ByteBuffer implementation.
"""
from __future__ import annotations

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / ".deps" / "aurora" / "lib" / "internal.hpp"
FIXTURE = Path(__file__).with_name("aurora_bounded_shadow_test.cpp")
MARKER = "@AURORA_BYTEBUFFER_CLASSES@"


class AuroraBoundedShadowTests(unittest.TestCase):
    def test_growth_ownership_padding_and_failure(self):
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler unavailable")
        self.assertTrue(HEADER.is_file(), "patched internal.hpp is missing")
        self.assertTrue(FIXTURE.is_file(), "bounded shadow C++ fixture is missing")

        header_text = HEADER.read_text()
        start = header_text.index("template <typename T>\nclass ArrayRef")
        end = header_text.index("\nclass ByteReader", start)
        actual_classes = header_text[start:end]
        fixture = FIXTURE.read_text()
        self.assertEqual(fixture.count(MARKER), 1, "fixture must have one class injection marker")
        generated = fixture.replace(MARKER, actual_classes)
        resources = (HEADER.parent / "gfx/resources.hpp").read_text()
        names = ("UniformBufferSize", "VertexBufferSize", "IndexBufferSize",
                 "StorageBufferSize", "TextureUploadSize")
        maxima = []
        for name in names:
            match = re.search(r"inline constexpr uint64_t " + name + r" = (\d+);", resources)
            self.assertIsNotNone(match, f"missing authoritative capacity: {name}")
            maxima.append(match.group(1))
        generated = generated.replace("@STAGING_MAXIMA@", ", ".join(maxima))

        with tempfile.TemporaryDirectory(prefix="aurora-bounded-shadow-") as directory:
            directory_path = Path(directory)
            source = directory_path / "aurora_bounded_shadow_test.cpp"
            binary = directory_path / "bounded_shadow_test"
            source.write_text(generated)
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c++20",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source),
                    "-o",
                    str(binary),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(run_result.returncode, 0, run_result.stdout + run_result.stderr)
            self.assertIn("bounded staging shadows: PASS", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
