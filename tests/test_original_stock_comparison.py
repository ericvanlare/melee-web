import copy
import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location(
    "stock_compare", Path(__file__).resolve().parents[1] / "tools/compare_stock_trace.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class OriginalStockComparison(unittest.TestCase):
    def fixture(self, frame_count=module.FRAME_COUNT):
        reference = []
        port = []
        for index in range(frame_count):
            stocks = 3 if index >= 111 else 4
            original_fighters = []
            current_players = []
            for slot, x in enumerate((-60.0, 60.0)):
                state = {
                    "slot": slot,
                    "motion": 14,
                    "ground_air": 0,
                    "stocks": stocks,
                    "position": [{"bits": "00000000"} for _ in range(3)],
                    "velocity": [{"bits": "00000000"} for _ in range(3)],
                    "damage": {"bits": "00000000"},
                }
                state["position"][0] = {"bits": "c2700000" if slot == 0 else "42700000"}
                original_fighters.append(copy.deepcopy(state))
                current_players.append(copy.deepcopy(state))
            reference.append({"game_frame": 12 + index, "fighters": original_fighters})
            port.append({"tick": index, "players": current_players})
        return reference, port

    def test_stock_jab_recipe_accepts_754_samples_and_reports_late_mismatch(self):
        reference, port = self.fixture(module.STOCK_JAB_FRAME_COUNT)
        result = module.compare(reference, port, recipe="stock-jab")
        self.assertEqual(result["frames"], 754)
        self.assertEqual(result["matched"], 754)
        self.assertIn("grounded jab", result["scope"])
        port[700]["players"][1]["motion"] = 44
        result = module.compare(reference, port, recipe="stock-jab")
        self.assertEqual(result["differences"][0]["sample"], 700)
        self.assertIn("motion", result["differences"][0]["fighters"][0]["fields"])

    def test_default_recipe_rejects_stock_jab_length(self):
        reference, port = self.fixture(module.STOCK_JAB_FRAME_COUNT)
        with self.assertRaisesRegex(ValueError, "exactly 541"):
            module.compare(reference, port)

    def test_exact_trace_reports_scope_and_exclusions(self):
        reference, port = self.fixture()
        result = module.compare(reference, port)
        self.assertEqual(result["matched"], module.FRAME_COUNT)
        self.assertIn("animation frame / idle phase", result["excluded"][0])
        self.assertIn("RNG", result["excluded"][1])
        self.assertIn("not a full-match equivalence claim", result["scope"])

    def test_missing_sample_is_rejected(self):
        reference, port = self.fixture()
        with self.assertRaisesRegex(ValueError, "exactly 541"):
            module.compare(reference[:-1], port)

    def test_vector_bit_mismatch_is_reported(self):
        reference, port = self.fixture()
        port[20]["players"][1]["velocity"][2]["bits"] = "3f800000"
        result = module.compare(reference, port)
        self.assertEqual(result["differences"][0]["sample"], 20)
        self.assertIn("velocity bits", result["differences"][0]["fighters"][0]["fields"])

    def test_motion_mismatch_is_reported(self):
        reference, port = self.fixture()
        port[12]["players"][1]["motion"] = 21
        result = module.compare(reference, port)
        self.assertEqual(result["differences"][0]["sample"], 12)
        self.assertIn("motion", result["differences"][0]["fighters"][0]["fields"])

    def test_stock_mismatch_is_reported(self):
        reference, port = self.fixture()
        port[111]["players"][1]["stocks"] = 4
        result = module.compare(reference, port)
        self.assertEqual(result["differences"][0]["sample"], 111)
        self.assertIn("stocks", result["differences"][0]["fighters"][0]["fields"])

    def test_missing_scalar_on_both_sides_is_rejected(self):
        reference, port = self.fixture()
        del reference[3]["fighters"][0]["motion"]
        del port[3]["players"][0]["motion"]
        with self.assertRaisesRegex(ValueError, "missing a valid motion"):
            module.compare(reference, port)

    def test_missing_damage_bits_on_both_sides_is_rejected(self):
        reference, port = self.fixture()
        del reference[3]["fighters"][0]["damage"]["bits"]
        del port[3]["players"][0]["damage"]["bits"]
        with self.assertRaisesRegex(ValueError, "missing damage bits"):
            module.compare(reference, port)

    def test_null_fighter_and_nonfinite_bits_are_rejected(self):
        reference, port = self.fixture()
        port[3]["players"][1] = None
        with self.assertRaisesRegex(ValueError, "null or invalid"):
            module.compare(reference, port)
        reference, port = self.fixture()
        port[3]["players"][1]["velocity"][0]["bits"] = "7f800000"
        with self.assertRaisesRegex(ValueError, "non-finite"):
            module.compare(reference, port)


if __name__ == "__main__":
    unittest.main()
