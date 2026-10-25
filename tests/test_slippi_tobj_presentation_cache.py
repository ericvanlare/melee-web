# SPDX-License-Identifier: MIT
"""Expose the isolated GPL TObj controls to flat discovery."""
from src.slippi_profile.tests import test_tobj_presentation_cache as _tobj_cache
class SlippiTObjPresentationCacheTests(_tobj_cache.TObjPresentationCacheTests):
    pass
