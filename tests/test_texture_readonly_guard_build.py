"""Routing/configure controls only; mocks never claim a CMake/GPU build."""
from unittest.mock import patch
import unittest
import test_match_draw_discard_build as fixture

class TextureReadonlyGuardBuildTests(unittest.TestCase):
    def configured_root(self):
        helper=fixture.MatchDrawDiscardBuildTests()
        self.addCleanup(helper.doCleanups)
        return helper._configured_root()

    def calls(self,root,target,**kwargs):
        build=fixture.BUILD
        with patch.object(build,'read_lock',return_value={'repositories':{},'emscripten':'6.0.9'}), \
             patch.object(build,'verify_sources'), \
             patch.object(build,'prepare_sources',return_value=root/'build/gameplay-source/src'), \
             patch.object(build.subprocess,'run') as run:
            build.build(3,root=root,target=target,configuration='Release',**kwargs)
        return [c.args[0] for c in run.call_args_list]

    def test_explicit_isolated_release_route_and_flags(self):
        root=self.configured_root();build=fixture.BUILD
        commands=self.calls(root,build.TEXTURE_READONLY_GUARD_TARGET,configure_only=True)
        configure=next(c for c in commands if '-S' in c)
        self.assertEqual(configure[configure.index('-B')+1],str(root/'build/browser-texture-readonly-guard-release'))
        self.assertIn('-DAURORA_BROWSER_TEXTURE_READONLY_GUARD=ON',configure)
        self.assertIn('-DMELEE_WEB_MATCH_FRAME_EFFECT_DIAGNOSTIC=ON',configure)
        self.assertIn('-DMELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC=OFF',configure)
        self.assertIn('-DMELEE_WEB_AURORA_FUTURE_OWNER_DIAGNOSTIC=OFF',configure)
        self.assertFalse(any('--build' in c for c in commands))

    def test_build_selects_only_component_with_requested_jobs(self):
        root=self.configured_root();build=fixture.BUILD
        commands=self.calls(root,build.TEXTURE_READONLY_GUARD_TARGET)
        command=next(c for c in commands if '--build' in c)
        self.assertEqual(command[command.index('--target')+1:command.index('-j')],['aurora_texture_readonly_guard_probe'])
        self.assertEqual(command[-2:],['-j','3'])

    def test_other_profiles_explicitly_disable_guard_and_keep_directories(self):
        root=self.configured_root();build=fixture.BUILD
        cases=[('runtime',{},'browser-release'),
               (build.MATCH_FRAME_EFFECT_DIAGNOSTIC_TARGET,{},'browser-match-frame-effect-release'),
               (build.MATCH_FRAME_EFFECT_DIAGNOSTIC_TARGET,{'first_pair_audio_file_diagnostic':True},'browser-first-pair-audio-file-release')]
        for target,extras,directory in cases:
            with self.subTest(directory=directory):
                configure=next(c for c in self.calls(root,target,configure_only=True,**extras) if '-S' in c)
                self.assertIn('-DAURORA_BROWSER_TEXTURE_READONLY_GUARD=OFF',configure)
                self.assertEqual(configure[configure.index('-B')+1],str(root/'build'/directory))

    def test_invalid_profiles_refuse_before_preparation(self):
        build=fixture.BUILD
        for options in [{'configuration':'RelWithDebInfo'},{'pipeline_provenance':True},
                        {'selective_pipelines':True},{'stadium_c1a_diagnostic':True},
                        {'fma_collision_diagnostic':True},{'fma_velocity_y_diagnostic':True},
                        {'first_pair_audio_file_diagnostic':True},{'trace_targets':['aurora_future_owner_probe']}]:
            with self.subTest(options=options),patch.object(build,'prepare_sources') as prepare:
                with self.assertRaises(ValueError):
                    build.build(3,target=build.TEXTURE_READONLY_GUARD_TARGET,**{'configuration':'Release',**options})
                prepare.assert_not_called()

if __name__=='__main__':unittest.main()
