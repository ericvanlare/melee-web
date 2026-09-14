from __future__ import annotations

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).parents[1]
PATCH = ROOT / "reference-capture" / "dolphin" / "patches" / "0001-jitarm64-reference-observer.patch"
SOURCE = ROOT / "reference-capture" / "dolphin" / "source" / "Core" / "PowerPC" / "ReferenceCaptureObserver.cpp"


class ReferenceDolphinObserverTests(unittest.TestCase):
    def test_patch_applies_to_clean_pinned_checkout(self) -> None:
        checkout = ROOT / ".deps" / "reference-dolphin"
        if not checkout.exists():
            self.skipTest("Optional pinned Dolphin source checkout is not installed")
        result = subprocess.run(
            ["git", "-C", str(checkout), "apply", "--check", str(PATCH)],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_observer_is_read_only_and_source_identity_is_internal(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("ValidateDiscDOL", source)
        self.assertIn("mbedtls_sha256_ret", source)
        self.assertIn("!status.Flush()", source)
        self.assertIn("File::OpenMode::Create", source)
        self.assertNotIn("Write_U", source)
        self.assertNotIn("WriteToEmu", source)

    def test_host_save_writes_are_guarded_by_session_flag(self) -> None:
        patch = PATCH.read_text(encoding="utf-8")
        self.assertIn("GCMemcardDirectory::FlushToFile", patch)
        self.assertIn("if (!Config::Get(Config::SESSION_SAVE_DATA_WRITABLE))", patch)
        self.assertIn("ExpansionInterfaceManager::Shutdown", patch)

    def test_boundary_blocks_retain_complete_guest_register_state(self) -> None:
        patch = PATCH.read_text(encoding="utf-8")
        self.assertIn("const bool observer_enabled", patch)
        self.assertIn("bool observer_block", patch)
        self.assertIn("!bJITRegisterCacheOff && !observer_block", patch)

    def test_pad_poll_contains_scene_routing_context(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        pad_poll = source[source.index("if (boundary == Boundary::PadPoll)") :
                           source.index("else if (boundary == Boundary::PadConsume)")]
        self.assertIn("SliceTag::SceneRouting, 0x80479d30, 6", pad_poll)

    def test_entry_arms_only_original_vs_setups(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        entry = source[source.index("if (boundary == Boundary::Entry)") :
                       source.index("else\n      {\n        if (!match_active", source.index("if (boundary == Boundary::Entry)"))]
        self.assertIn("match_active = (setup[4] & 0x40) != 0", entry)
        self.assertIn("if (match_active)", entry)
        self.assertIn("!match_active || !setup_pointer", source)


if __name__ == "__main__":
    unittest.main()
