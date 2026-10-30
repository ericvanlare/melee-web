# SPDX-License-Identifier: MIT
"""Expose the isolated GPL particle controls with a flat CI test identity."""
from src.slippi_profile.tests import test_particle_presentation as _controls


class ParticlePresentationTests(_controls.ParticlePresentationTests):
    pass

__all__ = ['ParticlePresentationTests']
