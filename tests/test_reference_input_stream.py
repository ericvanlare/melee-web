import hashlib,json,struct,tempfile,unittest,zlib
from pathlib import Path
from tools.reference_input_stream import HEADER,RECORD,validate_stream,validate_status,InputStreamError

def event(index,tick,port,kind=1,connected=1):
    raw=RECORD.pack(index,tick,port,0x100,128,128,128,128,0,0,0,0,0,connected,kind,0)
    return raw[:-4]+struct.pack('<I',zlib.crc32(raw[:-4]))

class InputStreamTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.path=Path(self.tmp.name)/'inputs.mwri'
        self.good=HEADER.pack(b'MWRI',1,16,40,0)+event(0,100,0)+event(1,100,256,connected=0)+event(2,101,0)+event(3,101,0xffffffff,2)
        self.path.write_bytes(self.good)
    def test_complete_native_field_stream(self):
        r=validate_stream(self.path)
        self.assertEqual(r['events'],3);self.assertEqual(r['sha256'],hashlib.sha256(self.good).hexdigest())
        self.assertEqual(r['operations'],{'pad_status':2,'adapter_connection':1})
    def test_reject_truncated_corrupt_reordered_and_incomplete_streams(self):
        for raw in [self.good[:-1],self.good[:16]+event(1,100,0)+self.good[56:],self.good[:16]+event(0,102,0)+self.good[56:],self.good[:-40]+event(3,101,0xffffffff,3),self.good[:-1]+b'X',self.good[:16]+event(0,100,4)+self.good[56:],self.good[:16]+event(0,100,0,connected=2)+self.good[56:]]:
            with self.subTest(length=len(raw)):
                self.path.write_bytes(raw)
                with self.assertRaises(InputStreamError):validate_stream(self.path)
    def test_status_requires_completion_mode_and_count(self):
        p=self.path.with_suffix('.json');v={'version':1,'mode':'record','events':3,'complete':True,'invalid':False,'error':None}
        p.write_text(json.dumps(v));validate_status(p,mode='record',events=3)
        for patch in [{'complete':False},{'invalid':True},{'events':2},{'mode':'replay'},{'error':'overflow'},{'events':True}]:
            p.write_text(json.dumps(dict(v,**patch)))
            with self.assertRaises(InputStreamError):validate_status(p,mode='record',events=3)
    def test_symlink_is_not_an_input_recording(self):
        p=self.path.with_suffix('.link');p.symlink_to(self.path)
        with self.assertRaises(InputStreamError):validate_stream(p)
