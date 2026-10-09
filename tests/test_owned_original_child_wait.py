"""Actual tiny child-exit controls; never launch Dolphin or a gameplay workload."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'tools'),str(ROOT/'reference-capture/dolphin')]
from capture_sd_reference_prefix import BoundedLog, check_owned_native_wait, cleanup_process
from sd_reference_diagnostic import SdDiagnosticError
from reference_versus_sequence_capture import ObserverTail, CaptureError
from test_reference_versus_sequence_capture import _frame


class OwnedOriginalChildWaitTests(unittest.TestCase):
    def test_actual_child_exit_fails_all_missing_record_waits_and_reaps(self):
        handshake=_frame(1,0,json.dumps({'whole_session':True,'match_count':1}).encode())
        for mode in ('missing_file','partial_header','partial_payload'):
            with self.subTest(mode=mode),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);path=root/'observer.bin'
                if mode=='partial_header':path.write_bytes(handshake[:10])
                if mode=='partial_payload':path.write_bytes(handshake[:-1])
                with (root/'child.log').open('wb') as log:
                    child=subprocess.Popen([sys.executable,'-c',
                        "import sys; print('declared tiny child failure',flush=True); sys.exit(7)"],
                        stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                    drain=BoundedLog(child.stdout,log,4096)
                    started=time.monotonic()
                    try:
                        with ObserverTail(path,None,poll=0.001,
                                wait_check=lambda:check_owned_native_wait(child,drain)) as tail:
                            with self.assertRaisesRegex(SdDiagnosticError,'native child exited.*returncode 7'):
                                tail.next(time.monotonic()+60)
                        self.assertLess(time.monotonic()-started,5)
                    finally:
                        cleanup_process(child,root,scope='tiny-child-control')
                        drain.finish()
                cleanup=json.loads((root/'cleanup.json').read_text())
                self.assertEqual(cleanup['pid'],child.pid)
                self.assertEqual(cleanup['returncode'],7)
                self.assertFalse(cleanup['terminate_sent'])
                self.assertFalse(cleanup['kill_sent'])
                self.assertIsNone(cleanup['error'])
                self.assertIn('declared tiny child failure',(root/'child.log').read_text())

    def test_available_records_and_footer_do_not_require_live_child(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'observer.bin'
            path.write_bytes(_frame(1,0,b'{"whole_session":true,"match_count":1}')+
                             _frame(6,1,b'{"complete":true}'))
            def fail_if_waited():raise AssertionError('Available record invoked wait health')
            with ObserverTail(path,None,wait_check=fail_if_waited) as tail:
                self.assertEqual(tail.next(time.monotonic()+60)['event'],'handshake')
                self.assertEqual(tail.next(time.monotonic()+60)['event'],'end')

    def test_legacy_missing_file_uses_original_deadline(self):
        with tempfile.TemporaryDirectory() as temp:
            with ObserverTail(Path(temp)/'absent',None,poll=0.001) as tail:
                with self.assertRaisesRegex(CaptureError,'did not appear before deadline'):
                    tail.next(time.monotonic()+0.01)

    def test_live_child_preserves_bounded_drain_failure(self):
        class Child:
            def poll(self):return None
        class Drain:
            def check(self):raise SdDiagnosticError('owned drain failed')
        with self.assertRaisesRegex(SdDiagnosticError,'owned drain failed'):
            check_owned_native_wait(Child(),Drain())
