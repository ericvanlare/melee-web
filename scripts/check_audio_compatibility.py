#!/usr/bin/env python3
"""Compare the replacement SRC with the historical adapter in a temporary build.

Requires local Git history, a C compiler, and the bootstrapped Node runtime.
The GPL reference retains its notices and is never copied into a player target.
This is regression evidence, not an independent original-hardware oracle.
"""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile

from check_gameplay import node_runtime

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_REVISION = "e519c2fa4416c1ff8dba4e5467e65a2927b5ad29"
COEFFICIENT_SHA256 = "d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179"


def main():
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        raise SystemExit("A C compiler is required")
    with tempfile.TemporaryDirectory(prefix="melee-audio-compatibility-") as temporary:
        directory = Path(temporary)
        for name in ("gameplay_audio_resample.c", "gameplay_audio_resample.h"):
            source = subprocess.check_output(
                ["git", "show", f"{REFERENCE_REVISION}:src/{name}"], cwd=ROOT
            )
            (directory / name).write_bytes(source)
        coefficients = subprocess.check_output([
            str(node_runtime()), "--input-type=module", "-e",
            "import {createAudioFilterTable} from './web/dsp-coefficients.mjs';"
            "process.stdout.write(createAudioFilterTable());",
        ], cwd=ROOT)
        if hashlib.sha256(coefficients).hexdigest() != COEFFICIENT_SHA256:
            raise SystemExit("Generated coefficient table differs from the compatibility baseline")
        coefficient_path = directory / "dsp_coef.bin"
        coefficient_path.write_bytes(coefficients)
        flags = [compiler, "-std=c11", "-O2", "-fsanitize=address,undefined"]
        reference_object = directory / "reference.o"
        subprocess.run([
            *flags, "-Dmelee_web_audio_resample=old_resample", "-c",
            str(directory / "gameplay_audio_resample.c"), "-o", str(reference_object),
        ], check=True)
        executable = directory / "compare"
        subprocess.run([
            *flags, "-I", str(ROOT / "src"),
            str(ROOT / "tests/audio_resample_compatibility.c"),
            str(ROOT / "src/gameplay_audio_resample.c"), str(reference_object),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable), str(coefficient_path)], check=True)
    print(f"Reference revision: {REFERENCE_REVISION}")
    print(f"Coefficient SHA-256: {COEFFICIENT_SHA256}")


if __name__ == "__main__":
    main()
