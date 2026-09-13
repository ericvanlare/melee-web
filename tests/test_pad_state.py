"""PAD snapshot semantics and new retail schema controls."""
from copy import deepcopy
from pathlib import Path
import struct
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'scripts'))
from check_gameplay import node_runtime
from pad_state import decode_pad_state, PAD_STATE_BYTES
from retail_replay_validation import _validate_capture, CaptureError, compare
from port_replay_validation import compare_rows
from retail_replay_recipe import encode_mwrc
from retail_draw_audit import validate_draw_rows
from test_retail_replay_validation import candidate
from test_port_replay_validation import fixture


def snapshot():
    # Canonical source defaults, followed by twelve independent histories.
    config=bytes.fromhex('0000002d00000008001e0000000000007f0000ff0000ff007fffff000000')
    assert len(config)==30
    return (config+bytes(12*66)).hex()


class PadSnapshotTests(unittest.TestCase):
    def test_native_codec_and_source_input_edges(self):
        target=ROOT/'build/browser/gameplay_pad_state_trace.js'
        if not target.is_file():self.skipTest('Build gameplay_pad_state_trace')
        result=subprocess.run([str(node_runtime()),str(target)],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('held/released edges',result.stdout)

    def test_v2_preserves_history_and_reports_exact_bank_and_member(self):
        rows=candidate();rows[0]['version']=2
        for key in ('pad_lib_hex','pad_master_hex','pad_game_hex'):del rows[1][key]
        for row in rows[1:-1]:row['pad_state_hex']=snapshot()
        reference=_validate_capture(rows,'v2')
        _,port=fixture();port[0]['version']=2
        for row in port[1:-1]:row['pad_state_hex']=snapshot()
        self.assertEqual(compare_rows(reference,port)['status'],'declared_state_match')
        raw=bytearray.fromhex(port[4]['pad_state_hex'])
        struct.pack_into('>i',raw,30+4*66+20,7)
        port[4]['pad_state_hex']=raw.hex()
        report=compare_rows(reference,port)
        self.assertEqual(report['first_divergence']['frame'],1)
        self.assertEqual(report['first_divergence']['field'],'pad_state.copy[0].repeat_count')
        payload,_=encode_mwrc(reference)
        self.assertEqual(struct.unpack_from('>I',payload,4)[0],2)
        self.assertEqual(payload[328:328+PAD_STATE_BYTES],bytes.fromhex(snapshot()))
        self.assertEqual(len(payload),328+PAD_STATE_BYTES+3*44)
        missing=deepcopy(rows);del missing[-2]['pad_state_hex']
        with self.assertRaises(CaptureError):_validate_capture(missing,'missing')

    def test_nonfinite_history_and_zero_scale_rejected(self):
        for offset, data in ((30+32,bytes.fromhex('7f800000')),(24,b'\0')):
            raw=bytearray.fromhex(snapshot());raw[offset:offset+len(data)]=data
            with self.assertRaises(ValueError):decode_pad_state(raw.hex())

    def test_draw_audit_binds_every_traversal_and_keeps_changes_visible(self):
        rows=candidate();rows[0]['version']=2
        for key in ('pad_lib_hex','pad_master_hex','pad_game_hex'):del rows[1][key]
        for row in rows[1:-1]:row['pad_state_hex']=snapshot()
        reference=_validate_capture(rows,'v2')
        draws=[]
        for index, frame in enumerate(reference.frames):
            before={k:deepcopy(frame[k]) for k in ('rng','match_frame','scene_frame','fighters','pad_state_hex')}
            before['scene_frame']+=1
            draws.append({'record':'draw','index':index,'before':before,'after':deepcopy(before)})
        self.assertEqual(validate_draw_rows(reference,draws)['status'],'declared_state_unchanged')
        changed=deepcopy(draws);changed[1]['after']['rng']^=1
        result=validate_draw_rows(reference,changed)
        self.assertEqual(result['status'],'declared_state_changed')
        self.assertEqual(result['first_change']['index'],1)
        self.assertEqual(result['first_change']['field'],'rng')
        invalid=deepcopy(draws);invalid[1]['before']['rng']^=1
        missing=deepcopy(draws);del missing[-1]['after']['pad_state_hex']
        reordered=deepcopy(draws);reordered.reverse()
        boolean=deepcopy(draws);boolean[0]['index']=False
        for bad in (draws[:-1],draws+draws[-1:],invalid,missing,reordered,boolean):
            with self.assertRaises(CaptureError):validate_draw_rows(reference,bad)
