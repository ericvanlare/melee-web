from __future__ import annotations

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
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

    def test_complete_observer_and_input_patch_series_applies_in_order(self) -> None:
        checkout = ROOT / ".deps" / "reference-dolphin"
        if not checkout.exists():
            self.skipTest("Optional pinned Dolphin source checkout is not installed")
        patches = sorted(PATCH.parent.glob("*.patch"))
        with tempfile.TemporaryDirectory() as temporary:
            staged = Path(temporary)
            names = set()
            for patch in patches:
                names.update(re.findall(r"^--- a/(.+)$", patch.read_text(), re.MULTILINE))
            for name in names:
                target = staged / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(checkout / name, target)
            for patch in patches:
                result = subprocess.run(["git", "apply", str(patch)], cwd=staged,
                                        text=True, capture_output=True, check=False)
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

    def test_whole_session_is_opt_in_and_has_pinned_source_boundaries(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("MWRC_WHOLE_SESSION_MATCHES", source)
        self.assertIn("WHOLE_SESSION_MIN_MATCHES = 3", source)
        for address in ("0x8026688c", "0x80266d70", "0x8025a998",
                        "0x8025bb5c", "0x801a5af0", "0x80177368",
                        "0x80177704", "0x801a5f64", "0x80179350",
                        "0x801bfcfc", "0x802febe0", "0x802fed10",
                        "0x801a6308", "0x801bff7c"):
            self.assertIn(address, source)
        self.assertIn("ResultsGObjProcess", source)
        self.assertIn("ReturnCss", source)
        self.assertIn("CssCancelEnter", source)
        self.assertIn("MenuSssRoute", source)
        self.assertIn("ProfileCharacters", source)
        self.assertIn("ProfileStages", source)
        self.assertIn("gmMainLib_804D3EE0", source)
        self.assertIn("completed_match_pending_prize", source)
        self.assertIn("StartupPrizeModeExit", source)
        self.assertIn("startup Prize mode exit", source)
        hook_predicate = source[source.index("bool Observer::IsBoundary"):
                                source.index("void Observer::OnBoundary")]
        self.assertIn("case 0x801BFF7C:", hook_predicate)
        self.assertIn("MWRC_CAPTURE_ID", source)
        self.assertIn("MWRC_SEQUENCE_ID", source)
        self.assertIn("MENU_AUDIO_STREAM_START", source)
        self.assertIn("audio_owner_epoch", source)
        self.assertIn("return word == 0x7c0802a6", source)
        self.assertIn("SliceTag::Result, 0x80479d98 + 0xc, 0x28", source)
        self.assertIn("AddSessionSlices(system)", source)
        self.assertIn("whole_session_enabled() ? WHOLE_SESSION_FLAG : 0", source)
        self.assertIn("PutU16(out, static_cast<u16>(match_index))", source)
        self.assertIn("PutU32(out, audio_owner_epoch)", source)
        self.assertNotIn("Write_U", source)
        self.assertNotIn("WriteToEmu", source)


if __name__ == "__main__":
    unittest.main()
