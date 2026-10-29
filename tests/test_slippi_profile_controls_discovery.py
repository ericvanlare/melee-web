# SPDX-License-Identifier: MIT
"""Expose isolated GPL profile controls to the repository's flat test discovery."""

from src.slippi_profile.tests import test_build_contract as _build_contract
from src.slippi_profile.tests import test_ci_discovery as _ci_discovery
from src.slippi_profile.tests import test_deadupfall_reduction as _deadupfall_reduction
from src.slippi_profile.tests import test_dbooc_binary32_oracle as _dbooc_oracle
from src.slippi_profile.tests import test_dbooc_scalar_helper as _dbooc_scalar
from src.slippi_profile.tests import test_effect_matrix as _effect_matrix
from src.slippi_profile.tests import test_fighter_projection as _fighter_projection
from src.slippi_profile.tests import test_offscreen_timer_block as _offscreen_timer
from src.slippi_profile.tests import test_profile_controls as _profile_controls
from src.slippi_profile.tests import test_runner_contract as _runner_contract
from src.slippi_profile.tests import test_source_composition as _source_composition
from src.slippi_profile.tests import test_stage_render_reductions as _stage_render_reductions
from src.slippi_profile.tests import test_ucf_shield_drop as _ucf_shield_drop


class SlippiBuildContractTests(_build_contract.SlippiSynctestBuildContractTests):
    pass


class SlippiDiscoveryContractTests(_ci_discovery.SlippiDiscoveryContractTests):
    pass


class SlippiProfileControlsTests(_profile_controls.ProfileControlsTests):
    pass


class SlippiOffscreenTimerTests(_offscreen_timer.OffscreenTimerBlockTests):
    pass


class SlippiDeadUpFallReductionTests(_deadupfall_reduction.DeadUpFallReductionTests):
    pass


class SlippiRunnerContractTests(_runner_contract.SlippiSynctestRunnerContractTests):
    pass


class SlippiSourceCompositionTests(_source_composition.SlippiSourceCompositionTests):
    pass


class SlippiStageRenderReductionTests(_stage_render_reductions.StageRenderReductionTests):
    pass


class SlippiDboocOracleTests(_dbooc_oracle.DboocBinary32OracleTests):
    pass


class SlippiDboocScalarTests(_dbooc_scalar.DboocScalarHelperTests):
    pass


class SlippiUcfShieldDropTests(_ucf_shield_drop.UcfShieldDropTests):
    pass


class SlippiFighterProjectionTests(_fighter_projection.FighterProjectionTests):
    pass


class SlippiEffectMatrixTests(_effect_matrix.EffectMatrixTests):
    pass
