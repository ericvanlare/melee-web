"""Fresh full original audio startup with synthetic boot/SRAM, source tables."""
import json
from pathlib import Path
import tempfile
import unittest
from tools import source_synth_joined_runtime as runtime
from tools import source_synth_joined_startup as startup
from tools import source_lbaudio_startup as lbaudio
from tools import source_synth_parameters as parameters
from tools import source_post_audio_allocations as post_audio
import test_source_synth_joined_runtime as synth_tests

ROOT = Path(__file__).resolve().parents[1]

class OriginalAudioStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        required = [startup.EMXX, ROOT/'.venv/bin/cmake', ROOT/'.venv/bin/ninja',
                    ROOT/'build/browser/CMakeCache.txt',
                    ROOT/'build/browser/_deps/fmt-src/include/fmt/base.h']
        if not all(path.is_file() for path in required):
            raise unittest.SkipTest('configured pinned source/CMake dependencies unavailable')
        (ROOT/'work').mkdir(exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix='source-lbaudio-runtime-test-', dir=ROOT/'work'))
        startup.build_profile(cls.work/'profile')
        lbaudio.build_profile(cls.work/'lbaudio')
        post_audio.compile_profile(cls.work/'post-audio')
        signed, offsets = parameters._source_tables(lbaudio.STATIC_HEADER)
        fixture = parameters.render_fixture(lbaudio.SOURCE, {'s32_table':signed, 'offsets_table':offsets})
        result = parameters.compile_and_run(fixture, root=ROOT, work_dir=cls.work/'parameters')
        inputs = synth_tests.synthetic_inputs(cls.work/'inputs')
        receipt = json.loads(inputs['parameters_path'].read_text())
        receipt['result'] = result
        inputs['parameters_path'].write_text(json.dumps(receipt)+'\n')
        cls.expected = result
        cls.output = cls.work/'runtime'
        cls.receipt = runtime.build_runtime(
            cls.work/'profile/receipt.json', artifact_dir=cls.output,
            lbaudio_profile=cls.work/'lbaudio/receipt.json',
            post_audio_profile=cls.work/'post-audio/receipt.json', run=True,
            inputs=inputs, synthetic_inputs=True)

    def test_original_startup_owns_driver_effects_and_banks(self):
        rows = [json.loads(line) for line in (self.output/'run.stdout').read_text().splitlines()
                if line.startswith('{')]
        row = next(item for item in rows if item.get('kind') == 'source_lbaudio_startup')
        self.assertEqual(row['driver_calls'], 1)
        self.assertEqual(row['fx_bytes'], [53*1024,71*1024])
        self.assertEqual(row['bank_sizes'], self.expected['bank_sizes'])
        self.assertEqual(row['bookkeeping_entries'], 4*56)
        self.assertFalse(row['runtime_claim'])
        self.assertTrue(self.receipt['input_plan']['synthetic'])

    def test_post_audio_memory_preserves_derived_bounds(self):
        rows = [json.loads(line) for line in (self.output/'run.stdout').read_text().splitlines()
                if line.startswith('{')]
        audio = next(row for row in rows if row.get('kind') == 'source_synth_allocations')
        memory = next(row for row in rows if row.get('kind') == 'source_post_audio_startup')
        self.assertEqual(memory['aram_stack'], audio['aram_next'])
        self.assertEqual(memory['aram_free_blocks'], audio['aram_free_blocks'])
        self.assertEqual(memory['root_handle_words'], [0, memory['aram_stack'], 16*1024*1024, 0])
        self.assertEqual(len(memory['game_heap_words']), 46)
        self.assertEqual(memory['game_heap_words'][2:4], memory['root_handle_words'][1:3])
        self.assertEqual(memory['free_heap_index'], 1)
        self.assertFalse(memory['runtime_claim'])

    def test_fresh_binary_rejected_all_declared_boundaries(self):
        cases = self.receipt['negative_results']['runs']
        expected = runtime.EXPECTED_NEGATIVE_DIAGNOSTICS | runtime.LBAUDIO_NEGATIVE_DIAGNOSTICS
        self.assertEqual({row['mode'] for row in cases}, set(expected))
        for row in cases:
            self.assertEqual(row['exit'], 1)
            self.assertIn(expected[row['mode']], row['diagnostic'])

    def test_independent_bank_total_mismatch_is_rejected(self):
        command = json.loads((self.output/'run.command.json').read_text())
        command[6] = str(int(command[6])+32)
        result = runtime._run(command, cwd=ROOT, work=self.output, label='bank-total-mismatch', timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('original lbAudio driver call differs', result.stderr)
