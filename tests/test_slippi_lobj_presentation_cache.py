# SPDX-License-Identifier: MIT
"""Expose the isolated GPL LObj presentation-cache controls to flat discovery."""

from src.slippi_profile.tests import test_lobj_presentation_cache as _lobj_cache


class SlippiLObjPresentationCacheTests(_lobj_cache.LObjPresentationCacheTests):
    pass


class SlippiLObjCurrentSetV15Tests(_lobj_cache.LObjCurrentSetV15Tests):
    pass
