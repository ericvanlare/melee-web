from __future__ import annotations

import copy
import hashlib
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [
    str(ROOT / "tools"),
    str(ROOT / "scripts"),
    str(ROOT / "reference-capture" / "dolphin"),
]

from authored_sd_reference_plan import canonical  # noqa: E402
from sd_original_menu_plan import (  # noqa: E402
    STADIUM_FIRST_FORM_MENU_WAIT_PROFILE,
    stadium_first_form_menu_wait_72_packet,
    stadium_first_form_menu_wait_receipt,
    stadium_go_feedback_packet,
    validate_packet,
)


class StadiumFirstFormMenuWaitVariantTests(unittest.TestCase):
    def test_named_variant_changes_only_css_wait_and_binds_selector_and_receipt(self):
        original = stadium_go_feedback_packet()
        variant = stadium_first_form_menu_wait_72_packet()

        self.assertEqual(original["css"]["idle_polls_before_start"], 12)
        self.assertEqual(variant["css"]["idle_polls_before_start"], 72)
        expected = copy.deepcopy(original)
        expected["css"]["idle_polls_before_start"] = 72
        expected["first_form_menu_wait_variant"] = STADIUM_FIRST_FORM_MENU_WAIT_PROFILE
        self.assertEqual(variant, expected)
        self.assertEqual(validate_packet(copy.deepcopy(variant)), variant)
        self.assertEqual(
            hashlib.sha256(canonical(original)).hexdigest(),
            "5f4dd95fb2e00cd35aa9fa42de10758acc4998624c964a1a077d683ebebe95f8",
        )

        receipt = stadium_first_form_menu_wait_receipt(
            variant, selected=True, menu_recipe_sha256="a" * 64
        )
        self.assertEqual(receipt, {
            "profile": STADIUM_FIRST_FORM_MENU_WAIT_PROFILE,
            "selector": "--stadium-first-form-menu-wait-72",
            "base_idle_polls": 12,
            "css_idle_polls_before_start": 72,
            "declared_css_visits": 3,
            "menu_recipe_sha256": "a" * 64,
        })
        self.assertIsNone(stadium_first_form_menu_wait_receipt(
            original, selected=False, menu_recipe_sha256="b" * 64
        ))

    def test_selector_and_forged_or_changed_packets_refuse(self):
        original = stadium_go_feedback_packet()
        variant = stadium_first_form_menu_wait_72_packet()
        with self.assertRaises(ValueError):
            stadium_first_form_menu_wait_receipt(original, selected=True,
                                                  menu_recipe_sha256="a" * 64)
        with self.assertRaises(ValueError):
            stadium_first_form_menu_wait_receipt(variant, selected=False,
                                                  menu_recipe_sha256="a" * 64)
        with self.assertRaises(ValueError):
            stadium_first_form_menu_wait_receipt(variant, selected=1,
                                                  menu_recipe_sha256="a" * 64)
        with self.assertRaises(ValueError):
            stadium_first_form_menu_wait_receipt(variant, selected=True,
                                                  menu_recipe_sha256="not-a-sha")

        for mutate in (
            lambda value: value["css"].__setitem__("idle_polls_before_start", 71),
            lambda value: value["css"].__setitem__("idle_polls_before_start", 73),
            lambda value: value["sss"].__setitem__("max_movement_callbacks", 55),
            lambda value: value.__setitem__("first_form_menu_wait_variant", "forged"),
            lambda value: value.pop("first_form_menu_wait_variant"),
        ):
            changed = copy.deepcopy(variant)
            mutate(changed)
            with self.subTest(changed=changed.get("first_form_menu_wait_variant")):
                with self.assertRaises(ValueError):
                    validate_packet(changed)


if __name__ == "__main__":
    unittest.main()
