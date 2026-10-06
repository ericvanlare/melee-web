#!/usr/bin/env python3
"""Run the checked-in source-data-only Pokémon Stadium C0 trace twice.

The disc image is opened read-only. Extracted inputs are temporary files under
the ignored assets-local directory; receipts and failure captures belong in the
explicit external run directory. This does not register or run a stage.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from bootstrap import read_lock
from check_gameplay import node_runtime
from extract_disc_file import DiscImage, extract_file


ROOT = Path(__file__).resolve().parents[1]
ASSETS_ROOT = ROOT / "assets-local"
MELEE_SOURCE = ROOT / ".deps/melee/src/melee/lb/lbaudio_ax.static.h"

# These values are the committed C0 input contract from the Stadium source
# notes. The runner checks both the current disc and the extracted bytes.
INPUTS = {
    "GrPs.usd": (419, 0x4A44BC28, 1_461_024,
                 "aa740cfbbeced294f058449caca8ac0380532521dde06ca4a6dcc03080cf6a6d"),
    "GrPs.dat": (418, 0x4CB88000, 1_468_544,
                 "0b55e7ba6234b882a35edf80d2d6c7f84d9fc887cbd4fe9c85b38ed7c3ea4e18"),
    "GrPs1.dat": (420, 0x4CCF0000, 327_617,
                  "ea94e509953a024cb72d6c917b9ad3bf9ebc7c6a40d5d091daa613adafdb3ad7"),
    "GrPs2.dat": (421, 0x4CD40000, 213_114,
                  "0232063e3393ee08cce329207e06bf237059b9585cfba96d2e4ab7274ca26375"),
    "GrPs3.dat": (422, 0x4CD78000, 297_517,
                  "aa167c82d65d93a7ac85aff513e3a0d02963a12bb50d1d8f76fc73b2e09205a8"),
    "GrPs4.dat": (423, 0x4CDC8000, 282_884,
                  "861a04b08dcaa98f098bdb5c5237e6710e27cbafbb74814005407789cb3bfce9"),
    "audio/pstadium.hps": (113, 0x43DB20C0, 2_303_488,
                           "66d873dd38973f3cf7e328365f32120648ca6f779ccd849226ff4c19e3c058f4"),
    "audio/pokesta.hps": (112, 0x43A47AE0, 3_581_408,
                          "00ff265d45dd8a72ecd1d80f192d3d32457e0990a5c93e699a42aac9e1ec5439"),
    "audio/us/pstadium.ssm": (186, 0x49987D00, 101_792,
                              "3b95045913d27469d87720f94b91e1bce4272e3ebabc743d74d0e8e863d34b25"),
}
TRANSFORMATIONS = ("GrPs1.dat", "GrPs2.dat", "GrPs3.dat", "GrPs4.dat")


class C0Error(RuntimeError):
    """A checked input, source identity, or structural trace assertion failed."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_array(source: str, name: str) -> list[str]:
    match = re.search(
        rf"static const char\*\s+{re.escape(name)}\[\]\s*=\s*\{{(.*?)\}};",
        source,
        re.S,
    )
    if not match:
        raise C0Error(f"pinned audio source array {name} was not found")
    return re.findall(r'"([^"\\]+)"', match.group(1))


def audio_source_identity(source_path: Path) -> dict[str, object]:
    source = source_path.read_text(encoding="utf-8")
    hps = source_array(source, "hps_files")
    ssm = source_array(source, "ssm_files")
    table = re.search(
        r"static u8\s+s32_arr_803BB6B0\[0x6F\]\[3\]\s*=\s*\{(.*?)\};",
        source,
        re.S,
    )
    if not table:
        raise C0Error("pinned source stage-to-SSM table was not found")
    rows = [tuple(int(value, 0) for value in row) for row in re.findall(
        r"\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\}",
        table.group(1),
    )]
    if len(hps) <= 64 or hps[63:65] != ["pokesta.hps", "pstadium.hps"]:
        raise C0Error("authored hps_files IDs 63-64 differ from Stadium notes")
    if len(ssm) <= 48 or ssm[48] != "pstadium.ssm":
        raise C0Error("authored ssm_files bank 48 differs from Stadium notes")
    if len(rows) <= 16 or rows[16] != (0x30, 1, 1):
        raise C0Error("Stadium GrKind row does not select authored SSM bank 48")
    return {
        "source": ".deps/melee/src/melee/lb/lbaudio_ax.static.h",
        "hps_ids": {"63": hps[63], "64": hps[64]},
        "ssm_bank_48": ssm[48],
        "grkind_16_ssm_selection": list(rows[16]),
    }


def input_inventory(disc_path: Path, extraction_root: Path) -> tuple[list[dict[str, object]], dict[str, object]]:
    with DiscImage(disc_path) as disc:
        fst = disc.indexed_files()
        records: list[dict[str, object]] = []
        for relative, (fst_index, expected_offset, expected_size, expected_digest) in INPUTS.items():
            indexed_entry = fst.get(relative)
            if indexed_entry is None:
                raise C0Error(f"required C0 source file is absent from this disc: {relative}")
            observed_fst_index, entry = indexed_entry
            if observed_fst_index != fst_index:
                raise C0Error(
                    f"{relative} observed FST entry {observed_fst_index}, expected {fst_index}"
                )
            if entry.offset != expected_offset or entry.size != expected_size:
                raise C0Error(
                    f"{relative} observed FST entry {observed_fst_index} is offset/size "
                    f"{entry.offset:#x}/{entry.size}, expected {expected_offset:#x}/{expected_size}"
                )
            output = extraction_root / relative
            selected = extract_file(
                disc_path, relative, output, assets_root=ASSETS_ROOT
            )
            data = output.read_bytes()
            digest = sha256(data)
            if selected.offset != expected_offset or selected.size != expected_size:
                raise C0Error(f"{relative} selected file-relative source range changed")
            if len(data) != expected_size or digest != expected_digest:
                raise C0Error(f"{relative} extracted size/SHA-256 differs from the pinned C0 input")
            records.append({
                "path": relative,
                "fst_entry": observed_fst_index,
                "disc_offset": entry.offset,
                "size": len(data),
                "sha256": digest,
            })

        tails: list[dict[str, object]] = []
        for relative in TRANSFORMATIONS:
            _observed_fst_index, entry = fst[relative]
            padded_size = (entry.size + 31) & ~31
            tail = disc.read(entry.offset + entry.size, padded_size - entry.size)
            if len(tail) != padded_size - entry.size or any(tail):
                raise C0Error(f"{relative} ROUND_UP_32 padding is not fully zero")
            tails.append({
                "path": relative,
                "disc_offset": entry.offset,
                "file_size": entry.size,
                "rounded_size": padded_size,
                "tail_bytes": len(tail),
                "tail_hex": tail.hex(),
                "tail_sha256": sha256(tail),
                "all_zero": True,
            })
    return records, {"transform_round_up_32_zero_tails": tails}


def run_capture(node: Path, target: Path, assets: Path, label: str,
                receipt_root: Path) -> bytes:
    command = [str(node), str(target), str(assets)]
    try:
        result = subprocess.run(
            command,
            cwd=ROOT,
            capture_output=True,
            timeout=300,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        (receipt_root / f"{label}.stdout.txt").write_bytes(stdout)
        (receipt_root / f"{label}.stderr.txt").write_bytes(stderr)
        raise C0Error(f"{label} structural trace exceeded its 300 second bound") from error
    (receipt_root / f"{label}.stdout.txt").write_bytes(result.stdout)
    (receipt_root / f"{label}.stderr.txt").write_bytes(result.stderr)
    if result.returncode != 0:
        raise C0Error(
            f"{label} structural trace exited {result.returncode}; retained stdout/stderr"
        )
    try:
        parsed = json.loads(result.stdout)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise C0Error(f"{label} did not emit exactly one JSON structural record") from error
    if not isinstance(parsed, dict) or not parsed.get("archives"):
        raise C0Error(f"{label} structural output omitted the archive inventory")
    return result.stdout


def repository_identity() -> dict[str, str]:
    """Bind evidence to a clean committed source tree."""
    head = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, timeout=10
    ).strip()
    tree = subprocess.check_output(
        ["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True, timeout=10
    ).strip()
    status = subprocess.check_output(
        ["git", "status", "--porcelain=v1", "--untracked-files=all"],
        cwd=ROOT, text=True, timeout=10,
    )
    if status:
        raise C0Error("C0 trace requires a clean committed checkout; git status is not empty")
    return {"head": head, "tree": tree, "worktree": "clean"}


def target_identity(target: Path, wasm: Path) -> dict[str, str]:
    return {
        "javascript_sha256": sha256(target.read_bytes()),
        "wasm_sha256": sha256(wasm.read_bytes()),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True,
                        help="authorized local GALE01 1.02 CISO/ISO/GCM image")
    parser.add_argument("--attempt-dir", type=Path, required=True,
                        help="new, exclusive per-attempt directory in the external task run root")
    parser.add_argument("--target", type=Path,
                        default=ROOT / "build/browser/pokemon_stadium_c0_trace.js",
                        help="built C0 Node/Wasm trace target")
    args = parser.parse_args(argv)
    attempt = args.attempt_dir.expanduser().resolve()
    if attempt == ROOT or attempt.is_relative_to(ROOT):
        parser.error("--attempt-dir must be outside the Git checkout")
    try:
        attempt.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        parser.error("--attempt-dir already exists; each attempt needs a fresh directory")
    except OSError as error:
        parser.error(f"cannot create --attempt-dir exclusively: {error}")
    receipt = attempt / "receipt.json"
    extraction: Path | None = None
    success = False
    report: dict[str, object] = {
        "schema": "melee-web-pokemon-stadium-c0-v1",
        "scope": "source-data structural parser evidence only; no runtime or stage admission",
        "attempt_directory": str(attempt),
        "result": "failed",
    }
    try:
        pre_source = repository_identity()
        report["source_identity_before"] = pre_source
        target = args.target.expanduser().resolve(strict=True)
        build_root = (ROOT / "build").resolve()
        if not target.is_relative_to(build_root) or target.suffix != ".js":
            raise C0Error("--target must be a .js target inside this checkout's build directory")
        if (ROOT / "build").is_symlink() or target.is_symlink():
            raise C0Error("build output must remain in this checkout, without a symlink")
        wasm = target.with_suffix(".wasm")
        if not wasm.is_file():
            raise C0Error("the C0 target's companion Wasm file is missing")
        if not MELEE_SOURCE.is_file():
            raise C0Error("pinned Melee source is unavailable; bootstrap is not part of this run")
        pre_target = target_identity(target, wasm)
        report["target"] = {"name": target.name, **pre_target}

        ASSETS_ROOT.mkdir(parents=True, exist_ok=True)
        extraction = Path(tempfile.mkdtemp(prefix="stadium-c0-", dir=ASSETS_ROOT))
        node = node_runtime(ROOT)
        lock = read_lock(ROOT)
        report["build_identity"] = {
            "emscripten": lock["emscripten"],
            "node_version": subprocess.check_output(
                [str(node), "--version"], text=True, timeout=10
            ).strip(),
        }
        inputs, tails = input_inventory(args.disc.expanduser().resolve(strict=True), extraction)
        report["inputs"] = inputs
        report.update(tails)
        report["audio_source_identity"] = audio_source_identity(MELEE_SOURCE)
        first = run_capture(node, target, extraction, "c0-trace-run-1", attempt)
        second = run_capture(node, target, extraction, "c0-trace-run-2", attempt)
        first_digest, second_digest = sha256(first), sha256(second)
        (attempt / "c0-trace-run-1.json").write_bytes(first)
        (attempt / "c0-trace-run-2.json").write_bytes(second)
        report["trace_runs"] = [
            {"run": 1, "fresh_process": True, "stdout_sha256": first_digest},
            {"run": 2, "fresh_process": True, "stdout_sha256": second_digest},
        ]
        if first != second:
            raise C0Error("fresh C0 trace processes disagreed; both outputs are retained")
        report["repeated_trace_output_identical"] = True
        post_source = repository_identity()
        post_target = target_identity(target, wasm)
        report["source_identity_after"] = post_source
        report["target_identity_after"] = post_target
        if post_source != pre_source:
            raise C0Error("committed source identity or clean worktree changed during trace")
        if post_target != pre_target:
            raise C0Error("C0 target bytes changed during trace")
        report["result"] = "passed"
        success = True
    except (OSError, ValueError, C0Error, subprocess.SubprocessError) as error:
        report["result"] = "failed"
        report["failure"] = str(error)
        if extraction is not None:
            report["retained_extraction"] = "assets-local/" + extraction.name
    finally:
        if "source_identity_after" not in report:
            try:
                report["source_identity_after"] = repository_identity()
            except (OSError, subprocess.SubprocessError, C0Error) as error:
                report["source_identity_after_error"] = str(error)
        if target := locals().get("target"):
            wasm = target.with_suffix(".wasm")
            if wasm.is_file():
                try:
                    report["target_identity_after"] = target_identity(target, wasm)
                except OSError as error:
                    report["target_identity_after_error"] = str(error)
        receipt.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
        if success:
            assert extraction is not None
            shutil.rmtree(extraction)
    if not success:
        parser.exit(1, f"C0 trace: {report.get('failure')}\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
