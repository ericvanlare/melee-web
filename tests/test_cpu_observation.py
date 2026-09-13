"""Exercise the native-shaped CPU observation sidecar with fixed source bytes."""
from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from cpu_observation_validation import (CaptureError, _compare, compare_observations,
                                        domain_divergences, load_observation)
from retail_cpu_observation import CAMERA, HUD, MAGNIFY, MATCH, CpuObservation


class MemoryFixture:
    """Sparse big-endian GALE01r2 memory for one human and one CPU."""

    def __init__(self):
        self.data = {}

    def write(self, address, data):
        for offset, value in enumerate(data):
            self.data[address + offset] = value

    def read(self, address, size):
        return bytes(self.data.get(address + offset, 0) for offset in range(size))


def u32(value):
    return struct.pack(">I", value)


def i16(value):
    return struct.pack(">h", value)


def i32(value):
    return struct.pack(">i", value)


def f32(value):
    return struct.pack(">f", value)


def vec(*values):
    return b"".join(f32(value) for value in values)


def setup_bytes():
    setup = bytearray(0x138)
    setup[0x61] = 0  # Human
    setup[0x85] = 1  # CPU
    for slot in range(2, 6):
        setup[0x61 + slot * 0x24] = 3  # Gm_PKind_NA
    return bytes(setup)


def make_fixture():
    memory = MemoryFixture()
    fighter0, fighter1 = 0x91000000, 0x92000000
    subject0, subject1 = 0x93000000, 0x94000000
    camera_gobj, camera_cobj = 0x90000000, 0x90000100

    camera = bytearray(0x4C)
    camera[0x14:0x20] = vec(4.0, 5.0, 6.0)
    camera[0x2C:0x38] = vec(1.0, -2.0, 3.5)
    camera[0:4] = u32(camera_gobj)
    memory.write(CAMERA, camera)
    memory.write(camera_gobj + 0x28, u32(camera_cobj))
    projection = bytearray(0x51)
    projection[0x38:0x3C] = f32(10.0)
    projection[0x3C:0x40] = f32(1000.0)
    projection[0x40:0x44] = f32(45.0)
    projection[0x50] = 1
    memory.write(camera_cobj, projection)

    match = bytearray(0x2E)
    match[0:1] = b"\x00"
    match[8:9] = b"\x00"
    match[0x24:0x28] = u32(123)
    match[0x28:0x2C] = u32(17)
    match[0x2C:0x2E] = struct.pack(">H", 9)
    memory.write(MATCH, match)

    for fighter, subject, state in ((fighter0, subject0, 7), (fighter1, subject1, 11)):
        memory.write(fighter + 0x890, u32(subject))
        subject_raw = bytearray(0x28)
        subject_raw[8:12] = i32(state)
        subject_raw[0xC] = 0xE0  # on_ledge, force_inactive, was_framed
        subject_raw[0xE:0x10] = i16(-3 if fighter == fighter0 else 4)
        subject_raw[0x10:0x1C] = vec(10.0, 11.0, 12.0)
        subject_raw[0x1C:0x28] = vec(-1.0, -2.0, -3.0)
        memory.write(subject, subject_raw)

    for slot, fighter in enumerate((fighter0, fighter1)):
        hud = bytearray(0x11)
        hud[0:4] = u32(0xA0000000 + slot)
        hud[0xA:0xC] = i16(42 + slot)
        hud[0xC:0xE] = i16(39 + slot)
        hud[0xE] = 12 + slot
        hud[0xF] = 6 + slot
        hud[0x10] = 0xEC  # explode/randomize/force, hide, animation status 2
        memory.write(HUD + slot * 0x64, hud)
        memory.write(MAGNIFY + 0x14 + slot * 0x10 + 0xC,
                     bytes((0xA5 if slot == 0 else 0x42,)))

    cpu = bytearray(0x57C)
    cpu[0:4] = u32(0xAABBCCDD)
    cpu[4:8] = struct.pack("bbbb", 127, -128, 12, -13)
    cpu[8:10] = bytes((49, 255))
    cpu[0xC:0x10] = i32(4)
    cpu[0x10:0x14] = i32(9)
    cpu[0x18:0x1C] = i32(-12)
    cpu[0x1C:0x20] = i32(13)
    cpu[0x20:0x24] = i32(-14)
    cpu[0x44:0x48] = u32(fighter0)
    cpu[0xA8:0xB0] = i32(101) + i32(-202)
    cpu[0xC8] = 2
    cpu[0xCC:0xD0] = i32(303)
    cpu[0xEC] = 1
    cpu_address = fighter1 + 0x1A88
    buffer_address = cpu_address + 0x454
    cpu[0x450:0x454] = u32(buffer_address + 2)
    cpu[0x454:0x459] = bytes.fromhex("aabbccddee")
    cpu[0x44C:0x450] = u32(77)
    cpu[0x554:0x558] = u32(buffer_address + 5)
    memory.write(cpu_address, cpu)
    return memory, {0: fighter0, 1: fighter1}, setup_bytes()


def expected_snapshot():
    subject = {
        "state": 7,
        "timer": -3,
        "on_ledge": 1,
        "force_inactive": 1,
        "was_framed": 1,
        "position_bits": ["41200000", "41300000", "41400000"],
        "bone_bits": ["bf800000", "c0000000", "c0400000"],
    }
    hud0 = {
        "present": True,
        "damage": 42,
        "old_damage": 39,
        "last_attack_damage": 12,
        "shake_frames": 6,
        "explode": 1,
        "randomize_velocity": 1,
        "force_shake": 1,
        "hide_digits": 1,
        "animation_status": 2,
    }
    cpu = {
        "kind": 4,
        "level": 9,
        "state": -12,
        "default_state": 13,
        "secondary_state": -14,
        "target_slot": 0,
        "buttons": 0xAABBCCDD,
        "sticks": [127, -128, 12, -13],
        "triggers": [49, 255],
        "command_duration": 77,
        "command_cursor": 2,
        "command_bytes": "aabbccddee",
        "defend_queue": [101, -202],
        "attack_queue": [303],
    }
    return {
        "match": {"frame": 123, "seconds": 17, "subframe": 9,
                  "outcome": 0, "end_state": 0},
        "camera": {"position_bits": ["3f800000", "c0000000", "40600000"],
                   "interest_bits": ["40800000", "40a00000", "40c00000"],
                   "projection": 1, "fov_bits": "42340000",
                   "near_bits": "41200000", "far_bits": "447a0000"},
        "players": [
            {"slot": 0, "cpu": None, "subject": subject,
             "magnifier": {"offscreen": 1, "ignore_offscreen": 0, "edge": 37},
             "hud": hud0},
            {"slot": 1, "cpu": cpu,
             "subject": {**subject, "state": 11, "timer": 4},
             "magnifier": {"offscreen": 0, "ignore_offscreen": 1, "edge": 2},
             "hud": {**hud0, "damage": 43, "old_damage": 40,
                     "last_attack_damage": 13, "shake_frames": 7}},
        ],
    }


class CpuObservationTests(unittest.TestCase):
    def capture(self):
        memory, fighters, setup = make_fixture()
        with tempfile.TemporaryDirectory(prefix="cpu-observation-") as directory:
            path = Path(directory) / "observation.jsonl"
            observation = CpuObservation(path, memory.read)
            observation.begin(setup.hex(), 1, fighters)
            observation.tick(0, fighters)
            observation.draw(0, fighters)
            observation.end(1, [])
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            return rows, path.read_bytes(), memory, fighters, setup

    def capture_timeline(self, frame_count=3):
        memory, fighters, setup = make_fixture()
        with tempfile.TemporaryDirectory(prefix="cpu-observation-timeline-") as directory:
            path = Path(directory) / "observation.jsonl"
            observation = CpuObservation(path, memory.read)
            observation.begin(setup.hex(), frame_count, fighters)
            for index in range(frame_count):
                observation.tick(index, fighters)
                observation.draw(index, fighters)
            observation.end(frame_count, [])
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            return rows

    @staticmethod
    def write_rows(path, rows):
        path.write_text("\n".join(json.dumps(row) for row in rows) + "\n")

    @staticmethod
    def without_draw(rows, source_index):
        result = []
        draw_index = 0
        for row in rows:
            if row.get("record") == "draw" and row["source_index"] == source_index:
                continue
            row = json.loads(json.dumps(row))
            if row.get("record") == "draw":
                row["index"] = draw_index
                draw_index += 1
            elif row.get("record") == "end":
                row["draws"] = draw_index
            result.append(row)
        return result

    def test_fixed_source_fixture_matches_native_schema(self):
        rows, _, _, _, _ = self.capture()
        self.assertEqual(rows[0]["source_drawing"], True)
        self.assertEqual({key for key in rows[1] if key != "record"}, set(expected_snapshot()))
        self.assertEqual({key for key in rows[1]["players"][1]["cpu"]}, set(expected_snapshot()["players"][1]["cpu"]))
        self.assertEqual({key for key in rows[1]["players"][0]["hud"]}, set(expected_snapshot()["players"][0]["hud"]))
        for key, value in expected_snapshot().items():
            self.assertEqual(rows[1][key], value, key)

    def test_strict_validator_accepts_complete_fixture(self):
        rows, payload, _, _, _ = self.capture()
        with tempfile.TemporaryDirectory(prefix="cpu-observation-validate-") as directory:
            path = Path(directory) / "observation.jsonl"
            path.write_bytes(payload)
            observation = load_observation(path)
            self.assertEqual(len(observation.frames), 1)
            self.assertEqual(len(observation.draws), 1)
            self.assertEqual(observation.end["remaining_fighter_slots"], [])

    def test_early_camera_failure_does_not_hide_later_cpu_failure(self):
        rows, payload, _, _, _ = self.capture()
        with tempfile.TemporaryDirectory(prefix="cpu-domain-divergence-") as directory:
            a, b, port = [Path(directory) / name for name in ('a.jsonl', 'b.jsonl', 'port.jsonl')]
            a.write_bytes(payload)
            b.write_bytes(payload)
            rows[1]['camera']['interest_bits'][0] = '00000000'
            rows[2]['players'][1]['cpu']['buttons'] = 0
            port.write_text('\n'.join(json.dumps(row) for row in rows) + '\n')
            result = compare_observations(a, b, port)
            self.assertEqual(result['status'], 'declared_state_divergence')
            self.assertEqual(result['first_divergence']['phase'], 'initial')
            cpu = result['first_divergences_by_domain']['cpu']
            self.assertEqual((cpu['phase'], cpu['tick'], cpu['field']),
                             ('frame', 0, 'players[1].cpu.buttons'))
            self.assertIsNone(result['first_divergences_by_domain']['hud'])

    def test_draw_count_does_not_hide_earlier_initial_or_frame_failure(self):
        rows = self.capture_timeline()
        with tempfile.TemporaryDirectory(prefix="cpu-draw-count-order-") as directory:
            root = Path(directory)
            expected_path, actual_path = root / "expected.jsonl", root / "actual.jsonl"
            self.write_rows(expected_path, rows)
            for record in ("initial", "frame"):
                changed = json.loads(json.dumps(rows))
                target = next(row for row in changed if row.get("record") == record)
                if record == "initial":
                    target["camera"]["interest_bits"][0] = "00000000"
                else:
                    target["players"][1]["cpu"]["buttons"] ^= 1
                self.write_rows(actual_path, self.without_draw(changed, 0))
                difference = _compare(load_observation(expected_path),
                                      load_observation(actual_path), drawing=True)
                self.assertEqual(difference["phase"], record)
                self.assertNotEqual(difference["field"], "draw_count")

    def test_earliest_missing_and_extra_draw_identify_source_tick(self):
        rows = self.capture_timeline()
        with tempfile.TemporaryDirectory(prefix="cpu-draw-alignment-") as directory:
            root = Path(directory)
            full_path, missing_path = root / "full.jsonl", root / "missing.jsonl"
            self.write_rows(full_path, rows)
            self.write_rows(missing_path, self.without_draw(rows, 1))
            full = load_observation(full_path)
            missing = load_observation(missing_path)

            difference = _compare(full, missing, drawing=True)
            self.assertEqual((difference["phase"], difference["tick"], difference["field"]),
                             ("draw", 1, "draw_alignment.missing_from_actual"))
            self.assertEqual(difference["expected"], {"ordinal": 1, "source_index": 1})
            self.assertIsNone(difference["actual"])

            difference = _compare(missing, full, drawing=True)
            self.assertEqual((difference["phase"], difference["tick"], difference["field"]),
                             ("draw", 1, "draw_alignment.extra_in_actual"))
            self.assertIsNone(difference["expected"])
            self.assertEqual(difference["actual"], {"ordinal": 1, "source_index": 1})

    def test_domain_draws_pair_only_matching_source_ticks(self):
        rows = self.capture_timeline()
        with tempfile.TemporaryDirectory(prefix="cpu-domain-draw-alignment-") as directory:
            root = Path(directory)
            expected_path, actual_path = root / "expected.jsonl", root / "actual.jsonl"
            self.write_rows(expected_path, rows)
            changed = self.without_draw(rows, 0)
            draw = next(row for row in changed
                        if row.get("record") == "draw" and row["source_index"] == 1)
            draw["camera"]["interest_bits"][0] = "00000000"
            self.write_rows(actual_path, changed)
            diagnostics = domain_divergences(load_observation(expected_path),
                                             load_observation(actual_path))
            self.assertEqual((diagnostics["camera"]["phase"], diagnostics["camera"]["tick"]),
                             ("draw", 1))
            self.assertEqual(diagnostics["draw_alignment"]["scope"],
                             "source_draws_by_source_index")
            self.assertEqual(diagnostics["draw_alignment"]["missing_from_actual"],
                             [{"ordinal": 0, "source_index": 0}])
            self.assertEqual(diagnostics["draw_alignment"]["missing_from_actual_count"], 1)
            self.assertEqual(diagnostics["draw_alignment"]["matched_count"], 2)

    def test_native_and_python_reject_non_na_tail(self):
        rows, _, _, fighters, setup = self.capture()
        bad_setup = bytearray(setup)
        bad_setup[0x61 + 4 * 0x24] = 2  # Gm_PKind_Demo in an inactive slot
        with tempfile.TemporaryDirectory(prefix="cpu-observation-tail-") as directory:
            path = Path(directory) / "tail.jsonl"
            changed = [dict(row) for row in rows]
            changed[0] = dict(changed[0])
            changed[0]["setup_hex"] = bad_setup.hex()
            path.write_text("\n".join(json.dumps(row) for row in changed) + "\n")
            with self.assertRaises(CaptureError):
                load_observation(path)

            observation = CpuObservation(Path(directory) / "native-shape.jsonl", lambda address, size: b"\0" * size)
            with self.assertRaises(ValueError):
                observation.begin(bad_setup.hex(), 1, fighters)

    def test_validator_rejects_wrong_role_missing_draw_and_teardown(self):
        rows, _, _, _, _ = self.capture()
        with tempfile.TemporaryDirectory(prefix="cpu-observation-invalid-") as directory:
            root = Path(directory)

            wrong_role = [dict(row) for row in rows]
            wrong_role[1] = dict(wrong_role[1])
            wrong_role[1]["players"] = list(wrong_role[1]["players"])
            wrong_role[1]["players"][0] = dict(wrong_role[1]["players"][0])
            wrong_role[1]["players"][0]["cpu"] = {}
            wrong_path = root / "wrong-role.jsonl"
            wrong_path.write_text("\n".join(json.dumps(row) for row in wrong_role) + "\n")
            with self.assertRaises(CaptureError):
                load_observation(wrong_path)

            missing_draw = rows[:2] + rows[2:3] + rows[-1:]
            missing_path = root / "missing-draw.jsonl"
            missing_path.write_text("\n".join(json.dumps(row) for row in missing_draw) + "\n")
            with self.assertRaises(CaptureError):
                load_observation(missing_path)

            stale_teardown = [dict(row) for row in rows]
            stale_teardown[-1] = dict(stale_teardown[-1])
            stale_teardown[-1]["remaining_fighter_slots"] = [1]
            stale_path = root / "stale-teardown.jsonl"
            stale_path.write_text("\n".join(json.dumps(row) for row in stale_teardown) + "\n")
            with self.assertRaises(CaptureError):
                load_observation(stale_path)

    def test_failed_port_keeps_first_divergence_without_accepting_prefix(self):
        rows, _, _, _, _ = self.capture()
        with tempfile.TemporaryDirectory(prefix="cpu-prefix-") as directory:
            paths = [Path(directory) / name for name in ("a.jsonl", "b.jsonl", "port.jsonl")]
            for path in paths[:2]:
                path.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
            partial = json.loads(json.dumps(rows[:-1]))
            partial[2]["players"][1]["cpu"]["buttons"] ^= 1
            paths[2].write_text("\n".join(json.dumps(row) for row in partial) + "\n")
            result = compare_observations(*paths)
            self.assertEqual(result["status"], "invalid_or_incomplete_port")
            self.assertEqual(result["first_divergence"]["tick"], 0)
            self.assertEqual(result["first_divergence"]["field"], "players[1].cpu.buttons")
            paths[2].write_text("\n".join(json.dumps(row) for row in rows[:-1]) + "\n")
            result = compare_observations(*paths)
            self.assertEqual(result["status"], "invalid_or_incomplete_port")
            self.assertIsNone(result["first_divergence"])

    def test_pointer_bounds_and_first_field_divergence(self):
        rows, _, memory, fighters, _ = self.capture()
        cpu_address = fighters[1] + 0x1A88
        cpu = bytearray(memory.read(cpu_address, 0x57C))
        cpu[0x554:0x558] = u32(cpu_address + 0x454 + 0x101)
        memory.write(cpu_address, cpu)
        observation = CpuObservation(Path("/dev/null"), memory.read)
        observation.types = [0, 1]
        with self.assertRaises(ValueError):
            observation.cpu(cpu_address, fighters)

        with tempfile.TemporaryDirectory(prefix="cpu-observation-diff-") as directory:
            left = Path(directory) / "left.jsonl"
            right = Path(directory) / "right.jsonl"
            left.write_bytes(b"\n".join(json.dumps(row).encode() for row in rows) + b"\n")
            changed = [dict(row) for row in rows]
            changed[1] = dict(changed[1])
            changed[1]["players"] = list(changed[1]["players"])
            changed[1]["players"][1] = dict(changed[1]["players"][1])
            changed[1]["players"][1]["cpu"] = dict(changed[1]["players"][1]["cpu"])
            changed[1]["players"][1]["cpu"]["buttons"] ^= 1
            right.write_bytes(b"\n".join(json.dumps(row).encode() for row in changed) + b"\n")
            a, b = load_observation(left), load_observation(right)
            difference = _compare(a, b, drawing=True)
            self.assertEqual(difference["phase"], "initial")
            self.assertEqual(difference["field"], "players[1].cpu.buttons")


if __name__ == "__main__":
    unittest.main()
