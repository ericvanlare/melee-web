from __future__ import annotations
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'reference-capture/dolphin')]
import reference_observer_stream as wire
import test_stadium_go_prefix as route
import test_stadium_idle_state_contract as state
from test_stadium_constructor_observations import literal_frames, stored_frame, with_constructor
from stadium_go_prefix import (validate_stadium_go_prefix, validate_stadium_idle_3500_reference,
                                StadiumGoPrefixError)


def frame(kind, tick, slices):
    raw=route._boundary(kind,0,pc={2:0x80377584,6:0x80390eb4,7:0x80390fc0,8:0x80391040,1:0x8034dd8c}[kind],
                        source_tick=tick,slices=slices)
    h,b=literal_frames(raw)[0]
    if kind==2:
        b=bytearray(b);struct.pack_into('<I',b,20+25*4,0x81006000);b=bytes(b)
    return stored_frame(h,b,replacement_payload=b)


def consume(tick):
    q=bytearray(12);q[0]=5;q[8:]=struct.pack('>I',0x81006000)
    pads=bytearray(48);pads[34]=255;pads[46]=255
    return frame(2,tick,[state._slice(2,0,0x804c1f78,bytes(q)),state._slice(3,0,0x81006000,bytes(pads))])


def state_slices(countdown):
    rows=copy.deepcopy(state._record()['payload']['slices'])
    for s in rows:
        if s['tag']==67 and s['flags']==2: s['hex']='81001000'
        if s['tag']==68 and s['flags']==2: s.update(address=0x8100102c,hex='81002000')
        if 69<=s['tag']<=75: s['address']+=0x81002000-0x80411200
        if s['tag']==69: s['hex']='00'
        if s['tag']==70: s['hex']=struct.pack('>i',countdown).hex()
        if s['tag']==71: s['hex']='0000'
    return rows


def match_slices(tick):
    camera=bytearray(76);camera[:4]=struct.pack('>I',0x81007000)
    return [state._slice(14,0,0x8046b6a0,bytes(46)),
            state._slice(21,0,0x804c1f84,bytes(856)),
            state._slice(30,0,0x80479d58,struct.pack('>I',tick)),
            state._slice(19,0,0x804d5f94,bytes.fromhex('804d5f90')),
            state._slice(20,0,0x804d5f90,bytes.fromhex('263cc5ca')),
            state._slice(10,0,0x80452c68,bytes(camera)),
            state._slice(28,0,0x81007028,struct.pack('>I',0x81008000)),
            state._slice(11,0,0x81008000,bytes(81))]


def pad_poll_slices():
    queue=bytearray(12);queue[0]=5;queue[8:]=struct.pack('>I',0x81006000)
    return [state._slice(1,0,0x81009000,bytes(48)),
            state._slice(27,0,0x8100a054,bytes.fromhex('80376a28')),
            state._slice(2,0,0x804c1f78,bytes(queue)),
            state._slice(21,0,0x804c1f84,bytes(856)),
            state._slice(22,0,0x804d7420,bytes(4)),
            state._slice(23,0,0x804a7f98,bytes(4)),
            state._slice(17,0,0x80479d30,bytes(6))]

def pad_poll(tick):
    raw=frame(1,tick,pad_poll_slices())
    h,b=literal_frames(raw)[0]
    b=bytearray(b)
    struct.pack_into('<I',b,20+31*4,0x81009030)
    struct.pack_into('<I',b,20+4,0x8100a000)
    return stored_frame(h,bytes(b),replacement_payload=bytes(b))


def fixture(*, tail=0, end_ordinal=3500, mutate=None, pad_poll_after_draw_enter=False):
    raw,status=route._fixture()
    raw,status=with_constructor(raw,status)
    out=[];go=False;end=None
    for h,b in literal_frames(raw):
        row=wire._decode_record(h,b,'3500 fixture');p=row['payload']
        if row['event']=='end': end=p;continue
        if go: continue
        if row['event'] in ('handshake','start'):
            p['original_idle_post_go_ticks']=3500
            if row['event']=='start':p['completion_boundary']='relative_post_go_tick_3500_then_first_draw_return'
            b=json.dumps(p).encode()
        if row['event']=='boundary' and p['boundary']=='source_tick':out.append(consume(p['source_tick']))
        if p.get('phase')=='go_after':
            go=True
            out.append(consume(p['source_tick']))
            p['slices']+=state_slices(3629)
            p.update(state_snapshot_schema='stadium_idle_state_v1',sis_table_entry_relationship_observed=False,
                     dynamic_title_text_bytes_observed=False)
            b=json.dumps(p).encode()
        out.append(stored_frame(h,b,replacement_payload=b))
    for ordinal in range(end_ordinal+tail+1):
        tick=125+ordinal
        if ordinal:out.append(consume(tick))
        slices=[route._slice(2,12)]+match_slices(tick)+state_slices(3628-ordinal)
        record=frame(6,tick,slices)
        if mutate:record=mutate(ordinal,record)
        out.append(record)
        if ordinal<end_ordinal and ordinal%5==4:
            enter=frame(7,tick,[route._slice(2,12)]+match_slices(tick))
            leave=frame(8,tick,[route._slice(2,12)]+match_slices(tick))
            if pad_poll_after_draw_enter and ordinal==4:
                out += [enter,pad_poll(tick),leave]
            else:
                out += [enter,leave]
    out += [frame(7,125+end_ordinal+tail,[route._slice(2,12)]+match_slices(125+end_ordinal+tail)),
            frame(8,125+end_ordinal+tail,[route._slice(2,12)]+match_slices(125+end_ordinal+tail))]
    end.update(original_idle_post_go_ticks=3500,comparison_end_tick=3625,tail_ticks_before_draw_return=tail,
               completion_boundary="relative_post_go_tick_3500_then_first_draw_return")
    out.append(route._frame(6,0,json.dumps(end).encode(),source_tick=125+end_ordinal+tail))
    # The native publisher increments its draw ordinal only after DrawReturn.
    current_draw=0; adjusted=[]; after_go=False
    for raw in out:
        h,b=literal_frames(raw)[0];r=wire._decode_record(h,b,'draw ordinal fixture')
        if r['payload'].get('phase')=='go_after':after_go=True;current_draw=r['draw_ordinal']
        if after_go and r['event']=='boundary':
            header=list(h);header[7]=current_draw
            raw=stored_frame(tuple(header),b,replacement_payload=b)
            if r['payload']['boundary']=='draw_return':current_draw+=1
        adjusted.append(raw)
    out=adjusted
    result=b''.join(stored_frame(h,b,sequence=i) for i,raw in enumerate(out) for h,b in literal_frames(raw))
    n=len(out);status.update(event_count=n,last_seq=n-1,source_tick=125+end_ordinal+tail)
    return result,status


class StadiumIdle3500ReferenceTests(unittest.TestCase):
    def validate(self, data,status,extract=False):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'observer.bin';p.write_bytes(data)
            q=Path(str(p)+'.status.json');q.write_text(json.dumps(status))
            return validate_stadium_idle_3500_reference(p,status_path=q,extract_inputs=extract)

    def test_whole_route_literal_inputs_relative_window_and_natural_tail(self):
        raw,status=fixture(tail=2)
        result=self.validate(raw,status,True)
        self.assertEqual(result['comparison_end_countdown'],128)
        self.assertEqual(result['natural_tail_ticks'],2)
        self.assertEqual(len(result['literal_input_bundles']),3503)
        self.assertEqual(result['literal_input_bundles'][0]['ports'][2]['status_hex'],'00'*10+'ff')
        self.assertFalse(result['native_parity'])
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'observer.bin';p.write_bytes(raw);Path(str(p)+'.status.json').write_text(json.dumps(status))
            with self.assertRaises(StadiumGoPrefixError):validate_stadium_go_prefix(p,constructor_observations=True)

    def test_recurrence_owner_and_terminal_counterexamples(self):
        def corrupt(ordinal,raw):
            if ordinal!=7:return raw
            h,b=literal_frames(raw)[0];p=wire._decode_record(h,b,'mutation')['payload']
            item=next(s for s in p['slices'] if s['tag']==70);item['hex']='00000001'
            return frame(6,132,p['slices'])
        for kwargs in ({'end_ordinal':3499},{'tail':5},{'mutate':corrupt}):
            with self.subTest(kwargs=kwargs), self.assertRaises(StadiumGoPrefixError):self.validate(*fixture(**kwargs))

    def test_draw_ordinal_continuity_and_open_draw_refusals(self):
        from stadium_go_prefix import StadiumIdle3500Observations
        raw,_=fixture()
        rows=[wire._decode_record(h,b,'continuity fixture') for h,b in literal_frames(raw)]
        first=next(i for i,r in enumerate(rows) if r['event']=='boundary' and
                   r['payload']['boundary']=='draw_enter' and r['source_tick']>=125)
        for mutation in ('missing_pair','duplicate_pair','jump','tick_inside_draw','consume_inside_draw'):
            prefix=copy.deepcopy(rows[:first+3])
            if mutation=='missing_pair':del prefix[first:first+2]
            elif mutation=='duplicate_pair':prefix[first+2:first+2]=copy.deepcopy(prefix[first:first+2])
            elif mutation=='jump':
                prefix[first]['draw_ordinal']+=1;prefix[first]['payload']['draw_ordinal']+=1
            else:
                inserted=copy.deepcopy(rows[first+3] if mutation=='tick_inside_draw' else prefix[first+2])
                inserted['draw_ordinal']=prefix[first]['draw_ordinal']
                inserted['payload']['draw_ordinal']=prefix[first]['draw_ordinal']
                prefix.insert(first+1,inserted)
            decoder=StadiumIdle3500Observations()
            with self.subTest(mutation=mutation),self.assertRaises(StadiumGoPrefixError):
                for row in prefix:decoder.accept(row)

    def test_open_draw_pad_poll_same_ordinal_tick_and_return_contract(self):
        from stadium_go_prefix import StadiumIdle3500Observations
        raw,_=fixture(pad_poll_after_draw_enter=True)
        rows=[wire._decode_record(h,b,'same-draw synthetic PadPoll') for h,b in literal_frames(raw)]
        enter_index=next(i for i,r in enumerate(rows) if r['event']=='boundary' and
                         r['payload']['boundary']=='draw_enter' and r['source_tick']==129)
        enter=rows[enter_index]
        poll=rows[enter_index+1]
        leave=rows[enter_index+2]
        self.assertEqual([r['payload'].get('boundary') for r in rows[enter_index:enter_index+3]],
                         ['draw_enter','pad_poll','draw_return'])
        self.assertEqual({r['source_tick'] for r in rows[enter_index:enter_index+3]},{129})
        self.assertEqual({r['draw_ordinal'] for r in rows[enter_index:enter_index+3]},
                         {enter['draw_ordinal']})

        def before_boundary():
            decoder=StadiumIdle3500Observations()
            for row in rows[:enter_index+1]:decoder.accept(row)
            return decoder

        def schedule(decoder):
            return (decoder.last_ordinal,decoder.pending is None,len(decoder.tick_sequences),
                    len(decoder.bundles),len(decoder.seed_observations),
                    decoder.state_digest.hexdigest(),len(decoder.actions),decoder.owner,
                    decoder.c_countdown,getattr(decoder,'map2_history',None))

        decoder=before_boundary()
        before=schedule(decoder)
        open_ordinal=decoder.draw_open
        self.assertEqual(open_ordinal,decoder.expected_draw)
        self.assertEqual(decoder.draw_open_tick,129)
        decoder.accept(poll)
        self.assertEqual(schedule(decoder),before)
        self.assertEqual(decoder.draw_open,open_ordinal)
        self.assertEqual(decoder.expected_draw,open_ordinal)
        self.assertEqual(decoder.draw_open_tick,129)

        wrong_tick=copy.deepcopy(poll)
        wrong_tick['source_tick']+=1
        wrong_tick['payload']['source_tick']+=1
        rejected=before_boundary()
        rejected_state=schedule(rejected)
        with self.assertRaisesRegex(StadiumGoPrefixError,'open-draw source tick differs'):
            rejected.accept(wrong_tick)
        self.assertEqual(schedule(rejected),rejected_state)

        # A valid synthetic SourceTick placed inside the open draw remains forbidden.
        source_tick=copy.deepcopy(next(r for r in rows if r['event']=='boundary' and
                                       r['payload'].get('boundary')=='source_tick' and
                                       r['source_tick']==129))
        source_tick['draw_ordinal']=open_ordinal
        source_tick['payload']['draw_ordinal']=open_ordinal
        forbidden=before_boundary()
        with self.assertRaisesRegex(StadiumGoPrefixError,'boundary not allowed inside source draw'):
            forbidden.accept(source_tick)

        decoder.accept(leave)
        self.assertIsNone(decoder.draw_open)
        self.assertIsNone(decoder.draw_open_tick)
        self.assertEqual(decoder.expected_draw,open_ordinal+1)
        self.assertEqual(schedule(decoder)[:-1],(before[0],before[1],before[2],before[3],
                         before[4],before[5],before[6]+1,before[7],before[8]))
        with self.assertRaisesRegex(StadiumGoPrefixError,'interval/terminal consume accounting incomplete'):
            decoder.finish({'tail_ticks_after_comparison_end':0})

    def test_binary_minima_and_cap_derivation(self):
        poll=[route._slice(t,n) for t,n in [(1,48),(27,4),(2,12),(21,856),(22,4),(23,4),(17,6)]]
        draw=[route._slice(t,n) for t,n in [(14,46),(21,856),(30,4),(19,4),(20,4),(10,76),(28,4),(11,81)]]
        self.assertEqual(len(frame(1,0,poll)),1238)
        self.assertEqual(len(frame(8,0,draw)),1395)
        self.assertEqual(16384+2*3505+(64*1024*1024)//1238+1,77602)
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'minimum.bin';p.write_bytes(frame(1,0,poll)+frame(8,0,draw))
            # Preserve actual encoding/GPR inventory while rebasing second sequence.
            chunks=literal_frames(p.read_bytes());p.write_bytes(b''.join(stored_frame(h,b,sequence=i) for i,(h,b) in enumerate(chunks)))
            rows=list(wire.iter_records(p));self.assertTrue(all(len(r['payload']['gprs'])==32 for r in rows))


    def test_cli_option_defaults_and_replay_hybrid_refusal(self):
        from unittest.mock import patch
        from test_stadium_recorded_replay import driver
        args=[]
        for name in ('dolphin','disc','profile','menu-recipe','output','build-manifest'):
            args += ['--'+name,'unused-'+name]
        for enabled in (False,True):
            with patch.object(driver,'run',return_value={}) as run, patch('builtins.print'):
                driver.main(args+(['--stadium-idle-3500'] if enabled else []))
            self.assertIs(run.call_args.kwargs['stadium_idle_3500'],enabled)
            self.assertIsNone(run.call_args.kwargs['common_wire_comparator'])
        with patch.object(driver,'validate_reference_build_manifest',return_value={}), \
             patch.object(driver,'menu_actions',return_value=({'scope':'stadium_go_prefix'},'hash')), \
             patch.object(driver.subprocess,'Popen',side_effect=AssertionError('must not launch')):
            with self.assertRaisesRegex(driver.SdDiagnosticError,'fresh recording'):
                driver._run(dolphin='unused',disc='unused',profile='unused',menu_recipe='unused',
                            output='unused',build_manifest='unused',timeout=180,
                            replay_receipt={},stadium_idle_3500=True)

    def test_live_pairing_and_literal_input_refuse_at_first_observation(self):
        from stadium_go_prefix import StadiumIdle3500Observations
        decoder=StadiumIdle3500Observations()
        decoder.setup=True
        h,b=literal_frames(consume(9))[0]
        row=wire._decode_record(h,b,'actual consumed input')
        decoder.accept(row)
        with self.assertRaisesRegex(StadiumGoPrefixError,'duplicate consume'):decoder.accept(row)
        altered=copy.deepcopy(row)
        slot=next(s for s in altered['payload']['slices'] if s['tag']==3)
        raw=bytearray.fromhex(slot['hex']);raw[2]=1;slot['hex']=raw.hex()
        decoder=StadiumIdle3500Observations();decoder.setup=True
        with self.assertRaisesRegex(StadiumGoPrefixError,'literal neutral'):decoder.accept(altered)
        decoder=StadiumIdle3500Observations();decoder.setup=True
        h,b=literal_frames(frame(6,9,[route._slice(2,12)]))[0]
        with self.assertRaisesRegex(StadiumGoPrefixError,'paired consume'):
            decoder.accept(wire._decode_record(h,b,'unpaired source tick'))

    def test_actual_cpp_boundary_progress_default_and_3500(self):
        import subprocess
        source=(ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        begin=source.index('    if (stadium_idle_3500 && setup_ready)')
        end=source.index('    if (boundary == Boundary::SourceTick)\n      CloseCpuProbeAtSourceTick',begin)
        body=source[begin:end]
        enum_begin=source.index('enum class StadiumGoPrefixPhase')
        enum_end=source.index('};',enum_begin)+2
        cap_begin=source.index('    if (stadium_idle_3500 && (next_sequence >= STADIUM_IDLE_RECORD_CAP - 1')
        cap_end=source.index('    const u64 head',cap_begin)
        cap_body=source[cap_begin:cap_end]
        code='''#include <cassert>
#include <cstdint>
#include <string>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;
constexpr u64 STADIUM_IDLE_RECORD_CAP=16384+2*(3500+1+4)+(64*1024*1024)/1238+1;
enum class Boundary {PadPoll,PadConsume,SourceTick,DrawEnter,DrawReturn,Setup};
struct EntityPrefixBoundaryProgress {static constexpr u32 queue_capacity=5;};
'''+source[enum_begin:enum_end]+'''
struct H {
 bool stadium_idle_3500=true,setup_ready=true,stadium_go_prefix_enabled=true;
 bool stadium_idle_pending_consume=false,stadium_idle_c_seen=false,stadium_go_prefix_go_seen=true;
 bool invalid=false,snapshot=false,complete=false;
 u32 stadium_go_prefix_batch_ticks=0,stadium_go_prefix_go_tick=137,stadium_go_prefix_last_tick=137;
 u32 stadium_go_prefix_tail_ticks=0;
 StadiumGoPrefixPhase stadium_go_prefix_phase=StadiumGoPrefixPhase::AwaitContainingTick;
 bool SetInvalid(const std::string&){invalid=true;return false;}
 void run(Boundary boundary,u32 source_tick){
 bool stadium_state_snapshot_this_tick=false,stadium_go_prefix_complete_after_publish=false;
'''+body+'''
 snapshot=stadium_state_snapshot_this_tick;complete=stadium_go_prefix_complete_after_publish;
 }
 void tick(u32 ordinal){run(Boundary::PadConsume,137+ordinal);run(Boundary::SourceTick,137+ordinal);}
};
struct Cap {
 bool stadium_idle_3500=true,stadium_idle_c_seen=false,invalid=false;
 u64 next_sequence=0;
 bool SetInvalid(const std::string&){invalid=true;return false;}
 void* reserve(){
'''+cap_body+'''
 return this;
 }
};
int main(){
 Cap cap;cap.next_sequence=16383;assert(cap.reserve());cap.next_sequence=16384;assert(!cap.reserve());
 cap.stadium_idle_c_seen=true;cap.next_sequence=77600;assert(cap.reserve());
 cap.next_sequence=77601;assert(!cap.reserve());cap.stadium_idle_3500=false;assert(cap.reserve());
 H h;h.tick(0);assert(!h.invalid&&h.snapshot);h.run(Boundary::DrawReturn,137);
 for(u32 i=1;i<=3500;i++){h.tick(i);assert(!h.invalid&&h.snapshot&&!h.complete);
 if(i%5==0&&i<3500){h.run(Boundary::DrawReturn,137+i);assert(!h.complete);}}
 h.run(Boundary::DrawReturn,3637);assert(h.complete&&!h.invalid);
 H missing;missing.run(Boundary::SourceTick,137);assert(missing.invalid);
 H duplicate;duplicate.run(Boundary::PadConsume,137);duplicate.run(Boundary::PadConsume,137);assert(duplicate.invalid);
 H excessive;for(u32 i=0;i<6;i++)excessive.tick(i);assert(excessive.invalid);
 H old;old.stadium_idle_3500=false;old.tick(0);old.run(Boundary::DrawReturn,137);old.tick(1);
 old.run(Boundary::DrawReturn,138);assert(old.complete&&!old.invalid);
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'control.cpp').write_text(code)
            result=subprocess.run(['c++','-std=c++20','-Wall','-Werror',str(p/'control.cpp'),'-o',str(p/'control')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            subprocess.run([str(p/'control')],check=True)


    def test_residual_mandatory_inventory_refuses_before_byte_budget(self):
        from stadium_go_prefix import _validate_idle_3500_boundary_minimum
        def decoded(raw):
            h,b=literal_frames(raw)[0]
            return wire._decode_record(h,b,'mandatory residual row')
        draw=decoded(frame(8,42,match_slices(42)))
        _validate_idle_3500_boundary_minimum(draw)
        q=bytearray(12);q[0]=5;q[8:]=struct.pack('>I',0x81006000)
        poll_slices=[state._slice(t,0,a,raw) for t,a,raw in (
            (1,0x81009000,bytes(48)),(27,0x8100a054,bytes.fromhex('80376a28')),
            (2,0x804c1f78,bytes(q)),(21,0x804c1f84,bytes(856)),
            (22,0x804d7420,bytes(4)),(23,0x804a7f98,bytes(4)),
            (17,0x80479d30,bytes(6)))]
        h,b=literal_frames(frame(1,42,poll_slices))[0];b=bytearray(b)
        struct.pack_into('<I',b,20+31*4,0x81009030);struct.pack_into('<I',b,20+4,0x8100a000)
        poll=decoded(stored_frame(h,bytes(b),replacement_payload=bytes(b)))
        _validate_idle_3500_boundary_minimum(poll)
        for original in (draw,poll):
            for mutation in ('missing','duplicate','size','address','gprs','pc'):
                row=copy.deepcopy(original);slices=row['payload']['slices']
                if mutation=='missing':slices.pop(0)
                if mutation=='duplicate':slices.append(copy.deepcopy(slices[0]))
                if mutation=='size':slices[0]['size']-=1
                if mutation=='address':slices[0]['address']+=4
                if mutation=='gprs':row['payload']['gprs'].pop()
                if mutation=='pc':row['payload']['pc']+=4
                with self.subTest(kind=original['payload']['boundary'],mutation=mutation), \
                     self.assertRaises(StadiumGoPrefixError):_validate_idle_3500_boundary_minimum(row)
