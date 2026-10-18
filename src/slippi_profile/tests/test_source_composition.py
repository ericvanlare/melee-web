# SPDX-License-Identifier: GPL-3.0-or-later
"""Controls for isolated source-overlay composition and ordinary-path safety."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
import gameplay_sources  # noqa: E402


def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root, text=True)


class SlippiSourceCompositionTests(unittest.TestCase):
    def test_profile_overlay_applies_after_bool_base_patch(self):
        with tempfile.TemporaryDirectory(prefix="slippi-overlay-control-") as temporary:
            repo = Path(temporary) / "repo"
            repo.mkdir()
            git(repo, "init", "-q")
            git(repo, "config", "user.email", "synctest@example.invalid")
            git(repo, "config", "user.name", "Synctest")
            source = repo / "src/melee/ft/fighter.c"
            source.parent.mkdir(parents=True)
            source.write_text("int draw_site(void) { return ifMagnify_802FC998(0); }\n")
            git(repo, "add", "src/melee/ft/fighter.c")
            git(repo, "commit", "-qm", "fixture")

            source.write_text("int draw_site(void) { return melee_source_bool(ifMagnify_802FC998(0)); }\n")
            base_patch = Path(temporary) / "bool.patch"
            base_patch.write_text(git(repo, "diff", "--", "src/melee/ft/fighter.c"))
            git(repo, "add", "src/melee/ft/fighter.c")
            source.write_text(
                "int draw_site(void) {\n"
                "#ifdef MELEE_WEB_SLIPPI_PROFILE\n"
                "    SlippiOffscreenDamageInput input = {0};\n"
                "    return melee_source_bool(slippi_profile_offscreen_damage_zone(&input));\n"
                "#else\n"
                "    return melee_source_bool(ifMagnify_802FC998(0));\n"
                "#endif\n"
                "}\n"
            )
            profile_patch = Path(temporary) / "profile.patch"
            profile_patch.write_text(git(repo, "diff", "--", "src/melee/ft/fighter.c"))

            composed = gameplay_sources.compose_slippi_profile_patch(
                repo, base_patch, profile_patch)
            patch_text = composed.read_text()
            self.assertIn("melee_source_bool", patch_text)
            self.assertIn("slippi_profile_offscreen_damage_zone", patch_text)
            self.assertIn("SLIPPI_PROFILE", patch_text)
            git(repo, "restore", "--source=HEAD", "--staged", "--worktree",
                "src/melee/ft/fighter.c")
            git(repo, "apply", "--check", str(composed))

    def test_default_prepare_path_never_selects_profile_checkout(self):
        with tempfile.TemporaryDirectory(prefix="slippi-prepare-default-") as temporary:
            root = Path(temporary)
            (root / "build").mkdir()
            (root / "patches").mkdir()
            (root / "patches/melee-gameplay.patch").write_text("base patch")
            generated = root / "generated"
            (generated / ".git").mkdir(parents=True)
            calls = []

            def ensure(_build, name, _identity):
                calls.append(name)
                return generated

            lock = {"repositories": {"melee": {"commit": "fixture-commit"}}}
            with mock.patch.object(gameplay_sources, "read_lock", return_value=lock), \
                 mock.patch.object(gameplay_sources, "verify_sources"), \
                 mock.patch.object(gameplay_sources, "ensure_repository", side_effect=ensure), \
                 mock.patch.object(gameplay_sources, "composed_patch", return_value=root / "patches/melee-gameplay.patch"), \
                 mock.patch.object(gameplay_sources, "compose_slippi_profile_patch") as compose, \
                 mock.patch.object(gameplay_sources, "apply_patch"):
                result = gameplay_sources.prepare_sources(root)

            self.assertEqual(calls, ["gameplay-source"])
            compose.assert_not_called()
            self.assertEqual(result, generated / "src")

    def test_profile_prepare_uses_separate_checkout_and_composed_patch(self):
        with tempfile.TemporaryDirectory(prefix="slippi-prepare-profile-") as temporary:
            root = Path(temporary)
            (root / "build").mkdir()
            (root / "patches").mkdir()
            (root / "patches/melee-gameplay.patch").write_text("base patch")
            profile_patch = root / "src/slippi_profile/patches/melee-profile-hooks.patch"
            profile_patch.parent.mkdir(parents=True)
            profile_patch.write_text("profile patch")
            generated = root / "generated"
            (generated / ".git").mkdir(parents=True)
            calls = []
            composed = root / "composed.patch"
            composed.write_text("composed patch")
            lock = {"repositories": {"melee": {"commit": "fixture-commit"}}}

            def ensure(_build, name, _identity):
                calls.append(name)
                return generated

            with mock.patch.object(gameplay_sources, "read_lock", return_value=lock), \
                 mock.patch.object(gameplay_sources, "verify_sources"), \
                 mock.patch.object(gameplay_sources, "ensure_repository", side_effect=ensure), \
                 mock.patch.object(gameplay_sources, "composed_patch", return_value=root / "patches/melee-gameplay.patch"), \
                 mock.patch.object(gameplay_sources, "compose_slippi_profile_patch", return_value=composed) as compose, \
                 mock.patch.object(gameplay_sources, "apply_patch"):
                result = gameplay_sources.prepare_sources(root, slippi_profile=True)

            self.assertEqual(calls, ["gameplay-source-slippi-profile"])
            compose.assert_called_once_with(generated, root / "patches/melee-gameplay.patch",
                                            profile_patch)
            self.assertEqual(result, generated / "src")


if __name__ == "__main__":
    unittest.main()
