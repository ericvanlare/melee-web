from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from bind_retail_clock import bind

class RetailClockBindingTests(unittest.TestCase):
    def test_context_keeps_input_bytes_and_rejects_changed_sources(self):
        recipe=struct.pack('>4sIIIHH',b'MWRC',4,42,4,0x7ff,0x1c0)+bytes(312+822+44*4)
        words=[0]*30
        words[23]=1000000; words[27]=675000
        video=[0]*125; video[23]=2; video[47]=7;video[117]=2
        first={'source_tick':0,'payload':{'clock_pc':0x803769d4,
            'cadence_words':words,'core_ticks':100,'time_adjust':0,
            'time_base':717663,'queue_bytes':[5,0,1,1],
            'video_words':video,'vi_period':8108100}}
        vi={'source_tick':2,'payload':{'clock_pc':0x803769d4,'r3':0,'core_ticks':6733087}}
        clocks=('\n'.join(json.dumps(row) for row in (first,vi))+'\n').encode()
        digest=lambda data:hashlib.sha256(data).hexdigest()
        result,evidence=bind(recipe,clocks,digest(recipe),digest(clocks))
        self.assertEqual(struct.unpack_from('>I',result,4)[0],5)
        self.assertEqual(result[16:20],recipe[16:20])
        self.assertEqual(result[60:],recipe[20:])
        self.assertEqual(evidence['next_pad_relative_cpu_ticks'],3388044)
        with self.assertRaisesRegex(ValueError,'recipe SHA-256'):
            bind(recipe+b'!',clocks,digest(recipe),digest(clocks))
        with self.assertRaisesRegex(ValueError,'clock SHA-256'):
            bind(recipe,clocks+b' ',digest(recipe),digest(clocks))
        first['payload']['video_words'][117]=3
        changed=('\n'.join(json.dumps(row) for row in (first,vi))+'\n').encode()
        with self.assertRaisesRegex(ValueError,'unsupported original startup'):
            bind(recipe,changed,digest(recipe),digest(changed))
