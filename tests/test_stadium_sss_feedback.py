"""Synthetic driver/source-record controls; no original input-effect or gameplay claim."""
import copy
import json
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'tools'),str(ROOT/'reference-capture/dolphin')]
from capture_sd_reference_prefix import StadiumGoPrefixReceiver, drive_stadium_sss
from sd_original_menu_plan import stadium_go_prefix_packet, stadium_go_feedback_packet, validate_packet
from retail_input_plan import NEUTRAL_PAD, DISCONNECTED_PAD
from reference_versus_sequence_capture import raw_pad
from test_stadium_sss_position import fixture, source_slice
from reference_observer_stream import SLICE_NAMES


def consume_row(seq,pad=NEUTRAL_PAD):
    queue=bytes([1,0,0,0,0,0,0,0])+(0x804C3000).to_bytes(4,'big')
    ports=[pad,NEUTRAL_PAD,DISCONNECTED_PAD,DISCONNECTED_PAD]
    slot=b''.join(bytes.fromhex(p)+b'\0'for p in ports)
    gprs=[0]*32;gprs[25]=0x804C3000
    slices=[source_slice(2,0x804C1F78,queue),source_slice(3,0x804C3000,slot)]
    for item in slices:item['name']=SLICE_NAMES[item['tag']]
    return dict(seq=seq,event='boundary',payload=dict(boundary='pad_consume',gprs=gprs,slices=slices))


def positions(call,cursor=(0,-13,0),target=(15,5,0),*,absent=False,hidden=False):
    result=[]
    phases=['sss_position_end']if hidden else ['sss_position_cursor']+([]if absent else ['sss_position_target'])+['sss_position_end']
    inside=abs(cursor[0]-target[0])<3.1 and abs(cursor[1]-target[1])<2.7
    for phase in phases:
        row=fixture(phase,call=call,cursor=not hidden,target=not(absent or hidden)and phase!='sss_position_cursor')
        row['source_tick']=row['payload']['source_tick']=call
        row['draw_ordinal']=row['payload']['draw_ordinal']=call+100
        if phase=='sss_position_end'and not hidden:row['payload']['end_row_index']=13 if absent else (18 if inside else 30)
        for item in row['payload']['slices']:
            if item['tag']in(59,66):item['hex']=struct.pack('>3f',*cursor).hex()
            if item['tag']==60:item['hex']=struct.pack('>3f',*target).hex()
            if item['tag']==65:item['hex']=bytes([0,0,13 if absent else (18 if inside else 30),1 if hidden else 0]).hex()
        result.append(row)
    return result


def receiver():
    r=StadiumGoPrefixReceiver(stadium_go_feedback_packet());r.sss_position_route_ready=True
    r.latest_menu={'scene':9};r.stage={'index':30,'kind':None,'stable_polls':1};r.css_live_owner_sequence=1
    return r


def feed(r,row):
    row=copy.deepcopy(row);row['seq']=r.seq;r.accept(row)


class Controller:
    def __init__(self):self.current=[NEUTRAL_PAD]*2;self.actions=[]
    def set_both(self,p1,p2,*,action):self.current=[p1,p2];self.actions.append((action,p1,p2))


class SyntheticSource:
    """Scripted raw observations, not a host model of original cursor arithmetic."""
    def __init__(self,points,*,missing=(),hidden=(),no_consume=False,no_neutral=False,no_stage=False,no_target_neutral=False,no_confirm=False,flood_movement=False,exit_after_confirm=False,exit_before_confirm=False,no_confirm_release=False):
        self.r=receiver();self.c=Controller();self.points=points;self.missing=set(missing);self.hidden=set(hidden)
        self.no_consume=no_consume;self.no_neutral=no_neutral;self.no_stage=no_stage
        self.no_target_neutral=no_target_neutral;self.no_confirm=no_confirm;self.flood_movement=flood_movement
        self.exit_after_confirm=exit_after_confirm;self.exit_before_confirm=exit_before_confirm
        self.no_confirm_release=no_confirm_release;self.exited=False
        self.call=0;self.queue=[]
    def next(self):
        if not self.queue:
            self.call+=1;point,target=self.points[min(self.call-1,len(self.points)-1)]
            pad=self.c.current[0]
            if self.no_target_neutral and self.c.actions[-1][0]=='SSS-feedback:target-neutral':pad=raw_pad(x=70)
            if self.no_confirm and pad==raw_pad(buttons=['A']):pad=NEUTRAL_PAD
            if self.no_confirm_release and self.r.confirm_sequence is not None and pad==NEUTRAL_PAD:pad=raw_pad(x=70)
            if not self.no_consume and not(self.no_neutral and pad==NEUTRAL_PAD):self.queue.append(consume_row(0,pad))
            if self.flood_movement and pad not in (NEUTRAL_PAD,raw_pad(buttons=['A'])):
                self.queue += [consume_row(0,pad) for _ in range(60)]
            if not self.exited and (self.exit_before_confirm or (self.exit_after_confirm and pad==raw_pad(buttons=['A']))):
                self.queue.append(dict(event='progress',payload={'phase':'sss_exit'}));self.exited=True
            if not self.exited:self.queue+=positions(self.call,point,target,absent=self.call in self.missing,hidden=self.call in self.hidden)
            self.queue.append(dict(event='synthetic_poll',seq=0,payload={}))
        row=self.queue.pop(0)
        if row['event']=='synthetic_poll':
            self.r.menu_polls+=1
            point,target=self.points[min(self.call-1,len(self.points)-1)]
            inside=abs(point[0]-target[0])<3.1 and abs(point[1]-target[1])<2.7 and not self.no_stage
            old=self.r.stage;index=18 if inside else 30
            self.r.stage={'index':index,'kind':3 if inside else None,'stable_polls':old['stable_polls']+1 if old['index']==index else 1}
            if self.r.target_neutral_release_sequence is not None and self.r.last_pad[:2]==[NEUTRAL_PAD]*2 and inside:
                self.r.target_neutral_polls+=1
        else:feed(self.r,row)
    def wait(self,predicate,label,cap):
        first=self.r.menu_polls;records=0
        while not predicate():
            if self.r.menu_polls-first>=cap or records>=cap*8+16:raise ValueError('synthetic bounded wait '+label)
            self.next();records+=1
    def run(self):drive_stadium_sss(self.r,self.r.menus,self.c,self.next,self.wait)


class StadiumSssFeedbackTests(unittest.TestCase):
    def test_distinct_named_recipe_and_mutations(self):
        old=stadium_go_prefix_packet();new=stadium_go_feedback_packet()
        self.assertNotIn('policy',old['sss']);self.assertIn('pulses',old['sss'])
        self.assertEqual(validate_packet(new),new);self.assertEqual(new['version'],11)
        self.assertEqual((new['sss']['max_movement_source_polls'],new['sss']['max_movement_callbacks']), (56,56))
        for key,value in [('target_kind',15),('max_reacquire_callbacks',5),('max_owner_polls',601)]:
            bad=copy.deepcopy(new);bad['sss'][key]=value
            with self.assertRaises(ValueError):validate_packet(bad)
    def test_actual_reader_completed_token_is_single_use_and_input_bound(self):
        r=receiver();feed(r,consume_row(0))
        rows=positions(1)
        feed(r,rows[0]);self.assertIsNone(r.sss_geometry_token)
        feed(r,rows[1]);self.assertIsNone(r.sss_geometry_token)
        feed(r,rows[2]);token=r.take_sss_geometry(0)
        self.assertEqual(token['consumed_sample'],r.consumed_samples[-1])
        self.assertEqual(token['target_world_hex'],struct.pack('>3f',15,5,0).hex())
        with self.assertRaises(ValueError):r.take_sss_geometry(0)
        feed(r,consume_row(0));[feed(r,q)for q in positions(2)]
        with self.assertRaises(ValueError):r.take_sss_geometry(r.consumed_samples[-1]['sequence']+1)
    def test_new_call_absent_hidden_phase_and_new_input_invalidate_token(self):
        for mode in ('cursor','absent','hidden','consume','exit'):
            with self.subTest(mode=mode):
                r=receiver();feed(r,consume_row(0));[feed(r,q)for q in positions(1)]
                self.assertIsNotNone(r.sss_geometry_token)
                if mode=='consume':feed(r,consume_row(0))
                elif mode=='exit':feed(r,dict(event='progress',payload={'phase':'sss_exit'}))
                else:
                    rows=positions(2,absent=mode=='absent',hidden=mode=='hidden')
                    for q in rows[:1]if mode=='cursor'else rows:feed(r,q)
                self.assertIsNone(r.sss_geometry_token)
    def test_end_error_and_foreign_phase_invalidate_geometry(self):
        for event,payload in [('error',{'error':'synthetic'}),('end',{'status':'interrupted','natural':False}),
                              ('progress',{'phase':'go_before'})]:
            with self.subTest(event=event):
                r=receiver();feed(r,consume_row(0));[feed(r,q)for q in positions(1)]
                if event=='progress':feed(r,dict(event=event,payload=payload))
                else:
                    with self.assertRaises(ValueError):feed(r,dict(event=event,payload=payload))
                self.assertIsNone(r.sss_geometry_token)
                self.assertIsNone(r.sss_geometry_pending)

    def test_foreign_duplicate_and_midcall_input_refuse(self):
        for mode in ('foreign','duplicate','input','clock'):
            with self.subTest(mode=mode):
                r=receiver();feed(r,consume_row(0));rows=positions(1);feed(r,rows[0])
                q=rows[1]
                if mode=='foreign':q['payload']['cursor_jobj']='0x80500104'
                if mode=='duplicate':q=rows[0]
                if mode=='input':feed(r,consume_row(0))
                if mode=='clock':q['source_tick']+=1;q['payload']['source_tick']+=1
                with self.assertRaises(ValueError):feed(r,q)
    def test_feedback_uses_fresh_moving_target_and_actual_confirm_release(self):
        points=[((0,-13,0),(36,3.7,0)),((8,-13,0),(15,3.7,0)),((14,-13,0),(15,3.7,0)),((14,3,0),(15,3.7,0))]
        s=SyntheticSource(points);s.run()
        pads=[p for label,p,_ in s.c.actions if label=='SSS-feedback:cardinal']
        self.assertEqual(pads,[raw_pad(x=70),raw_pad(x=70),raw_pad(y=70)])
        self.assertIsNotNone(s.r.confirm_sequence);self.assertGreater(s.r.confirm_release_sequence,s.r.confirm_sequence)
        labels=[q[0]for q in s.c.actions]
        self.assertLess(labels.index('SSS-feedback:target-neutral'),labels.index('SSS-Stadium-confirm'))
    def test_near_target_uses_declared_small_axis(self):
        s=SyntheticSource([((10,-13,0),(15,3.7,0)),((14,3,0),(15,3.7,0))]);s.run()
        self.assertIn(('SSS-feedback:cardinal',raw_pad(x=35),NEUTRAL_PAD),s.c.actions)
    def test_earlier_hit_reacquires_without_another_geometry_command(self):
        points=[((0,-13,0),(15,3.7,0)),((2,-13,0),(15,3.7,0)),((4,-13,0),(15,3.7,0)),((14,3,0),(15,3.7,0))]
        s=SyntheticSource(points,missing=(2,3));s.run()
        self.assertEqual(sum(x[0]=='SSS-feedback:cardinal'for x in s.c.actions),1)
    def test_missing_target_reacquisition_is_bounded(self):
        s=SyntheticSource([((0,-13,0),(15,3.7,0))],missing=range(2,20))
        with self.assertRaisesRegex(ValueError,'bounded reacquisition'):s.run()
        self.assertEqual(s.call,6);self.assertNotIn('SSS-Stadium-confirm',[q[0]for q in s.c.actions])
    def test_no_consumption_hidden_and_no_neutral_cannot_steer(self):
        for options in ({'no_consume':True},{'hidden':(1,)},{'no_neutral':True}):
            with self.subTest(options=options):
                s=SyntheticSource([((0,-13,0),(15,3.7,0))],**options)
                with self.assertRaises(ValueError):s.run()
                self.assertNotIn('SSS-feedback:cardinal',[q[0]for q in s.c.actions])
    def test_movement_and_owner_caps_fail_without_confirm(self):
        for cap in ('movement','owner'):
            with self.subTest(cap=cap):
                s=SyntheticSource([((0,-13,0),(15,3.7,0))])
                if cap=='owner':s.r.menus['sss']['max_owner_polls']=3
                with self.assertRaisesRegex(ValueError,cap if cap=='movement'else 'live-owner'):s.run()
                self.assertNotIn('SSS-Stadium-confirm',[q[0]for q in s.c.actions])
    def test_actual_consumption_cap_does_not_depend_on_poll_or_callback_count(self):
        s=SyntheticSource([((0,-13,0),(15,3.7,0))],flood_movement=True)
        with self.assertRaisesRegex(ValueError,'movement cap'):s.run()
        self.assertEqual(s.r.sss_callback_count,1)
        self.assertLess(s.r.menu_polls,3)
        self.assertIsNone(s.r.confirm_sequence)

    def test_malformed_new_observation_clears_prior_token_before_refusal(self):
        r=receiver();feed(r,consume_row(0));[feed(r,q)for q in positions(1)]
        q=positions(2)[0];q['payload']['cursor_jobj']='0x80500104'
        with self.assertRaises(ValueError):feed(r,q)
        self.assertIsNone(r.sss_geometry_token)
        self.assertIsNone(r.sss_geometry_pending)

    def test_target_neutral_and_confirm_must_actually_be_consumed(self):
        for mode in ('no_target_neutral','no_confirm'):
            with self.subTest(mode=mode):
                s=SyntheticSource([((0,-13,0),(15,3.7,0)),((14,3,0),(15,3.7,0))],**{mode:True})
                with self.assertRaises(ValueError):s.run()
                self.assertIsNone(s.r.confirm_sequence)
                self.assertIsNone(s.r.confirm_release_sequence)

    def test_actual_receiver_release_only_may_cross_source_sss_exit(self):
        s=SyntheticSource([((0,-13,0),(15,3.7,0)),((14,3,0),(15,3.7,0))],exit_after_confirm=True)
        s.run()
        self.assertFalse(s.r.sss_position_route_ready)
        self.assertIsNotNone(s.r.confirm_sequence)
        self.assertGreater(s.r.confirm_release_sequence,s.r.confirm_sequence)
        labels=[q[0]for q in s.c.actions]
        self.assertEqual(labels[labels.index('SSS-Stadium-confirm')+1:],['SSS-Stadium-confirm:neutral-release'])
        self.assertIsNone(s.r.sss_geometry_token)

    def test_source_exit_before_confirm_and_missing_postconfirm_neutral_refuse(self):
        s=SyntheticSource([((0,-13,0),(15,3.7,0))],exit_before_confirm=True)
        with self.assertRaisesRegex(ValueError,'live-owner'):s.run()
        self.assertNotIn('SSS-feedback:cardinal',[q[0]for q in s.c.actions])
        s=SyntheticSource([((0,-13,0),(15,3.7,0)),((14,3,0),(15,3.7,0))],exit_after_confirm=True,no_confirm_release=True)
        with self.assertRaises(ValueError):s.run()
        self.assertIsNotNone(s.r.confirm_sequence)
        self.assertIsNone(s.r.confirm_release_sequence)
        labels=[q[0]for q in s.c.actions]
        self.assertEqual(labels[labels.index('SSS-Stadium-confirm')+1:],['SSS-Stadium-confirm:neutral-release'])

    def test_geometry_inside_does_not_replace_original_selection_authority(self):
        s=SyntheticSource([((14,3,0),(15,3.7,0))],no_stage=True)
        with self.assertRaises(ValueError):s.run()
        self.assertIsNone(s.r.confirm_sequence)

if __name__=='__main__':unittest.main()
