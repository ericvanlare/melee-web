"""Synthetic source-timing protocol controls, never original evidence."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import stadium_selected_request_timing as m

class TimingTests(unittest.TestCase):
    def fixture(self, interleave=False, before_read=False):
        status=dict(state='completed',completed=True,invalid=False,error=None,
                    stadium_selected_request_timing=m.PROFILE,stadium_selected_request_timing_complete=True)
        out=[dict(seq=i,event=e,payload={'stadium_selected_request_timing':m.PROFILE}) for i,e in enumerate(['handshake','start'])]
        kinds=['request_enter','request_return','poll_enter','poll_pending_read','poll_return',
               'callback_enter','callback_return','poll_enter','poll_pending_read','poll_return']
        if interleave:
            kinds=['request_enter','request_return','poll_enter','poll_pending_read',
                   'callback_enter','callback_return','poll_return','poll_enter','poll_pending_read','poll_return']
        if before_read:
            kinds=['request_enter','request_return','poll_enter','callback_enter','callback_return',
                   'poll_pending_read','poll_return']
        pending=True;decision=None
        for i,k in enumerate(kinds):
            if k=='callback_return':pending=False
            result=0
            if k=='request_return':result=5
            if k=='poll_pending_read':result=0x40 if pending else 0;decision=pending
            if k=='poll_return':result=int(not decision)
            p=dict(schema=m.SCHEMA,profile=m.PROFILE,kind=k,whole_session=True,match_index=1,
                   selected_form=9,clock_ticks=100+i,request_id=0 if i==0 else 5,file_entry=421,
                   map2_gobj=0x80500000,map2_ground=0x80501000,buffer=0x80600000,
                   logical_size=297517,transfer_size=297536,pending_flag=pending,
                   result=result,pc=m.PCS[k],sp=0x817ff000,lr=0x80000100,
                   source_tick=3700+i,draw_ordinal=3690+i,drive_model_observed=False)
            if k=='poll_pending_read':p.update(sp=0x817feff0,lr=0x801d42fc)
            out.append(dict(seq=i+2,event='progress',source_tick=p['source_tick'],draw_ordinal=p['draw_ordinal'],payload=p))
        out.append(dict(seq=len(out),event='end',payload={}));return out,status

    @staticmethod
    def row(rows, kind):
        return next(r for r in rows if r['payload'].get('kind')==kind)

    def test_labeled_model_positive(self):
        a,b=self.fixture();r=m.validate_selected_request_timing(a,b)
        self.assertEqual(r['request_id'],5);self.assertFalse(r['drive_model_observed'])

    def test_callback_between_read_and_false_return(self):
        a,b=self.fixture(interleave=True);r=m.validate_selected_request_timing(a,b)
        first_return=self.row(a,'poll_return')['payload']
        self.assertFalse(first_return['pending_flag'])
        self.assertEqual(first_return['result'],0)
        self.assertEqual(len([p for p in r['rows'] if p['kind']=='poll_pending_read']),2)
        # The stale read makes false correct even though callback cleared live memory.
        first_return['result']=1
        with self.assertRaisesRegex(ValueError,'Timing poll return'):
            m.validate_selected_request_timing(a,b)

    def test_callback_before_read_uses_new_byte(self):
        a,b=self.fixture(before_read=True);m.validate_selected_request_timing(a,b)
        self.assertEqual(self.row(a,'poll_pending_read')['payload']['result'],0)
        self.row(a,'poll_return')['payload']['result']=0
        with self.assertRaisesRegex(ValueError,'Timing poll return'):
            m.validate_selected_request_timing(a,b)

    def test_read_identity_and_impossible_decisions_refuse(self):
        for mode in ['missing','repeat','byte','sp','lr','decision']:
            with self.subTest(mode=mode):
                a,b=self.fixture();r=self.row(a,'poll_pending_read')
                if mode=='missing':a.remove(r)
                if mode=='repeat':
                    # Replace first false-return with a second read, preserving envelope order.
                    q=self.row(a,'poll_return');q['payload'].update(kind='poll_pending_read',pc=m.PCS['poll_pending_read'],sp=r['payload']['sp'],lr=r['payload']['lr'])
                if mode=='byte':r['payload']['result']=256
                if mode=='sp':r['payload']['sp']+=4
                if mode=='lr':r['payload']['lr']+=4
                if mode=='decision':r['payload']['result']=0
                with self.assertRaises(ValueError):m.validate_selected_request_timing(a,b)

    def test_mutations_refuse(self):
        for mode in ['owner','clock','id','callback','poll','status','unknown','extent']:
            with self.subTest(mode=mode):
                a,b=self.fixture()
                if mode=='owner':self.row(a,'poll_return')['payload']['buffer']+=32
                if mode=='clock':self.row(a,'poll_return')['payload']['clock_ticks']=0
                if mode=='id':self.row(a,'poll_return')['payload']['request_id']=6
                if mode=='callback':a.remove(self.row(a,'callback_return'))
                if mode=='poll':a.pop(-2)
                if mode=='status':b['stadium_selected_request_timing_complete']=False
                if mode=='unknown':self.row(a,'poll_enter')['payload']['extra']=0
                if mode=='extent':a[2]['payload']['transfer_size']-=32
                with self.assertRaises(ValueError):m.validate_selected_request_timing(a,b)

if __name__=='__main__':unittest.main()
