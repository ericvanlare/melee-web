"""Exact passive-timing replay joins; synthetic rows are not original evidence."""
from pathlib import Path
import copy
import importlib.util
import os
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get('MELEE_WEB_CANDIDATE_SOURCE_ROOT', ROOT))
REFERENCE = Path(os.environ.get('MELEE_WEB_REFERENCE_SOURCE_ROOT', ROOT))
sys.path[:0] = [str(REFERENCE/'tests'), str(REFERENCE/'scripts'), str(REFERENCE/'tools'),
               str(REFERENCE/'reference-capture/dolphin')]
from owned_test_workspace import OwnedWorkspaceTests
import test_stadium_selected_request_timing as timing_fixture
import stadium_selected_request_timing as timing
spec = importlib.util.spec_from_file_location('water_timing_candidate', SOURCE/'scripts/capture_sd_reference_prefix.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def function(source, signature):
    start = source.index(signature)
    pos = source.index('{', start)
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class WaterTimingReplayTests(OwnedWorkspaceTests):
    def fixture(self):
        # Minimal source points and existing fully modeled timing protocol.
        # No reconstructed input, source World or timing equivalence is claimed.
        original = [dict(seq=i, event=event, source_tick=0, draw_ordinal=0,
                    payload=payload) for i,(event,payload) in enumerate([
            ('handshake', {}), ('start', {}),
            ('boundary', {'boundary':'source_tick','match_index':1}),
            ('boundary', {'boundary':'draw_enter','match_index':1}),
            ('boundary', {'boundary':'draw_return','match_index':1}), ('end', {})])]
        modeled, status = timing_fixture.TimingTests().fixture()
        current = copy.deepcopy(original[:2]) + modeled[2:-1] + copy.deepcopy(original[2:])
        for i,row in enumerate(current): row['seq'] = i
        for row in current[:2]: row['payload']['stadium_selected_request_timing'] = timing.PROFILE
        return original, current, status

    def join(self, original, current):
        joined = m.FirstFormTimingSequenceJoin(original)
        for row in current: joined.accept(row)
        return joined

    def test_native_actual_mode_predicate_preserves_fresh_and_bound_replay(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler: self.skipTest('Native C++ compiler required')
        scratch = self.new_workspace(REFERENCE, 'water-timing-admission-')
        source = (SOURCE/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        helper = function(source, '  static bool SelectedTimingInputAdmit(')
        code = '#include <cassert>\nstruct Admission {\n' + helper + '\n};\nint main(){\n'
        code += 'for(int r=0;r<2;r++) for(int p=0;p<2;p++) for(int f=0;f<2;f++) assert(Admission::SelectedTimingInputAdmit(r,p,f)==((r&&!p&&!f)||(!r&&p&&f)));\n}\n'
        src, binary = scratch/'test.cpp', scratch/'test'
        src.write_text(code)
        for name,argv in [('compile',[compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(src),'-o',str(binary)]),('run',[str(binary)])]:
            result = subprocess.run(argv,capture_output=True,text=True,timeout=30)
            (scratch/(name+'.stdout')).write_text(result.stdout)
            (scratch/(name+'.stderr')).write_text(result.stderr)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        guard = source[source.index('if (selected_timing_enabled &&'):source.index('stadium_whole_long_idle.enabled =',source.index('if (selected_timing_enabled &&'))]
        for leaf in ['whole_session_matches!=3','!stadium_whole_b.enabled','stadium_whole_paired_idle','whole_rng_returns','MWRC_CPU_PROBE_OUTPUT','SelectedTimingInputAdmit']:
            self.assertIn(leaf,guard)

    def test_exact_correspondence_keeps_all_raw_rows_and_sequence_metadata(self):
        original,current,status = self.fixture();before = copy.deepcopy(current)
        timing.validate_selected_request_timing(current,status)
        join = self.join(original,current)
        self.assertTrue(join.complete);self.assertEqual(current,before)
        self.assertEqual(len(join.timing_sequences),10)
        mapped = join.metadata({'sequence':2,'source_tick':2,'source_tick_seqs':[2,3]},to_actual=True)
        self.assertEqual(mapped,{'sequence':12,'source_tick':2,'source_tick_seqs':[12,13]})
        self.assertEqual(join.metadata(mapped,to_actual=False),{'sequence':2,'source_tick':2,'source_tick_seqs':[2,3]})
        self.assertFalse(join.receipt()['raw_rows_modified'])
        with self.assertRaises(Exception):join.metadata({'sequence':3},to_actual=False) # Actual timing row is not an original point.

    def test_missing_extra_reordered_or_changed_source_points_refuse(self):
        for mode in ['missing','extra','reordered','tick','kind']:
            with self.subTest(mode=mode):
                original,current,_ = self.fixture()
                if mode=='missing': current.pop(-3)
                if mode=='extra': current.insert(-2,copy.deepcopy(current[-3]))
                if mode=='reordered': current[-3],current[-2] = current[-2],current[-3]
                if mode=='tick': current[-3]['source_tick'] += 1
                if mode=='kind': current[-3]['payload']['boundary'] = 'pad_consume'
                for i,row in enumerate(current):row['seq']=i
                with self.assertRaises(Exception):self.join(original,current)

    def test_only_declared_timing_rows_can_be_excluded_and_full_decoder_stays_strict(self):
        for mode in ['schema','pc','extra','owner']:
            with self.subTest(mode=mode):
                original,current,status = self.fixture()
                if mode=='schema':current[2]['payload']['schema']='unknown'
                if mode=='pc':current[2]['payload']['pc'] += 4
                if mode=='extra':current[2]['payload']['extra']=0
                if mode=='owner':current[7]['payload']['buffer'] += 32
                if mode=='owner':
                    self.join(original,current) # Full timing decoder still owns exact dynamic owner/clock protocol.
                    with self.assertRaises(ValueError):timing.validate_selected_request_timing(current,status)
                else:
                    with self.assertRaises(Exception):self.join(original,current)
        original,current,_ = self.fixture();join=m.FirstFormTimingSequenceJoin(original)
        for row in current[:-1]:join.accept(row)
        with self.assertRaises(Exception):join.receipt()

    def test_actual_receiver_applies_historical_issue_at_mapped_row_not_host_batch(self):
        original,current,_ = self.fixture()
        issue = dict(after_sequence=2,source=dict(sequence=2,source_tick=0,draw_ordinal=0,mode=8,focus=0),ports=['left','neutral'])
        class Policy:
            last=None
            def __init__(self):self.calls=[]
            def issue(self,ports):self.calls.append(list(ports))
        class Receiver:
            accept=m.StadiumWholeFeedbackReceiver.accept
            def _accept_source(self,row):
                if row['payload'].get('boundary')=='source_tick':
                    self.standings_policy.last=dict(sequence=row['seq'],source_tick=0,draw_ordinal=0,mode=8,focus=0)
        receiver=Receiver();receiver.standings_policy=Policy()
        receiver.first_form_timing_sequence_join=m.FirstFormTimingSequenceJoin(original)
        receiver.standings_replay_issues=[issue];receiver.standings_replay_issue_cursor=0
        for row in current[:12]:receiver.accept(row)
        self.assertEqual(receiver.standings_policy.calls,[])
        receiver.accept(current[12])
        self.assertEqual(receiver.standings_policy.calls,[['left','neutral']])
        self.assertEqual(receiver.standings_replay_issue_cursor,1)
        for row in current[13:]:receiver.accept(row)
        self.assertTrue(receiver.first_form_timing_sequence_join.complete)
        self.assertEqual(issue['after_sequence'],2)

    def test_no_timing_join_does_not_change_default_receiver_behavior(self):
        calls=[]
        class Receiver:
            accept=m.StadiumWholeFeedbackReceiver.accept
            standings_replay_issues=None
            def _accept_source(self,row):calls.append(row)
        row={'seq':17};Receiver().accept(row)
        self.assertEqual(calls,[row])

if __name__=='__main__':unittest.main()
