"""Asset-free controls execute the production checked source-owner admission."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp'

class ReferenceEntityProfileTests(unittest.TestCase):
    def test_typed_identity_and_secondary_relationship_decode(self):
        import json
        import struct
        from test_reference_observer_stream import frame, stream
        identity = struct.pack('>III', 2, 14, 1) + bytes([0, 1, 0, 0])
        userdata = bytes.fromhex('80511000')
        raw = identity + userdata
        offset = stream.BOUNDARY.size + 32 * 4 + 2 * stream.SLICE.size
        payload = (stream.BOUNDARY.pack(5, 0, 0x8016e9c4, 32, 2, 0) +
                   struct.pack('<32I', *range(32)) +
                   stream.SLICE.pack(57, 1, 0x80453f10, len(identity), offset) +
                   stream.SLICE.pack(53, 0x101, 0x8050112c, len(userdata), offset + len(identity)) + raw)
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'owners.mwro'
            mode = json.dumps({'entity_profile': 'jiggly-ice-mario-fox-v1'}).encode()
            path.write_bytes(frame(1, 0, mode) + frame(2, 1, mode) + frame(3, 2, payload))
            records = list(stream.iter_records(path))
        slices = records[-1]['payload']['slices']
        self.assertEqual(records[0]['payload']['entity_profile'], 'jiggly-ice-mario-fox-v1')
        self.assertEqual([(row['name'], row['flags'], row['hex']) for row in slices],
                         [('player_identity', 1, identity.hex()),
                          ('player_entity_user_data', 0x101, userdata.hex())])

    def test_actual_profile_startup_gate_and_identity(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            self.skipTest('Native C++ compiler unavailable')
        source = SOURCE.read_text()
        gate = source[source.index('    whole_session_matches = WholeSessionMatchCount();'):
                      source.index('    capture_id = Env("MWRC_CAPTURE_ID");')]
        start = source.index('    if (checked_entity_profile)\n      handshake +=')
        metadata = source[start:source.index('    handshake += "}";', start)]
        harness = r'''
#include <cassert>
#include <map>
#include <string>
std::map<std::string,std::string> environment;
std::string Env(const char* name){return environment[name];}
unsigned WholeSessionMatchCount(){return 0;}
bool SdInitRequested(){return !Env("MWRC_SD_INIT").empty();}
struct Reader {
 unsigned whole_session_matches=0;bool checked_entity_profile=false;std::string error;
 bool SetInvalid(const char* s){error=s;return false;}
 bool configure(){
''' + gate + r'''
return true;}
std::string identity(){std::string handshake="base";
''' + metadata + r'''
return handshake;}
};
int main(){
 Reader r;assert(r.configure());assert(!r.checked_entity_profile);assert(r.identity()=="base");
 environment["MWRC_ENTITY_PROFILE"]="jiggly-ice-mario-fox-v1";
 r=Reader{};assert(r.configure()&&r.checked_entity_profile);
 assert(r.identity()=="base,\"entity_profile\":\"jiggly-ice-mario-fox-v1\"");
 for(const char* other:{"MWRC_SD_INIT","MWRC_CPU_PROBE_OUTPUT","MWRC_ITEM_PROBE_OUTPUT","MWRC_ALLOCATION_OUTPUT"}){
  environment[other]="1";r=Reader{};assert(!r.configure());assert(!r.error.empty());environment.erase(other);
 }
 environment["MWRC_ENTITY_PROFILE"]="unknown";r=Reader{};assert(!r.configure());
}
'''
        scratch_parent = Path(os.environ.get('MELEE_ENTITY_TEST_OUTPUT', ROOT / 'work'))
        scratch_parent.mkdir(parents=True, exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix='entity-startup-', dir=scratch_parent))
        (scratch / 'startup.cpp').write_text(harness)
        try:
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Werror', str(scratch / 'startup.cpp'), '-o', str(scratch / 'startup')], capture_output=True, text=True)
            (scratch / 'compiler.log').write_text(built.stdout + built.stderr)
            self.assertEqual(built.returncode, 0, built.stderr)
            checked = subprocess.run([str(scratch / 'startup')], capture_output=True, text=True)
            (scratch / 'execution.log').write_text(checked.stdout + checked.stderr)
            self.assertEqual(checked.returncode, 0, checked.stderr)
        except BaseException:
            print('Retained entity-startup failure:', scratch)
            raise
        else:
            shutil.rmtree(scratch)

    def test_actual_checked_pair_and_default_admission(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            self.skipTest('Native C++ compiler unavailable')
        source = SOURCE.read_text()
        methods = source[source.index('  bool AddCheckedPlayerEntitySlices('):
                         source.index('  bool AddMatchSlices(')]
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
namespace Core { struct System {}; }
enum class SliceTag { PlayerIdentity, PlayerEntities, PlayerEntityUserData };
u32 ReadBE32(const u8* p) { return u32(p[0])<<24|u32(p[1])<<16|u32(p[2])<<8|p[3]; }
struct Slice { SliceTag tag; u32 address; size_t size; u16 flags; };
struct Reader {
 bool checked_entity_profile=true; u32 active_slot_count=4;
 std::map<u32,u8> memory;
 std::array<u32,4> fighter_pointers{};
 std::array<bool,4> fighter_present{};
 std::array<std::array<u32,2>,4> fighter_entity_pointers{},fighter_entity_kinds{};
 std::array<u8,4> fighter_entity_count{};
 std::vector<Slice> slices;
 std::string error;
 bool IsMem1Range(u32 a,size_t n) const { return a>=0x80000000 && n<=0x81800000-a && a<=0x81800000; }
 bool ReadBytes(Core::System*,u32 a,size_t n,u8* p) const {
  if(!IsMem1Range(a,n))return false;
  for(size_t i=0;i<n;i++){auto it=memory.find(a+i);if(it==memory.end())return false;p[i]=it->second;}
  return true;
 }
 bool ReadU32(Core::System* s,u32 a,u32* p)const {u8 v[4];if(!ReadBytes(s,a,4,v))return false;*p=ReadBE32(v);return true;}
 bool SetInvalid(const char* s){error=s;return false;}
 bool AddSlice(Core::System* s,SliceTag t,u32 a,size_t n,u16 f){
  std::vector<u8> v(n);if(!ReadBytes(s,a,n,v.data()))return false;slices.push_back({t,a,n,f});return true;
 }
 static u16 FighterEntitySliceFlags(u32 slot,u32 ordinal){return u16(slot|(ordinal<<8));}
 void zero(u32 a,size_t n){for(size_t i=0;i<n;i++)memory[a+i]=0;}
 void word(u32 a,u32 n){for(int i=0;i<4;i++)memory[a+i]=u8(n>>(24-i*8));}
''' + methods + r'''
};
Reader valid() {
 Reader r; const u32 characters[4]={15,14,8,2};const u32 kinds[4][2]={{15,0},{10,11},{0,0},{1,0}};
 for(u32 slot=0;slot<6;slot++){
  u32 p=0x80453080+slot*0xe90;r.zero(p,0x10);r.zero(p+0xb0,8);r.word(p+8,slot<4?1:3);
  if(slot>=4)continue;
  r.word(p+4,characters[slot]);r.memory[p+0xd]=1;r.fighter_present[slot]=true;
  r.fighter_entity_count[slot]=slot==1?2:1;
  for(u32 e=0;e<r.fighter_entity_count[slot];e++){
   u32 g=0x80500000+slot*0x1000+e*0x100;u32 f=g+0x10000;
   r.zero(g,0x30);r.zero(f,0x100);r.word(g+0x2c,f);r.word(f,g);r.word(f+4,kinds[slot][e]);r.memory[f+0xc]=slot;
   r.word(p+0xb0+e*4,g);r.fighter_entity_pointers[slot][e]=f;r.fighter_entity_kinds[slot][e]=kinds[slot][e];
  }
  r.fighter_pointers[slot]=r.fighter_entity_pointers[slot][0];
 }
 return r;
}
void rejected(Reader r){Core::System s;assert(!r.AddPlayerEntitySlices(&s,1));assert(!r.error.empty());assert(r.slices.empty());}
int main(){
 Core::System s; Reader r=valid();
 for(u32 slot=0;slot<4;slot++)assert(r.AddPlayerEntitySlices(&s,slot));
 assert(r.slices.size()==13); // 4 headers +4 pairs +5 user-data pointers.
 assert(r.slices[6].tag==SliceTag::PlayerEntityUserData && r.slices[6].flags==0x101);
 Reader d=valid();d.checked_entity_profile=false;assert(!d.AddPlayerEntitySlices(&s,1)); // default refuses Nana.
 d=valid();d.checked_entity_profile=false;assert(d.AddPlayerEntitySlices(&s,0));assert(d.slices.size()==2);
 const u32 p=0x80453080+0xe90; const u32 f=valid().fighter_entity_pointers[1][1];
 r=valid();r.word(p+0xb4,0);rejected(r);
 r=valid();r.word(p+0xb4,0x817ffff0);rejected(r);
 r=valid();r.word(p+0xb4,0x80500000);rejected(r); // cross-slot GObj.
 r=valid();r.word(p+0xb4,0x80501000);rejected(r); // duplicate primary.
 r=valid();r.word(f,0x80500000);rejected(r); // wrong backlink.
 r=valid();r.memory[f+0xc]=0;rejected(r);
 r=valid();r.word(f+4,7);rejected(r); // undeclared transformed fighter.
 r=valid();r.fighter_entity_kinds[1][1]=10;rejected(r);
 r=valid();r.fighter_entity_pointers[1][1]=r.fighter_entity_pointers[0][0];rejected(r);
 r=valid();r.fighter_entity_count[1]=1;rejected(r);
 r=valid();r.fighter_entity_count[0]=2;rejected(r);
 r=valid();r.word(p+4,18);rejected(r);
 r=valid();r.word(p+8,3);rejected(r);
 r=valid();r.memory[p+0xc]=1;r.memory[p+0xd]=0;rejected(r);
 r=valid();r.word(0x80453080+4*0xe90+8,0);rejected(r);
 r=valid();r.word(0x80453080+4*0xe90+0xb0,0x80500000);rejected(r);
 r=valid();r.active_slot_count=3;rejected(r);
 r=valid();r.memory.erase(f+4);rejected(r);
 r=valid();r.fighter_pointers[1]=r.fighter_entity_pointers[0][0];rejected(r);
 r=valid();assert(!r.AddPlayerEntitySlices(&s,4));
}
'''
        scratch_parent = Path(os.environ.get('MELEE_ENTITY_TEST_OUTPUT', ROOT / 'work'))
        scratch_parent.mkdir(parents=True, exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix='entity-profile-', dir=scratch_parent))
        (scratch / 'owners.cpp').write_text(harness)
        try:
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Werror',
                                    str(scratch / 'owners.cpp'), '-o', str(scratch / 'owners')],
                                   capture_output=True, text=True)
            (scratch / 'compiler.log').write_text(built.stdout + built.stderr)
            self.assertEqual(built.returncode, 0, built.stderr)
            checked = subprocess.run([str(scratch / 'owners')], capture_output=True, text=True)
            (scratch / 'execution.log').write_text(checked.stdout + checked.stderr)
            self.assertEqual(checked.returncode, 0, checked.stderr)
        except BaseException:
            print('Retained entity-profile failure:', scratch)
            raise
        else:
            shutil.rmtree(scratch)
