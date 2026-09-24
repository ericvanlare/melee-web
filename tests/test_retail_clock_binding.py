from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from bind_retail_clock import bind, validate_queue_prediction

class RetailClockBindingTests(unittest.TestCase):
    def test_context_keeps_input_bytes_and_rejects_changed_sources(self):
        recipe=struct.pack('>4sIIIHH',b'MWRC',4,42,4,0x7ff,0x1c0)+bytes(312+822+44*4)
        words=[0]*30
        words[23]=1000000; words[27]=675000
        video=[0]*125; video[23]=2; video[47]=7;video[117]=2
        first={'source_tick':0,'payload':{'clock_pc':0x803769d4,
            'cadence_words':words,'core_ticks':100,'time_adjust':0,
            'time_base':717663,'queue_bytes':[5,0,1,1],
            'video_words':video,'vi_period':8108100,'r3':1}}
        vi={'source_tick':2,'payload':{'clock_pc':0x803769d4,'r3':0,'core_ticks':6733087}}
        rest=[{'source_tick':tick,'payload':{'clock_pc':0x803769d4,'r3':1}} for tick in (1,2,3)]
        rows=[first,rest[0],vi,*rest[1:]]
        clocks=('\n'.join(json.dumps(row) for row in rows)+'\n').encode()
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
        changed=('\n'.join(json.dumps(row) for row in rows)+'\n').encode()
        with self.assertRaisesRegex(ValueError,'unsupported original startup'):
            bind(recipe,changed,digest(recipe),digest(changed))

    def test_incomplete_or_delayed_queue_history_is_rejected(self):
        rows=[{'source_tick':tick,'payload':{'clock_pc':0x803769d4,'r3':1}}
              for tick in range(4)]
        self.assertEqual(validate_queue_prediction(rows,4,3388044,6732987,8100000,8108100),4)
        with self.assertRaisesRegex(ValueError,'complete input-queue history'):
            validate_queue_prediction(rows[:-1],4,3388044,6732987,8100000,8108100)
        # Same final consumed-input total but a different original snapshot:
        # captures a late queue check without supplying any expected draw rows.
        late=[rows[0],rows[1],{'source_tick':2,'payload':{'clock_pc':0x803769d4,'r3':2}}]
        with self.assertRaisesRegex(ValueError,'disagrees with input queue'):
            validate_queue_prediction(late,4,3388044,6732987,8100000,8108100)

    def test_independent_yoshis_queue_snapshots_reject_periodic_prediction(self):
        # Passive original observations: ten pairs, including one late queue
        # check at 8527; no draw trace is supplied to the validator.
        pairs={1527,2528,3529,4530,5531,6532,7533,8527,9535,10536}
        snapshots=[]
        tick=0
        while tick<11077:
            queued=2 if tick in pairs else 1
            snapshots.append({'source_tick':tick,'payload':{'clock_pc':0x803769d4,'r3':queued}})
            tick+=queued
        with self.assertRaisesRegex(ValueError,r'snapshot 8520: predicted \(8527, 1\), observed \(8527, 2\)'):
            validate_queue_prediction(snapshots,11077,1255632,5110718,8100000,8108100)
