# SPDX-License-Identifier: MIT
"""Expose the isolated profile controls to the repository's normal discovery."""

from src.slippi_profile.tests.test_profile_controls import ProfileControlsTests
from src.slippi_profile.tests.test_build_contract import SlippiSynctestBuildContractTests
from src.slippi_profile.tests.test_source_composition import SlippiSourceCompositionTests
from src.slippi_profile.tests.test_runner_contract import SlippiSynctestRunnerContractTests


__all__ = ["ProfileControlsTests", "SlippiSynctestBuildContractTests",
           "SlippiSourceCompositionTests", "SlippiSynctestRunnerContractTests"]
