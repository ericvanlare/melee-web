from pathlib import Path
import os, sys, unittest, copy, tempfile, subprocess
ROOT=Path(os.environ.get('MELEE_FIRST_FORM_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'reference-capture/dolphin')]
from stadium_go_prefix import StadiumFirstFormObservations, _validate_stadium_idle_state_record, SOURCE_TICK_PC

# Modeled memory at actual authored member coordinates; these are not captured
# transformed observations. Captured phase5 and failing4432 are pinned by freeze.
def state(phase, form=9, tick=4431):
    old, new=0x80c3d1a0,0x80c44000
    roots={0:0x80c2a900,1:0x80c2c3a0,2:0x80c35280}
    grounds={0:0x80c2a960,1:0x80c2c400,2:0x80c352e0}
    active=5 if phase<6 else form
    roots[active]=old if phase<6 else new; grounds[active]=0x80c3d200 if phase<6 else 0x80c44060
    form=5 if phase==0 else form
    row=dict(seq=tick,event='boundary',source_tick=tick,draw_ordinal=tick-2,
             payload=dict(boundary='source_tick',pc=SOURCE_TICK_PC,source_tick=tick,draw_ordinal=tick-2,slices=[]))
    def add(tag,addr,size,value=0,flags=0):
        raw=value if isinstance(value,bytes) else value.to_bytes(size,'big')
        row['payload']['slices'].append(dict(tag=tag,flags=flags,address=addr,size=size,hex=raw.hex()))
    for i in roots:
        add(67,0x8049e848+i*4,4,roots[i],i);add(68,roots[i]+0x2c,4,grounds[i],i)
    m,d=grounds[2],grounds[1]
    for tag,off,size,value in ((69,0xc4,1,0),(70,0xd8,4,120),(71,0xdc,2,phase),(72,0xde,2,form),(73,0xe0,2,0xffff if phase==0 else 5),(74,0xe2,2,0xffff),(75,0xe4,4,roots[active])):
        add(tag,m+off,size,value)
    for tag,off,size in ((76,0xe0,4),(77,0xe4,2),(78,0xe6,2),(79,0xe8,2),(80,0xea,2),(81,0xee,2),(82,0xf2,2),(83,0xf8,1)):
        add(tag,d+off,size)
    add(84,0x8049f040,256);add(85,0x804d1128,4);add(86,d+0xd4,4)
    incoming=new if phase==5 else 0
    formroot=old if phase==0 else new if phase>=5 else 0
    add(99,m+0xe8,4,incoming);add(100,grounds[active]+0x14,4,active)
    add(102,0x8049e85c,4,old if phase<6 else 0);add(103,0x8049e848+form*4,4,formroot,form)
    if incoming:add(101,0x80c44074,4,form)
    if formroot:
        fg=0x80c3d200 if phase==0 else 0x80c44060
        add(104,formroot+0x2c,4,fg,form);add(105,fg+0x14,4,form,form)
    return row

def changed(row,tag,value,flags=0):
    x=next(x for x in row['payload']['slices'] if x['tag']==tag and x['flags']==flags)
    x['hex']=value.to_bytes(x['size'],'big').hex();return row

class FirstFormTests(unittest.TestCase):
    def prepared(self):
        v=StadiumFirstFormObservations()
        for i in range(6):v.accept(state(i))
        return v
    def test_actual_decoder_phase5_to6_and_stable_form(self):
        v=self.prepared(); after=state(6,tick=4432)
        self.assertTrue(v.accept(after)['first_form']['root_replacement_observed'])
        self.assertFalse(_validate_stadium_idle_state_record(after,'IDLE',4432,_active_map_id=9)['map2']['map5_root_pointer_equals_fixed_map5_slot_value'])
        # case6 executes once, then actual source resets phase0 without changing form.
        resting=changed(changed(state(6,tick=4433),71,0),70,300)
        v.accept(resting)
        with self.assertRaises(ValueError):v.accept(state(1,tick=4434))
    def test_foreign_roots_missing_provenance_and_resurrection_refused(self):
        for row in (changed(state(6),75,0x80c45000),changed(state(6),102,0x80c3d1a0),changed(state(6),100,3),changed(state(6),67,0x80c45000,0)):
            with self.assertRaises(ValueError):self.prepared().accept(row)
        with self.assertRaises(ValueError):StadiumFirstFormObservations().accept(state(6))
        v=StadiumFirstFormObservations();v.accept(state(0))
        with self.assertRaises(ValueError):v.accept(state(5))
    def test_legacy_decoder_default_and_transformed_refusal(self):
        row=state(0);row['payload']['slices']=[x for x in row['payload']['slices'] if x['tag']<99]
        self.assertTrue(_validate_stadium_idle_state_record(row,'IDLE',4431)['map2']['map5_root_pointer_equals_fixed_map5_slot_value'])
        with self.assertRaises(ValueError):_validate_stadium_idle_state_record(state(6),'IDLE',4431)
    def test_exact_inventory_and_pointer_members(self):
        for tag in (99,100,102,103):
            row=state(0);row['payload']['slices']=[x for x in row['payload']['slices'] if x['tag']!=tag]
            with self.assertRaises(ValueError):StadiumFirstFormObservations().accept(row)
        row=state(0);next(x for x in row['payload']['slices'] if x['tag']==100)['address']+=4
        with self.assertRaises(ValueError):StadiumFirstFormObservations().accept(row)
    def test_live_decoder_switches_only_after_actual_window_closure(self):
        import ast
        from types import SimpleNamespace
        tree=ast.parse((ROOT/'scripts/capture_sd_reference_prefix.py').read_text())
        expressions=[node.value for node in ast.walk(tree) if isinstance(node,ast.Assign)
            and any(isinstance(x,ast.Name) and x.id=='snapshot' for x in node.targets)
            and 'self.first_form.accept' in ast.unparse(node.value)]
        self.assertEqual(len(expressions),1)
        code=compile(ast.Expression(expressions[0]),'<actual-live-decoder-selection>','eval')
        called=[]
        owner=SimpleNamespace(stadium_whole_post_transform_standings=True,
            long_idle_window_complete=False,
            first_form=SimpleNamespace(accept=lambda row:called.append('first_form')))
        scope=dict(self=owner,row=dict(source_tick=3586),
            _validate_stadium_idle_state_record=lambda *args:called.append('legacy'))
        eval(code,scope);owner.long_idle_window_complete=True;eval(code,scope)
        self.assertEqual(called,['legacy','first_form'])

    def test_binary_profile_announcements_and_unannounced_slices_refused(self):
        import reference_observer_stream as stream
        import json, zlib, struct
        def frame(event,seq,payload):
            return stream.HEADER.pack(stream.MAGIC_U32,stream.SCHEMA_VERSION,event,seq,seq,0,0,0,len(payload),zlib.crc32(payload)&0xffffffff)+payload
        announcement=dict(whole_session=True,match_count=3,stadium_whole_feedback=True,
            stadium_whole_b_state=True,stadium_observed_match=1,
            stadium_whole_long_idle=dict(profile='selected_b_3500_v1'),
            stadium_posttransform_state='first_form_v1')
        with tempfile.TemporaryDirectory(dir=os.environ.get('MELEE_TEST_SCRATCH')) as tmp:
            p=Path(tmp)/'raw.mwro'
            def decoded(a,b):
                p.write_bytes(frame(1,0,json.dumps(a).encode())+frame(2,1,json.dumps(b).encode()))
                return list(stream.iter_records(p))
            self.assertEqual(len(decoded(announcement,announcement)),2)
            bad=dict(announcement,stadium_posttransform_state='unknown')
            with self.assertRaises(ValueError):decoded(bad,bad)
            bad=dict(announcement);bad.pop('stadium_posttransform_state')
            with self.assertRaises(ValueError):decoded(announcement,bad)
            # A legacy stream cannot smuggle the new pointer observations.
            payload=stream.BOUNDARY.pack(6,0,0,32,1,0)+struct.pack('<32I',*([0]*32))
            offset=len(payload)+stream.SLICE.size
            payload+=stream.SLICE.pack(99,0,0x80001000,4,offset)+bytes(4)
            p.write_bytes(frame(1,0,json.dumps(bad).encode())+frame(3,1,payload))
            with self.assertRaises(ValueError):list(stream.iter_records(p))

    def test_compiled_actual_observer_transition_and_admission(self):
        source=(ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        a=source.index('struct StadiumFirstFormObservation\n');b=source.index('\nstruct SliceRef',a)
        actual=source[a:b]
        main=r'''
int main(){
 StadiumFirstFormObservation s;
 assert(s.Observe(0,5,65535,65535,100,100,0,100,5,0));
 for(unsigned p=1;p<5;p++)assert(s.Observe(p,9,5,65535,100,100,0,0,5,0));
 assert(s.Observe(5,9,5,65535,100,100,200,200,5,9));
 auto old=s;assert(!old.Observe(6,9,5,65535,0,300,0,300,9,0));
 assert(s.Observe(6,9,5,65535,0,200,0,200,9,0));
 assert(s.Observe(0,9,5,65535,0,200,0,200,9,0));
 assert(!s.Observe(1,5,9,5,0,200,0,0,9,0));
 assert(StadiumPostTransformAdmit("",false,false,false,""));
 assert(StadiumPostTransformAdmit("first_form_v1",true,true,false,"selected_b_3500_v1"));
 assert(!StadiumPostTransformAdmit("first_form_v1",true,true,true,"selected_b_3500_v1"));
 assert(!StadiumPostTransformAdmit("unknown",true,true,false,"selected_b_3500_v1"));
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ.get('MELEE_TEST_SCRATCH')) as tmp:
            path=Path(tmp);(path/'test.cpp').write_text('#include <cstdint>\n#include <string>\n#include <cassert>\nusing u16=uint16_t;using u32=uint32_t;\n'+actual+main)
            compiled=subprocess.run(['/usr/bin/clang++','-std=c++17','-Wall','-Wextra','-Werror',str(path/'test.cpp'),'-o',str(path/'test')],text=True,capture_output=True)
            self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
            ran=subprocess.run([str(path/'test')],text=True,capture_output=True)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
