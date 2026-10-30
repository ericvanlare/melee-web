# SPDX-License-Identifier: MIT
"""Expose the isolated GPL planner controls with a flat CI test identity."""
from src.slippi_profile.tests import test_particle_pass_plan as _controls


class ParticlePassPlanTests(_controls.ParticlePassPlanTests):
    pass

__all__ = ["ParticlePassPlanTests"]
