#!/usr/bin/env python3
"""Package owned original-Dolphin CPU reference scenarios.

This is a preparation step. It creates a fresh scenario directory with an
owned donor snapshot, external-card copy, raw Pipe controller configuration,
v3 input plan, and an inline collector. The collector repeats original CSS/SSS
menu preparation inside each fresh Dolphin process before arming observation.
No final saved checkpoint is required and this script never launches Dolphin.
"""
from __future__ import annotations

import argparse
import configparser
import io
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DONOR_ROOT = ROOT / "work" / "cpu-reference"
DEFAULT_OUTPUT_ROOT = ROOT / "work" / "cpu-corpus" / "prepared"

sys.path.insert(0, str(ROOT / "tools"))
from cpu_reference_scenarios import (  # noqa: E402
    canonical,
    get_scenario,
    make_input_plan,
    scenario_ids,
    scenario_target,
    validate_catalog,
    validate_input_plan,
    validate_scenario,
)
from retail_cpu_menu_prepare import (  # noqa: E402
    render_source_driver,
    validate_driver,
)


COLLECTOR_FILES = (
    "reference_replay_boundary.py",
    "retail_input_plan.py",
    "retail_input_bootstrap.py",
    "retail_cpu_observation.py",
)


@dataclass(frozen=True)
class OwnedInputs:
    """Paths to owned donor inputs.

    Defaults are repository-relative. Host-specific Dolphin, disc, and DOL
    paths are accepted for the generated capture command but are represented
    there by environment variables so personal paths never enter a bundle's
    reusable recipe.
    """

    donor_root: Path
    raw_template_user: Path
    snapshot: Path
    checkpoint_gc: Path
    provenance: Path
    dolphin: Path | None = None
    disc: Path | None = None
    dol: Path | None = None


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def hash_tree(root: Path) -> dict[str, str]:
    return {
        str(path.relative_to(root)): sha256(path)
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def write_json(path: Path, value: Any) -> None:
    path.write_bytes(canonical(value))


def repo_reference(path: Path) -> str:
    """Return a stable repository-relative reference without exposing paths."""
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return "external-owned-path"


def _regular_file(path: Path, label: str) -> Path:
    if path.is_symlink() or not path.is_file():
        raise RuntimeError(f"{label} is not a regular file: {path}")
    return path


def _regular_directory(path: Path, label: str) -> Path:
    if path.is_symlink() or not path.is_dir():
        raise RuntimeError(f"{label} is not a regular directory: {path}")
    return path


def add_pad_sections(config: Path, player_count: int) -> str:
    """Add missing active Pipe ports without duplicating existing sections."""
    original = config.read_text(encoding="utf-8")
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str
    parser.read_string(original)
    if player_count <= 2:
        return original
    if not parser.has_section("GCPad2"):
        raise RuntimeError("Raw controller config has no GCPad2 template")
    for port in range(3, player_count + 1):
        name = f"GCPad{port}"
        if not parser.has_section(name):
            parser[name] = dict(parser["GCPad2"])
            parser[name]["Device"] = f"Pipe/0/pad{port}"
    output = io.StringIO()
    parser.write(output)
    updated = output.getvalue()
    config.write_text(updated, encoding="utf-8")
    return updated


def validate_raw_controller_config(config: Path, active_player_count: int) -> None:
    # Preparation and launch must enforce the same raw polling conversion.
    sys.path.insert(0, str(ROOT / "scripts"))
    from capture_retail_replay import require_raw_pipe_config
    require_raw_pipe_config(config, tuple(range(1, active_player_count + 1)))


def copy_raw_template(source: Path, destination: Path, player_count: int) -> Path:
    """Copy regular donor files and create fresh FIFO endpoints later."""
    source = _regular_directory(source, "raw Dolphin user template")
    destination.mkdir(parents=True)
    for current, directories, files in os.walk(source, followlinks=False):
        current_path = Path(current)
        relative = current_path.relative_to(source)
        if relative.parts and relative.parts[0] == "Pipes":
            directories[:] = []
            continue
        target_dir = destination / relative
        target_dir.mkdir(parents=True, exist_ok=True)
        for name in files:
            source_file = current_path / name
            if source_file.is_symlink() or not source_file.is_file():
                raise RuntimeError(f"raw donor contains a non-regular file: {source_file}")
            shutil.copy2(source_file, target_dir / name)
    (destination / "Pipes").mkdir(parents=True, exist_ok=True)
    config = _regular_file(destination / "Config" / "GCPadNew.ini", "copied GCPadNew.ini")
    add_pad_sections(config, player_count)
    validate_raw_controller_config(config, player_count)
    # GCPad mappings alone do not connect a controller. The pinned Dolphin
    # SIDevices enum uses 6 for a standard GC controller and 0 for none.
    dolphin_ini = destination / "Config" / "Dolphin.ini"
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str
    parser.read(dolphin_ini)
    if not parser.has_section("Core"):
        parser.add_section("Core")
    for port in range(4):
        parser["Core"][f"SIDevice{port}"] = "6" if port < player_count else "0"
    with dolphin_ini.open("w") as stream:
        parser.write(stream)
    return config


def inline_collector(target: dict[str, Any], driver: str | None = None) -> str:
    """Build a collector with the source-menu driver embedded in its bytes."""
    validate_driver()
    driver = render_source_driver() if driver is None else driver
    target_text = json.dumps(target, sort_keys=True, separators=(",", ":"))
    base = (ROOT / "tools" / "reference_replay_capture.py").read_text(encoding="utf-8")
    prefix = f'''# Generated inline CPU reference collector.
# Original CSS/SSS preparation runs in this fresh GDB process before Arm().
import os as _cpu_os
from pathlib import Path as _cpu_Path

_cpu_evidence = _cpu_Path(_cpu_os.environ["MELEE_REPLAY_REFERENCE_WORK"]).resolve()
_cpu_target_path = _cpu_evidence / "cpu-target.json"
_cpu_target_path.write_text({target_text!r} + "\\n", encoding="utf-8")
_cpu_os.environ["MELEE_CHECKPOINT_TARGET_JSON"] = str(_cpu_target_path)
_cpu_menu_driver = {driver!r}
exec(compile(_cpu_menu_driver, "retail_cpu_menu_prepare.py", "exec"),
     {{"__name__": "cpu_menu_prepare"}})
'''
    return prefix + base


def copy_collector_bundle(bundle: Path, target: dict[str, Any]) -> dict[str, str]:
    collector_dir = bundle / "collector"
    collector_dir.mkdir()
    (collector_dir / "reference_replay_capture.py").write_text(
        inline_collector(target), encoding="utf-8")
    for name in COLLECTOR_FILES:
        source = _regular_file(ROOT / "tools" / name, f"collector helper {name}")
        shutil.copy2(source, collector_dir / name)
    return {
        name: sha256(collector_dir / name)
        for name in ("reference_replay_capture.py", *COLLECTOR_FILES)
    }


def preparation_manifest(scenario: dict[str, Any], target: dict[str, Any],
                         plan: dict[str, Any], bundle: Path,
                         inputs: OwnedInputs, config: Path,
                         collector_hashes: dict[str, str]) -> dict[str, Any]:
    donor_provenance = json.loads(inputs.provenance.read_text(encoding="utf-8"))
    owned_user = bundle / "template-user"
    owned_gc = bundle / "checkpoint-gc"
    return {
        "schema": "melee-web-cpu-reference-corpus-preparation",
        "version": 2,
        "status": "prepared_inline_menu_capture_pending",
        "claim": "owned inputs and inline source-menu collector emitted; Dolphin was not launched",
        "scenario_id": scenario["scenario_id"],
        "scenario_sha256": sha256_bytes(canonical(scenario)),
        "target_sha256": sha256_bytes(canonical(target)),
        "input_plan_sha256": sha256_bytes(canonical(plan)),
        "collector_sources_sha256": collector_hashes,
        "rules": scenario["rules"],
        "players": scenario["players"],
        "source_team_values": [p["team"] for p in scenario["players"]],
        "stage": {"name": scenario["stage"], "kind": scenario["stage_kind"]},
        "source_menu_route": scenario["source_menu_route"],
        "availability_policy": scenario["availability_policy"],
        "source_evidence": scenario["source_evidence"],
        "assets": {
            "donor_root": repo_reference(inputs.donor_root),
            "raw_template_user": repo_reference(inputs.raw_template_user),
            "snapshot": "owned-donor.sav",
            "checkpoint_gc": "checkpoint-gc",
            "provenance": "provenance.json",
            "base_snapshot_sha256": sha256(inputs.snapshot),
            "owned_snapshot_sha256": sha256(bundle / "owned-donor.sav"),
            "owned_external_save_hashes": hash_tree(owned_gc),
            "owned_gcpadnew_sha256": sha256(config),
            "base_provenance_sha256": sha256(inputs.provenance),
            "dol_sha1": donor_provenance.get("dol_sha1"),
            "dolphin_binary_sha256": donor_provenance.get("dolphin_binary_sha256"),
        },
        "controller": {
            "backend": "Pipe",
            "controlled_ports": [1],
            "source_input_policy": "dolphin-pipe-raw-v2",
            "active_player_count": len(scenario["players"]),
            "cpu_pad_modes": [p["cpu_pad_mode"] for p in scenario["players"]],
            "raw_template_policy": "empty calibration/center/modifier; zero deadzone/notches",
        },
        "writes": {
            "source_memory": "none; reject baseline if a requested availability bit is missing",
            "gameplay_state": [], "rng": [], "fighter_state": [],
            "cpu_state": [], "registers": [],
        },
        "capture": {
            "final_checkpoint_required": False,
            "menu_preparation": "inline in every fresh Dolphin process",
            "native_input_plan": "melee-web-retail-input-plan-v3",
            "natural_end_required": True,
            "allowed_result": ["winner", "timeout", "tie", "cap_exhausted_incomplete"],
            "long_capture_started": False,
        },
    }


def capture_command(scenario_id: str, bundle: Path, player_count: int, frames: int) -> str:
    try:
        bundle_ref = str(bundle.resolve().relative_to(ROOT))
    except ValueError:
        bundle_ref = "${BUNDLE_DIR:?set BUNDLE_DIR to this prepared bundle}"
    return f'''# Run from the repository root after setting only owned host asset paths.
export MELEE_DOLPHIN="${{MELEE_DOLPHIN:?set path to the pinned Dolphin.app}}"
export MELEE_DISC="${{MELEE_DISC:?set path to the owned Melee disc image}}"
export MELEE_DOL="${{MELEE_DOL:?set path to the pinned GALE01 main.dol}}"
BUNDLE_DIR="{bundle_ref}"
MELEE_REPLAY_COLLECTOR="$BUNDLE_DIR/collector/reference_replay_capture.py" \\
python3 scripts/capture_retail_replay.py \\
  --dolphin "$MELEE_DOLPHIN" --disc "$MELEE_DISC" --dol "$MELEE_DOL" \\
  --template-user "$BUNDLE_DIR/template-user" \\
  --snapshot "$BUNDLE_DIR/owned-donor.sav" --checkpoint-gc "$BUNDLE_DIR/checkpoint-gc" \\
  --provenance "$BUNDLE_DIR/provenance.json" --input-plan "$BUNDLE_DIR/input-plan.json" \\
  --frames {frames} --until-match-end --draw-audit --cpu Interpreter64 \\
  --output "$BUNDLE_DIR/discovery.jsonl" --timeout 1800

# Keep timeout/tie and cap_exhausted_incomplete results with their run evidence.
# A fixed follow-up may use a discovered natural end only after review; do not
# append neutral input and do not request --require-match-complete here.
'''


def prepare_one(scenario_id: str, output_root: Path, inputs: OwnedInputs,
                ticks: int | None = None) -> Path:
    scenario = get_scenario(scenario_id)
    validate_scenario(scenario)
    bundle = output_root / scenario_id
    if bundle.exists():
        raise RuntimeError(f"refusing to reuse existing corpus bundle: {bundle}")
    output_root.mkdir(parents=True, exist_ok=True)
    bundle.mkdir()
    # Validate before copying to leave a concrete failed bundle when an owned
    # input is malformed. Existing failures are never removed or overwritten.
    _regular_file(inputs.snapshot, "owned donor snapshot")
    _regular_directory(inputs.checkpoint_gc, "owned external GC state")
    _regular_file(inputs.provenance, "owned donor provenance")
    template = _regular_directory(inputs.raw_template_user, "raw-template-user")
    paths = {
        "snapshot": bundle / "owned-donor.sav",
        "template": bundle / "template-user",
        "gc": bundle / "checkpoint-gc",
    }
    shutil.copy2(inputs.snapshot, paths["snapshot"])
    config = copy_raw_template(template, paths["template"], len(scenario["players"]))
    shutil.copytree(inputs.checkpoint_gc, paths["gc"])

    target = scenario_target(scenario)
    plan = make_input_plan(scenario, ticks)
    validate_input_plan(plan, scenario["human_workload"]["capture_cap_ticks"])
    driver = render_source_driver()
    validate_driver()
    target_for_driver = target
    write_json(bundle / "scenario.json", scenario)
    write_json(bundle / "target.json", target)
    write_json(bundle / "input-plan.json", plan)
    collector_hashes = copy_collector_bundle(bundle, target_for_driver)
    (bundle / "capture-command.txt").write_text(
        capture_command(scenario_id, bundle, len(scenario["players"]), len(plan["frames"])),
        encoding="utf-8")
    write_json(bundle / "preparation.json",
               preparation_manifest(scenario, target, plan, bundle, inputs,
                                    config, collector_hashes))

    provenance = json.loads(inputs.provenance.read_text(encoding="utf-8"))
    provenance.update({
        "cpu": "Interpreter64",
        "setup": "original source CSS/SSS menu preparation; authored corpus scenario",
        "setup_snapshot_sha256": sha256(paths["snapshot"]),
        "setup_snapshot_origin": provenance.get("setup_snapshot_origin", "owned original SSS donor"),
        "donor_setup": provenance.get("setup"),
        "donor_provenance_sha256": sha256(inputs.provenance),
        "external_save_hashes": hash_tree(paths["gc"]),
        "Dolphin.ini_sha256": sha256(paths["template"] / "Config" / "Dolphin.ini"),
        "GCPadNew.ini_sha256": sha256(config),
        "corpus_scenario_sha256": sha256_bytes(canonical(scenario)),
        "corpus_plan_sha256": sha256_bytes(canonical(plan)),
        "final_checkpoint_status": "not_required_inline_menu_preparation",
    })
    write_json(bundle / "provenance.json", provenance)
    return bundle


def _path_argument(parser: argparse.ArgumentParser, name: str, default: Path | None,
                   help_text: str) -> None:
    parser.add_argument(name, type=Path, default=default, help=help_text)


def main(argv: list[str] | None = None) -> int:
    validate_catalog()
    parser = argparse.ArgumentParser(description=__doc__)
    choices = scenario_ids()
    parser.add_argument("--scenario", choices=choices)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--donor-root", type=Path, default=DEFAULT_DONOR_ROOT)
    parser.add_argument("--raw-template-user", type=Path,
                        default=DEFAULT_DONOR_ROOT / "raw-template-user")
    parser.add_argument("--snapshot", type=Path,
                        default=DEFAULT_DONOR_ROOT / "base-sss-mario-fd.sav")
    parser.add_argument("--checkpoint-gc", type=Path,
                        default=DEFAULT_DONOR_ROOT / "checkpoint-gc")
    parser.add_argument("--provenance", type=Path,
                        default=DEFAULT_DONOR_ROOT / "base-provenance.json")
    _path_argument(parser, "--dolphin", None, "owned Dolphin path recorded only for caller use")
    _path_argument(parser, "--disc", None, "owned disc path recorded only for caller use")
    _path_argument(parser, "--dol", None, "owned main.dol path recorded only for caller use")
    parser.add_argument("--ticks", type=int, default=None,
                        help="authored capture prefix length (default: each scenario's cap)")
    parser.add_argument("--check-only", action="store_true",
                        help="validate recipes and the embedded driver without copying assets")
    args = parser.parse_args(argv)
    if args.all == bool(args.scenario):
        parser.error("choose exactly one of --scenario or --all")
    selected = choices if args.all else [args.scenario]
    if args.check_only:
        for scenario_id in selected:
            scenario = get_scenario(scenario_id)
            plan = make_input_plan(scenario, args.ticks)
            validate_input_plan(plan, scenario["human_workload"]["capture_cap_ticks"])
        print(json.dumps({"status": "recipes_valid", "scenarios": selected,
                          "dolphin_launched": False,
                          "final_checkpoint_required": False}, sort_keys=True))
        return 0
    inputs = OwnedInputs(
        donor_root=args.donor_root,
        raw_template_user=args.raw_template_user,
        snapshot=args.snapshot,
        checkpoint_gc=args.checkpoint_gc,
        provenance=args.provenance,
        dolphin=args.dolphin,
        disc=args.disc,
        dol=args.dol,
    )
    bundles = [prepare_one(scenario_id, args.output_root, inputs, args.ticks)
               for scenario_id in selected]
    print(json.dumps({"status": "prepared_inline_menu_capture_pending",
                      "bundles": [str(path) for path in bundles]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
