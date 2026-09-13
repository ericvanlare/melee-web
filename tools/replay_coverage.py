"""Measure vanilla replay execution coverage and select marginal canaries.

This module consumes validated retail collector JSONL traces. It never reads
Slippi post-frame observations and never treats a donor as an expected-state
oracle. Source motion labels come from the pinned Melee enum headers.
"""
from __future__ import annotations

import ast
import hashlib
from pathlib import Path
import re
import struct
from typing import Iterable, Mapping, Sequence

from retail_input_plan import load_plan, verify_capture
from retail_replay_validation import CaptureError, load_capture


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_ROOT = ROOT / ".deps" / "melee" / "src"
SCHEMA = "melee-web-replay-coverage"
# Version 2 changes special-family labels from posture-plus-direction to
# direction only. Existing version-1 reports remain historical ledgers; their
# labels are not rewritten. Physical posture is reported by ground_air fields
# and transitions instead.
VERSION = 2
CPUS = ("Interpreter64", "JITARM64")

_FIGHTER_KIND_RE = re.compile(r"^FTKIND_[A-Z0-9_]+$")
_ENUM_NAME_RE = re.compile(r"^([A-Za-z_]\w*)_MS_(.+)$")
class CoverageError(ValueError):
    """The capture or pinned source mapping is not trustworthy."""


def _sha256_source(paths: Iterable[Path], source_root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(set(paths)):
        try:
            relative = path.resolve().relative_to(source_root.resolve()).as_posix()
        except ValueError as error:
            raise CoverageError(f"source mapping escaped source root: {path}") from error
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def _strip_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)


def _eval_enum_expr(expression: str, values: Mapping[str, int], source: Path) -> int:
    """Evaluate only the integer expressions used by the pinned source enums."""
    expression = expression.strip()
    try:
        tree = ast.parse(expression, mode="eval")
    except SyntaxError as error:
        raise CoverageError(
            f"ambiguous enum expression in {source}: {expression!r}") from error

    def evaluate(node: ast.AST) -> int:
        if isinstance(node, ast.Constant) and isinstance(node.value, int) \
                and not isinstance(node.value, bool):
            return node.value
        if isinstance(node, ast.Name):
            if node.id not in values:
                raise CoverageError(
                    f"ambiguous enum expression references unknown value "
                    f"{node.id!r} in {source}")
            return values[node.id]
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            value = evaluate(node.operand)
            return value if isinstance(node.op, ast.UAdd) else -value
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub)):
            left, right = evaluate(node.left), evaluate(node.right)
            return left + right if isinstance(node.op, ast.Add) else left - right
        raise CoverageError(
            f"ambiguous enum expression in {source}: {expression!r}")

    return evaluate(tree.body)


def _enum_body(text: str, enum_name: str, source: Path) -> str:
    clean = _strip_comments(text)
    match = re.search(
        r"typedef\s+enum\s+" + re.escape(enum_name) +
        r"\s*\{(.*?)\}\s*[A-Za-z_]\w*\s*;",
        clean, flags=re.S,
    )
    if not match:
        raise CoverageError(f"pinned source enum {enum_name} is missing from {source}")
    return match.group(1)


def _parse_enum(
    text: str,
    enum_name: str,
    source: Path,
    initial_values: Mapping[str, int] | None = None,
) -> dict[str, int]:
    values: dict[str, int] = dict(initial_values or {})
    local_names: list[str] = []
    body = _enum_body(text, enum_name, source)
    for raw in body.split(","):
        entry = raw.strip()
        if not entry:
            continue
        match = re.fullmatch(r"([A-Za-z_]\w*)\s*(?:=\s*(.*))?", entry, flags=re.S)
        if not match:
            raise CoverageError(f"ambiguous enum entry in {source}: {entry!r}")
        name, expression = match.groups()
        if expression is None:
            values[name] = values[local_names[-1]] + 1 if local_names else 0
        else:
            values[name] = _eval_enum_expr(expression, values, source)
        local_names.append(name)
    return {name: values[name] for name in local_names}


def _parse_fighter_kinds(source: Path) -> dict[int, str]:
    text = source.read_text()
    body = _enum_body(text, "FighterKind", source)
    values: dict[str, int] = {}
    result: dict[int, str] = {}
    for raw in body.split(","):
        entry = raw.strip()
        if not entry:
            continue
        match = re.fullmatch(r"(FTKIND_[A-Z0-9_]+)\s*(?:=\s*(.*))?", entry, flags=re.S)
        if not match:
            raise CoverageError(f"ambiguous FighterKind entry in {source}: {entry!r}")
        name, expression = match.groups()
        if not _FIGHTER_KIND_RE.fullmatch(name):
            raise CoverageError(f"invalid FighterKind entry {name!r}")
        value = 0 if expression is None and not values else (
            values[list(values)[-1]] + 1 if expression is None
            else _eval_enum_expr(expression, values, source)
        )
        values[name] = value
        if name not in {"FTKIND_NONE", "FTKIND_MAX"}:
            if value in result:
                raise CoverageError(f"duplicate FighterKind value {value} in {source}")
            result[value] = name
    if not result:
        raise CoverageError(f"FighterKind enum has no usable entries in {source}")
    return result


def _motion_enum_files(source_root: Path) -> list[Path]:
    paths = sorted((source_root / "melee" / "ft" / "kinds").glob("*/forward.h"))
    if not paths:
        raise CoverageError(f"pinned fighter motion headers are missing below {source_root}")
    return paths


def _enum_candidates(
    source_root: Path,
    common_values: Mapping[str, int],
) -> dict[str, list[tuple[Path, dict[str, int]]]]:
    result: dict[str, list[tuple[Path, dict[str, int]]]] = {}
    for path in _motion_enum_files(source_root):
        text = path.read_text()
        for match in re.finditer(
                r"typedef\s+enum\s+([A-Za-z_]\w*_MotionState)\s*\{",
                _strip_comments(text)):
            enum_name = match.group(1)
            try:
                values = _parse_enum(text, enum_name, path, common_values)
            except CoverageError:
                # Some headers contain only an unsupported boss sentinel alias
                # (for example Crazy Hand's Count/SelfCount).  Ignore that
                # empty mapping, but never hide an ambiguous expression in an
                # enum that could contribute observed motion names.
                names = re.findall(
                    r"\b([A-Za-z_]\w*_MS_[A-Za-z0-9_]+)\b",
                    _enum_body(text, enum_name, path),
                )
                if names and all(name.endswith(("_MS_Count", "_MS_SelfCount"))
                                 for name in names):
                    continue
                raise
            by_prefix = {}
            for name, value in values.items():
                name_match = _ENUM_NAME_RE.fullmatch(name)
                if name_match and not name.endswith(("_MS_Count", "_MS_SelfCount")):
                    by_prefix.setdefault(name_match.group(1), {})[value] = name
            for prefix, mapping in by_prefix.items():
                result.setdefault(prefix, []).append((path, mapping))
    return result


def _motion_table_prefixes(source_root: Path) -> list[str | None]:
    source = source_root / "melee" / "ft" / "ftdata.c"
    clean = _strip_comments(source.read_text())
    match = re.search(
        r"MotionState\*\s+ftData_CharacterStateTables\s*\[[^]]+\]\s*=\s*\{(.*?)\};",
        clean, flags=re.S,
    )
    if not match:
        raise CoverageError(f"source motion table registry is missing from {source}")
    entries = [item.strip() for item in match.group(1).split(",") if item.strip()]
    prefixes = []
    for entry in entries:
        if entry == "NULL":
            prefixes.append(None)
            continue
        found = re.fullmatch(r"([A-Za-z_]\w*)_Init_MotionStateTable", entry)
        if not found:
            raise CoverageError(
                f"ambiguous source motion table entry in {source}: {entry!r}")
        prefixes.append(found.group(1))
    return prefixes


def _table_forward_header(source_root: Path, prefix: str) -> Path | None:
    """Resolve shared fighter enum headers through the source table owner."""
    needle = re.compile(
        r"\bMotionState\s+" + re.escape(prefix) + r"_Init_MotionStateTable\s*\[")
    matches: list[Path] = []
    for path in sorted((source_root / "melee" / "ft" / "kinds").rglob("*.c")):
        text = path.read_text()
        if not needle.search(text):
            continue
        includes = re.findall(
            r"#include\s*[<\"](melee/ft/kinds/[^>\"]+/forward\.h)[>\"]", text)
        headers = [source_root / item for item in includes
                   if "/ftCommon/" not in item]
        if not headers:
            return None
        if len(headers) != 1:
            raise CoverageError(
                f"ambiguous forward-header owner for motion table {prefix!r} in {path}")
        matches.append(headers[0])
    if not matches:
        return None
    if len(set(matches)) != 1:
        raise CoverageError(f"motion table {prefix!r} has multiple forward-header owners")
    return matches[0]


def _resolve_motion_maps(source_root: Path) -> tuple[dict[int, str], dict[int, dict[int, str]], set[Path]]:
    source_root = source_root.resolve()
    fighter_source = source_root / "melee" / "ft" / "forward.h"
    common_source = source_root / "melee" / "ft" / "kinds" / "ftCommon" / "forward.h"
    fighter_kinds = _parse_fighter_kinds(fighter_source)
    common_values = _parse_enum(common_source.read_text(), "ftCommon_MotionState", common_source)
    common = {
        value: name for name, value in common_values.items()
        if not name.endswith(("_MS_None", "_MS_Count", "_MS_SelfCount"))
    }
    if len(common) != sum(
            not name.endswith(("_MS_None", "_MS_Count", "_MS_SelfCount"))
            for name in common_values):
        raise CoverageError("duplicate common motion ID in pinned enum")

    candidates = _enum_candidates(source_root, common_values)
    prefixes = _motion_table_prefixes(source_root)
    if len(prefixes) != len(fighter_kinds):
        raise CoverageError(
            f"fighter motion table has {len(prefixes)} rows but FighterKind has "
            f"{len(fighter_kinds)} usable kinds")

    by_kind: dict[int, dict[int, str]] = {}
    # _enum_candidates reads every fighter motion header and
    # _table_forward_header scans every kinds/**/*.c file while resolving
    # table ownership. Include that complete read inventory in the mapping
    # hash so provenance covers all files that can affect resolution.
    motion_sources = set(_motion_enum_files(source_root))
    table_sources = {
        path for path in (source_root / "melee" / "ft" / "kinds").rglob("*.c")
        if path.is_file()
    }
    used = {
        fighter_source,
        common_source,
        source_root / "melee" / "ft" / "ftdata.c",
    } | motion_sources | table_sources
    for kind, prefix in zip(sorted(fighter_kinds), prefixes):
        if prefix is None:
            by_kind[kind] = {}
            continue
        matches = candidates.get(prefix, [])
        if not matches:
            header = _table_forward_header(source_root, prefix)
            if header is not None:
                matches = [item for item in candidates.get(prefix, []) if item[0] == header]
                if not matches:
                    # Shared Fox/Falco tables use ftFx enum names while the
                    # per-kind source table is named ftFc.
                    for candidate_prefix, candidate_items in candidates.items():
                        if any(item[0] == header for item in candidate_items):
                            matches = [item for item in candidate_items if item[0] == header]
                            break
                used.add(header)
        if not matches:
            # Boss-only or shared aliases without a standalone motion enum are
            # represented as unsupported. An observed ID still fails closed
            # in _motion instead of receiving a guessed family.
            by_kind[kind] = {}
            continue
        if len(matches) != 1:
            raise CoverageError(
                f"fighter kind {kind} ({fighter_kinds[kind]}) has "
                f"{len(matches)} pinned enum mappings for {prefix!r}")
        path, mapping = matches[0]
        if any(value in common for value in mapping):
            raise CoverageError(
                f"fighter enum {path} overlaps common motion IDs for kind {kind}")
        by_kind[kind] = mapping
        used.add(path)
    return common, by_kind, used


def load_source_mapping(source_root: Path = DEFAULT_SOURCE_ROOT) -> dict:
    """Load verified source motion/fighter mappings and their content hash."""
    source_root = Path(source_root).resolve()
    common, by_kind, used = _resolve_motion_maps(source_root)
    fighter_source = source_root / "melee" / "ft" / "forward.h"
    fighter_kinds = _parse_fighter_kinds(fighter_source)
    names = {kind: name for kind, name in fighter_kinds.items()}
    return {
        "common": common,
        "fighters": by_kind,
        "fighter_names": names,
        "source_files": sorted(
            path.resolve().relative_to(source_root).as_posix() for path in used
        ),
        "source_sha256": _sha256_source(used, source_root),
    }


def _family(name: str, *, special: bool) -> str | None:
    if special:
        match = re.search(r"_MS_Special(?:Air)?(N|S|Hi|Lw)", name)
        if not match:
            return None
        direction = match.group(1)
        labels = {"N": "neutral", "S": "side", "Hi": "up", "Lw": "down"}
        return f"special:{labels[direction]}"
    suffix = name.split("_MS_", 1)[-1]
    # These labels are deliberately sourced from exact pinned enum names; no
    # numeric motion ranges are used.
    groups = (
        ("death", ("Dead", "Sleep")),
        ("respawn", ("Rebirth",)),
        ("damage", ("Damage",)),
        ("attack", ("Attack",)),
        ("defense", ("Guard", "ShieldBreak", "Furafura")),
        ("grab-throw", ("Catch", "Capture", "Throw", "Thrown", "Lift")),
        ("recovery", ("Cliff", "Escape", "Rebound", "Passive", "FlyReflect")),
        ("downed", ("Down",)),
        ("item", ("Item",)),
        ("movement", ("Wait", "Walk", "Turn", "Dash", "Run", "KneeBend",
                      "Jump", "Fall", "Landing", "Squat", "Pass", "Ottotto",
                      "StopWall", "StopCeil")),
    )
    for family, prefixes in groups:
        if suffix.startswith(prefixes):
            return f"common:{family}"
    return None


def _motion(mapping: Mapping, kind: int, motion: int) -> tuple[str | None, str | None, str | None]:
    common = mapping["common"]
    if motion in common:
        name = common[motion]
        return name, "common", _family(name, special=False)
    fighters = mapping["fighters"].get(kind)
    if fighters is None:
        raise CoverageError(f"fighter kind {kind} has no pinned motion mapping")
    if motion not in fighters:
        raise CoverageError(
            f"fighter kind {kind} has observed motion {motion} absent from pinned source mapping")
    name = fighters[motion]
    return name, "fighter_specific", _family(name, special=True)


def _bits_value(value: str) -> float:
    return struct.unpack(">f", bytes.fromhex(value))[0]


def _features_for_capture(capture: Mapping) -> set[str]:
    features = set()
    for fighter in capture["players"]:
        kind = fighter["kind"]
        features.add(f"fighter:{kind}")
        for motion in fighter["motions"]:
            features.add(f"motion:{kind}:{motion['id']}")
            if motion.get("family"):
                features.add(f"family:{motion['family']}")
        for transition in fighter["motion_transitions"]:
            features.add(
                f"motion-transition:{kind}:{transition['from_id']}->{transition['to_id']}")
        for transition in fighter["ground_air_transitions"]:
            features.add(
                f"ground-air-transition:{kind}:{transition['from']}->{transition['to']}")
        if fighter["stock_losses"]:
            features.add(f"event:stock-loss:{kind}")
        if fighter["respawns"]:
            features.add(f"event:respawn:{kind}")
        if fighter["damage_increases"]:
            features.add(f"event:damage-increase:{kind}")
        if fighter["knockback_changes"]:
            features.add(f"event:knockback-change:{kind}")
        if fighter["knockback_active_ticks"]:
            features.add(f"event:knockback-active:{kind}")
    return features


def _analyze_frames(capture: Mapping, frames: Sequence[dict], mapping: Mapping) -> dict:
    by_slot = {fighter["slot"]: fighter for fighter in frames[0]["fighters"]}
    players = []
    for slot in sorted(by_slot):
        first = by_slot[slot]
        kind = first["kind"]
        motion_states: dict[int, dict] = {}
        motion_transitions = []
        ground_transitions = []
        stock_losses = []
        respawns = []
        damage_increases = []
        knockback_changes = []
        active_knockback_ticks = []
        previous = None
        for frame in frames:
            current = next(fighter for fighter in frame["fighters"] if fighter["slot"] == slot)
            if current["kind"] != kind:
                raise CoverageError(
                    f"{capture['path']}: slot {slot} fighter kind changed during capture")
            motion = current["motion"]
            name, scope, family = _motion(mapping, kind, motion)
            state = motion_states.setdefault(
                motion, {"id": motion, "name": name, "scope": scope,
                         "family": family, "ticks": 0, "entries": 0})
            state["ticks"] += 1
            if previous is not None:
                if motion != previous["motion"]:
                    state["entries"] += 1
                    old_name = previous["name"]
                    motion_transitions.append({
                        "tick": frame["index"], "from_id": previous["motion"],
                        "from_name": old_name, "to_id": motion, "to_name": name,
                    })
                if current["ground_air"] != previous["ground_air"]:
                    ground_transitions.append({
                        "tick": frame["index"], "from": previous["ground_air"],
                        "to": current["ground_air"],
                    })
                if current["stocks"] < previous["stocks"]:
                    stock_losses.append({
                        "tick": frame["index"], "before": previous["stocks"],
                        "after": current["stocks"],
                    })
                old_damage = _bits_value(previous["damage_bits"])
                new_damage = _bits_value(current["damage_bits"])
                if new_damage > old_damage:
                    damage_increases.append({
                        "tick": frame["index"], "before_bits": previous["damage_bits"],
                        "after_bits": current["damage_bits"],
                        "delta": new_damage - old_damage,
                    })
                old_kb = tuple(previous["knockback_bits"])
                new_kb = tuple(current["knockback_bits"])
                if new_kb != old_kb:
                    knockback_changes.append({
                        "tick": frame["index"], "before_bits": list(old_kb),
                        "after_bits": list(new_kb),
                    })
                if any(_bits_value(value) != 0.0 for value in current["knockback_bits"]):
                    active_knockback_ticks.append(frame["index"])
            else:
                state["entries"] += 1
            previous = {
                "motion": motion, "name": name, "ground_air": current["ground_air"],
                "stocks": current["stocks"], "damage_bits": current["damage_bits"],
                "knockback_bits": list(current["knockback_bits"]),
            }
        for event in motion_transitions:
            if event["to_name"] == "ftCo_MS_Rebirth":
                respawns.append({"tick": event["tick"], "motion": event["to_id"],
                                 "name": event["to_name"]})
        players.append({
            "slot": slot, "kind": kind, "kind_name": mapping["fighter_names"].get(kind),
            "motions": [motion_states[key] for key in sorted(motion_states)],
            "motion_transitions": motion_transitions,
            "ground_air_transitions": ground_transitions,
            "stock_losses": stock_losses,
            "respawns": respawns,
            "damage_increases": damage_increases,
            "knockback_changes": knockback_changes,
            "knockback_active_ticks": active_knockback_ticks,
        })
    return players


def analyze_capture(
    path: str | Path,
    *,
    input_plan: str | Path,
    cpu: str,
    source_root: str | Path = DEFAULT_SOURCE_ROOT,
    mapping: Mapping | None = None,
) -> dict:
    """Validate one complete retail capture against its exact input plan."""
    if cpu not in CPUS:
        raise CoverageError(f"unsupported CPU {cpu!r}; choose one of {CPUS}")
    try:
        capture = load_capture(path, cpu=cpu)
        plan, plan_sha256 = load_plan(input_plan)
        if capture.header["provenance"].get("input_plan_sha256") != plan_sha256:
            raise CoverageError(
                f"{path}: capture input_plan_sha256 does not match {input_plan}")
        verify_capture(plan, capture)
    except (CaptureError, OSError, TypeError, ValueError) as error:
        if isinstance(error, CoverageError):
            raise
        raise CoverageError(f"{path}: validated capture/plan binding failed: {error}") from error
    if mapping is None:
        mapping = load_source_mapping(source_root)
    frames = capture.frames
    players = _analyze_frames({"path": str(path)}, frames, mapping)
    # verify_capture has already checked these values against the complete
    # StartMelee bytes (including stage at 0x0e and the external character
    # slots).  Reuse the verified plan values rather than parsing a second,
    # weaker entry-header interpretation here.
    stage = plan["source_stage"]
    source_characters = list(plan["source_characters"])
    # StartMelee character IDs are external Slippi/CSS IDs.  The observed
    # fighter kinds above are internal FighterKind values, so keep the former
    # in provenance only and never merge the two namespaces into one feature.
    features = _features_for_capture({"players": players})
    features.add(f"stage:{stage}")
    source_sha256 = plan["source_sha256"]
    capture = {
        "path": str(path),
        "capture_sha256": capture.sha256,
        "input_plan_sha256": plan_sha256,
        "source_sha256": source_sha256,
        "source_hashes": sorted({source_sha256, plan_sha256}),
        "input_policy": plan["policy"],
        "source_stage": stage,
        "source_characters": source_characters,
        "provenance": capture.header["provenance"],
        "frames": len(frames),
        "frames_requested": capture.header["frames_requested"],
        "game_revision": capture.header.get("game_revision"),
        "players": players,
        "features": sorted(features),
        "scope": (
            "Observed execution coverage from a validated bounded vanilla retail "
            "capture; donor post-frame state, port equivalence, pixels, PCM, "
            "physical input and content admission are outside scope. Special "
            "family labels carry direction only; physical posture comes from "
            "ground_air fields and transitions."
        ),
    }
    return capture


def analyze_cpu_capture(path, *, input_plan, observation, completion,
                        cpu="Interpreter64", source_root=DEFAULT_SOURCE_ROOT):
    """Extend the existing motion/event report with observed CPU match coverage.

    CPU command-buffer inventory means queued commands, not an assertion that
    every command ran. Decision-state samples, generated PAD, and fighter
    motion entries provide separate execution evidence.
    """
    from cpu_observation_validation import load_observation
    from retail_match_completion import load_match_completion
    from retail_setup_validation import _decode_setup

    report = analyze_capture(path, input_plan=input_plan, cpu=cpu, source_root=source_root)
    capture = load_capture(path, cpu=cpu)
    audit = load_observation(observation, capture)
    ending = load_match_completion(capture, completion, require_complete=True)
    setup = _decode_setup(capture.match_enter['start_melee_hex'])
    raw_setup = bytes.fromhex(capture.match_enter['start_melee_hex'])
    for slot, settings in enumerate(setup['players']):
        settings['team'] = raw_setup[0x69 + slot * 0x24]
        settings['sub_color'] = raw_setup[0x67 + slot * 0x24]
        settings['source_slot'] = raw_setup[0x64 + slot * 0x24]
    source = Path(source_root) / 'melee/ft/ftcmdscript.h'
    text = re.sub(r'^\s*#.*$', '', source.read_text(), flags=re.M)
    commands = _parse_enum(text, 'CPUCommand', source)
    names = {value: name for name, value in commands.items()
             if name not in ('CpuCmd_ZeroArgEnd', 'CpuCmd_OneArgEnd', 'CpuCmd_Count')}

    def program(hex_bytes):
        raw = bytes.fromhex(hex_bytes)
        cursor, result = 0, set()
        while cursor < len(raw):
            opcode = raw[cursor]
            if opcode not in names:
                raise CoverageError(f'CPU buffer has an unmapped source command: {opcode:#x}')
            width = 1 if opcode <= 0x7f else 2 if opcode <= 0xbf else 3
            if cursor + width > len(raw):
                raise CoverageError('CPU buffer ends inside a command')
            result.add(opcode)
            cursor += width
        return result

    drawing_available = bool(audit.header['source_drawing'])
    for player, settings in zip(report['players'], setup['players']):
        slot = player['slot']
        player['configuration'] = settings
        player['role'] = 'cpu' if settings['player_type'] == 1 else 'human'
        player['attack_motion_entries'] = sum(motion['entries'] for motion in player['motions']
            if 'Attack' in motion['name'] or 'Throw' in motion['name'])
        samples = [row['players'][slot] for row in audit.frames]
        target_changes, state_changes = [], []
        state_ticks, commands_seen = {}, set()
        target_ticks = {}
        queue_values = {'attack': set(), 'defend': set()}
        queue_ticks = {'attack': {}, 'defend': {}}
        queue_transitions = {'attack': [], 'defend': []}
        generated_active_ticks = 0
        initial_target = None
        previous_decision = None
        for index, sample in enumerate(samples):
            decision = sample['cpu']
            if decision is None:
                continue
            if initial_target is None:
                initial_target = decision['target_slot']
            state = decision['state']
            state_ticks[state] = state_ticks.get(state, 0) + 1
            target = decision['target_slot']
            target_ticks[target] = target_ticks.get(target, 0) + 1
            commands_seen |= program(decision['command_bytes'])
            generated_active_ticks += bool(decision['buttons'] or any(decision['sticks']) or any(decision['triggers']))
            for queue_name, field in (('attack', 'attack_queue'),
                                      ('defend', 'defend_queue')):
                queue = tuple(decision.get(field, ()))
                for value in set(queue):
                    queue_values[queue_name].add(value)
                    ticks = queue_ticks[queue_name]
                    ticks[value] = ticks.get(value, 0) + 1
                if previous_decision is not None:
                    previous_queue = tuple(previous_decision.get(field, ()))
                    if queue != previous_queue:
                        queue_transitions[queue_name].append({
                            'tick': index, 'from': list(previous_queue),
                            'to': list(queue),
                        })
            if previous_decision is not None:
                for key, events in [('target_slot', target_changes), ('state', state_changes)]:
                    if decision[key] != previous_decision[key]:
                        events.append({'tick': index, 'from': previous_decision[key],
                                       'to': decision[key]})
            previous_decision = decision
        if player['role'] == 'cpu':
            player['cpu'] = {
                'difficulty': settings['cpu_level'],
                'decision_state_ticks': {str(key): state_ticks[key] for key in sorted(state_ticks)},
                'decision_state_changes': state_changes, 'target_changes': target_changes,
                'initial_target_slot': initial_target,
                'target_ticks': {str(key): target_ticks[key] for key in sorted(target_ticks)},
                'generated_active_input_ticks': generated_active_ticks,
                'queued_commands': [{'id': cmd, 'name': names[cmd]} for cmd in sorted(commands_seen)],
                'attack_queue': {
                    'observed_ids': sorted(queue_values['attack']),
                    'tick_counts': {str(key): queue_ticks['attack'][key]
                                    for key in sorted(queue_ticks['attack'])},
                    'transitions': queue_transitions['attack'],
                },
                'defend_queue': {
                    'observed_ids': sorted(queue_values['defend']),
                    'tick_counts': {str(key): queue_ticks['defend'][key]
                                    for key in sorted(queue_ticks['defend'])},
                    'transitions': queue_transitions['defend'],
                },
                'decision_state_definition': 'source CpuFighter.x18; numeric states are retained without guessed labels',
                'queue_observation_scope': 'active source queue values and transitions; no consumption or execution inference',
                'source_motion_inventory': [
                    {'id': motion['id'], 'name': motion['name'],
                     'scope': motion['scope'], 'family': motion.get('family'),
                     'ticks': motion['ticks'], 'entries': motion['entries']}
                    for motion in player['motions']
                ],
            }
        player['drawing_coverage'] = {
            'available': drawing_available,
            'phase': 'source_draws' if drawing_available else 'simulation_frames_only',
            'source_draws': len(audit.draws),
        }
        player['magnifier_events'] = []
        player['offscreen_draws'] = 0
        player['camera_subject_events'] = []
        if drawing_available:
            last = audit.initial['players'][slot]
            for row in audit.draws:
                sample = row['players'][slot]
                player['offscreen_draws'] += sample['magnifier']['offscreen']
                if sample['magnifier'] != last['magnifier']:
                    player['magnifier_events'].append({'tick': row['source_index'],
                        'before': last['magnifier'], 'after': sample['magnifier']})
                before = last['subject']
                after = sample['subject']
                if ((before is None) != (after is None) or
                        (before is not None and after is not None and
                         any(before[key] != after[key] for key in ('state', 'on_ledge', 'force_inactive', 'was_framed')))):
                    player['camera_subject_events'].append({'tick': row['source_index'],
                        'before': before, 'after': after})
                last = sample
        else:
            # Frame snapshots retain simulation state, but no draw-phase
            # camera or magnifier coverage exists in a headless sidecar.
            player['magnifier_events'] = None
            player['offscreen_draws'] = None
            player['camera_subject_events'] = None
    camera_transform_changes = None
    if drawing_available:
        camera_transform_changes = sum(left['camera'] != right['camera']
            for left, right in zip((audit.initial, *audit.draws[:-1]), audit.draws))
    report['cpu_match'] = {
        'schema_version': 1, 'player_count': len(setup['players']),
        'roles': [player['role'] for player in report['players']],
        'rules_and_players': setup,
        'cpu_difficulties': [p['cpu_level'] for p in setup['players'] if p['player_type'] == 1],
        'source_ticks': len(audit.frames), 'source_draws': len(audit.draws),
        'duration_nominal_seconds': len(audit.frames) / 60,
        'duration_scope': 'nominal source ticks divided by 60; not measured wall time or a retail VI cadence claim',
        'drawing': {
            'available': drawing_available,
            'phase': 'source_draws' if drawing_available else 'simulation_frames_only',
            'source_draws': len(audit.draws),
        },
        'camera_transform_changes': camera_transform_changes,
        'ending': ending, 'final_match_state': audit.frames[-1]['match'],
        'result': audit.end['result'],
        'teardown_remaining_slots': audit.end['remaining_fighter_slots'],
        'observation_sha256': audit.sha256,
        'cpu_command_mapping_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    }
    features = set(report.get('features', ()))
    features.add(f"player-count:{len(setup['players'])}")
    for player in report['players']:
        slot = player['slot']
        features.add(f"role:{slot}:{player['role']}")
        if player['role'] == 'cpu':
            cpu_report = player['cpu']
            features.add(f"cpu-level:{slot}:{cpu_report['difficulty']}")
            for state in cpu_report['decision_state_ticks']:
                features.add(f"cpu-state:{slot}:{state}")
            for motion in cpu_report['source_motion_inventory']:
                features.add(f"cpu-motion:{slot}:{motion['id']}")
                if motion['family']:
                    features.add(f"cpu-family:{slot}:{motion['family']}")
            for target in cpu_report['target_ticks']:
                features.add(f"cpu-target:{slot}:{target}")
            for command in cpu_report['queued_commands']:
                features.add(f"cpu-command:{slot}:{command['id']}")
            for queue_name, field in (('attack', 'attack_queue'),
                                      ('defend', 'defend_queue')):
                for value in cpu_report[field]['observed_ids']:
                    features.add(f"cpu-queue:{slot}:{queue_name}:{value}")
            if cpu_report['generated_active_input_ticks']:
                features.add(f"cpu-input-active:{slot}")
    features.add('drawing:source' if drawing_available else 'drawing:headless')
    if drawing_available:
        if camera_transform_changes:
            features.add('camera:transform-change')
        if any(player['magnifier_events'] or player['offscreen_draws']
               for player in report['players']):
            features.add('magnifier:observed')
    if isinstance(ending.get('ending'), dict):
        mode = ending['ending'].get('mode')
        if mode is not None:
            features.add(f"ending:mode:{mode}")
        winners = ending['ending'].get('winner_slots')
        if isinstance(winners, list):
            features.add(f"ending:winner-count:{len(winners)}")
    if ending.get('match_result') is not None:
        features.add(f"ending:result:{ending['match_result']}")
    report['features'] = sorted(features)
    report['scope'] += (' CPU, camera/HUD/magnifier and ending coverage is from the exact '
                        'semantic sidecar. Queued command inventory is not command execution coverage; '
                        'attack/defend queue values are observed snapshots without consumption inference; '
                        'damage events identify receivers, not inferred attackers. Draw-phase fields are '
                        'marked unavailable for headless sidecars.')
    return report


def select_coverage(captures: Sequence[Mapping], limit: int) -> list[dict]:
    """Select a deterministic marginal-coverage subset from candidate captures."""
    if isinstance(limit, bool) or not isinstance(limit, int) or limit < 1:
        raise CoverageError("selection limit must be a positive integer")
    remaining = list(captures)
    selected = []
    covered: set[str] = set()
    while remaining and len(selected) < limit:
        choices = []
        for capture in remaining:
            features = set(capture.get("features", ()))
            choices.append((
                len(features - covered),
                str(capture.get("source_sha256", "")),
                str(capture.get("capture_sha256", "")),
                capture,
            ))
        best_gain = max(item[0] for item in choices)
        if best_gain <= 0 and selected:
            break
        chosen = sorted(
            (item for item in choices if item[0] == best_gain),
            key=lambda item: (item[1], item[2]),
        )[0][3]
        selected.append(dict(chosen, marginal_features=sorted(
            set(chosen.get("features", ())) - covered)))
        covered.update(chosen.get("features", ()))
        remaining = [item for item in remaining if item is not chosen]
    return selected


def _check_identity_overlap(left: Sequence[Mapping], right: Sequence[Mapping]) -> None:
    left_hashes = {value for capture in left for value in capture["source_hashes"]}
    right_hashes = {value for capture in right for value in capture["source_hashes"]}
    capture_hashes = {capture["capture_sha256"] for capture in left}
    overlap = left_hashes & right_hashes
    duplicate_capture = capture_hashes & {capture["capture_sha256"] for capture in right}
    if overlap or duplicate_capture:
        details = sorted(overlap | duplicate_capture)
        raise CoverageError(
            "candidate and held-out captures share source identity hash(es): "
            + ", ".join(details))


def build_report(
    candidates: Sequence[str | Path],
    *,
    input_plans: Sequence[str | Path],
    cpu: str,
    held_out: Sequence[str | Path] = (),
    held_out_input_plans: Sequence[str | Path] = (),
    select_limit: int = 1,
    source_root: str | Path = DEFAULT_SOURCE_ROOT,
) -> dict:
    """Analyze input-bound candidates, select them, then evaluate held-out games.

    Every capture must be paired with its complete input plan.  The plan's
    source SHA is the workload identity; capture and plan hashes are retained
    separately for provenance and overlap checks.
    """
    if not candidates:
        raise CoverageError("at least one candidate capture is required")
    if len(candidates) != len(input_plans):
        raise CoverageError("each candidate capture requires one input plan")
    if len(held_out) != len(held_out_input_plans):
        raise CoverageError("each held-out capture requires one input plan")
    mapping = load_source_mapping(source_root)
    candidate_captures = [
        analyze_capture(path, input_plan=plan, cpu=cpu, source_root=source_root,
                        mapping=mapping)
        for path, plan in zip(candidates, input_plans)
    ]
    # Held-out games are intentionally loaded only after candidate selection;
    # their observations cannot influence marginal scoring or tie-breaking.
    selected = select_coverage(candidate_captures, select_limit)
    held_out_captures = [
        analyze_capture(path, input_plan=plan, cpu=cpu, source_root=source_root,
                        mapping=mapping)
        for path, plan in zip(held_out, held_out_input_plans)
    ]
    _check_identity_overlap(candidate_captures, held_out_captures)
    selected_features = set().union(*(set(item["features"]) for item in selected))
    held_out_report = []
    for capture in held_out_captures:
        features = set(capture["features"])
        held_out_report.append({
            "path": capture["path"],
            "capture_sha256": capture["capture_sha256"],
            "input_plan_sha256": capture["input_plan_sha256"],
            "source_sha256": capture["source_sha256"],
            "source_hashes": capture["source_hashes"],
            "source_stage": capture["source_stage"],
            "source_characters": capture["source_characters"],
            "input_policy": capture["input_policy"],
            "provenance": capture["provenance"],
            "frames": capture["frames"],
            "features": capture["features"],
            "features_covered_by_selected": sorted(features & selected_features),
            "features_uncovered_by_selected": sorted(features - selected_features),
        })
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "gold_admitted": False,
        "cpu": cpu,
        "source_mapping": {
            "source_sha256": mapping["source_sha256"],
            "source_files": mapping["source_files"],
        },
        "unsupported_fields": [
            "donor post-frame observations are not used as vanilla expected state",
            "port equivalence, rendering pixels, emitted PCM, physical input and browser timing",
            "items, articles, effects, RNG and internal source fields not present in capture rows",
            "unmapped motion IDs are rejected rather than assigned guessed families",
        ],
        "candidate_captures": candidate_captures,
        "selected": selected,
        "held_out": held_out_report,
        "selected_feature_count": len(selected_features),
        "selection_limit": select_limit,
        "scope": (
            "Measured fields from the supplied vanilla retail trajectory only; "
            "selection never uses held-out coverage. Special family labels carry "
            "direction only; physical posture comes from ground_air fields and "
            "transitions."
        ),
    }
