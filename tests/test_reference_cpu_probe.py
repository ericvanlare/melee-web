"""Compile the observer's opt-in configuration and serialization boundaries."""
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get("MELEE_RNG_OWNER_BASE_ROOT", Path(__file__).resolve().parents[1]))
SOURCE = Path(os.environ.get("MELEE_RNG_OWNER_OBSERVER_SOURCE", ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"))


class ReferenceCpuProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            raise unittest.SkipTest("A native C++ compiler is required")
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        source = SOURCE.read_text()
        constants = source[source.index("constexpr size_t CPU_PROBE_TICK_WINDOW_MAX"):
                           source.index("constexpr std::array<u8, 32> EXPECTED_DOL_SHA256_BYTES")]
        constants = "\n".join(line for line in constants.splitlines()
                              if "CPU_PROBE_TICK_WINDOW_MAX" in line or
                              "CPU_PROBE_MAX_JSON_BYTES" in line)
        helpers = source[source.index("bool SelectedRngReturn"):
                         source.index("}  // namespace", source.index("bool SelectedRngReturn"))]
        stream_policy = source[source.index("enum class Event"):
                               source.index("enum class Boundary")]
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t; using u64 = uint64_t;
constexpr u32 WHOLE_SESSION_MAX_MATCHES = 64;
std::string Env(const char* key) { const char* value = std::getenv(key); return value ? value : ""; }
bool ActivationRequested() { return Env("MWRC_ENABLE") == "1"; }
''' + constants + stream_policy + helpers + r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string action(argv[1]);
  if (action == "stream-policy") {
    assert(ShouldWriteObserverEvent(Event::Handshake, true));
    assert(ShouldWriteObserverEvent(Event::Start, true));
    assert(!ShouldWriteObserverEvent(Event::Boundary, true));
    assert(!ShouldWriteObserverEvent(Event::Progress, true));
    assert(ShouldWriteObserverEvent(Event::Boundary, false));
    assert(ShouldWriteObserverEvent(Event::Progress, false));
    return 0;
  }
  if (action.rfind("item-", 0) == 0) {
    const auto& settings = ItemProbeEnvironment();
    if (action == "item-absent") assert(!settings.present && !ItemProbeEnabled());
    else if (action == "item-invalid")
      assert(settings.present && !settings.valid && !ItemProbeEnabled() && !settings.error.empty());
    else if (action == "item-disabled") assert(settings.valid && !ItemProbeEnabled());
    else if (action == "item-enabled") assert(settings.valid && ItemProbeEnabled());
    else assert(false);
    return 0;
  }
  if (action == "helpers") {
    assert(std::string(CpuProbeOwnerCallbackSymbol(0,0x8005c9a4)) == "null");
    assert(std::string(CpuProbeOwnerCallbackSymbol(1,0x8005c9a4)) == "\"efLib_particles_proc_main\"");
    assert(std::string(CpuProbeOwnerCallbackSymbol(1,0x801d1390)) == "\"grStadium_801D1390\"");
    assert(std::string(CpuProbeOwnerCallbackSymbol(1,0x8006a360)) == "\"Fighter_8006A360\"");
    assert(std::string(CpuProbeOwnerCallbackSymbol(1,0x8006a364)) == "\"unknown\"");
    assert(std::string(CpuProbeOwnerCallbackSymbol(1,0x8005c9a8)) == "\"unknown\"");
    u32 result = 42;
    for (const auto bad : {"", "-1", "+1", "1 ", " 1", "0x10", "1.0", "4294967296"})
      assert(!ParseBoundedDecimal(bad, UINT32_MAX, &result));
    assert(ParseBoundedDecimal("4294967295", UINT32_MAX, &result) && result == UINT32_MAX);
    assert(ParseBoundedDecimal("0", 0, &result) && result == 0);
    assert(!ParseBoundedDecimal("1", 0, &result));
    assert(!ParseBoundedDecimal("64", 63, &result));
    assert(ParseBoundedDecimal("63", 63, &result) && result == 63);
    std::string output;
    assert(AppendHex(&output, 0xfedcba9876543210ULL, 16));
    assert(output == "fedcba9876543210");
    assert(!AppendHex(&output, 0, 17));
    const u8 bytes[] = {0, 0x80, 0xff};
    assert(AppendHexBytes(&output, bytes, 3));
    assert(output == "fedcba98765432100080ff");
    output.assign(CPU_PROBE_MAX_JSON_BYTES - 1, 'x');
    assert(!AppendHexBytes(&output, bytes, 1));
    assert(AppendBounded(&output, "y"));
    assert(!AppendBounded(&output, "z"));
    assert(output.back() == 'y');
    for (const auto& point : CPU_PROBE_POINTS)
      assert(FindCpuProbePoint(point.address) == &point);
    assert(!FindCpuProbePoint(0x80000000));
    return 0;
  }
  const auto& settings = CpuProbeEnvironment();
  if (action == "absent") assert(!settings.present && !CpuProbeEnabled());
  else if (action == "invalid") assert(settings.present && !settings.valid && !CpuProbeEnabled() && !settings.error.empty());
  else if (action == "disabled") assert(settings.valid && !CpuProbeEnabled());
  else if (action == "enabled") assert(settings.valid && CpuProbeEnabled());
  else if (action == "rng-returns") {
    assert(settings.valid && settings.rng_returns && settings.rng_return_pc == 0);
    assert(SelectedRngReturn(0, true, 0x80380524));
    assert(SelectedRngReturn(0, true, 0x8038057c));
    assert(!settings.rng_updates);
    assert(!SelectedRngReturn(0, true, 0x803805b8));
    assert(!SelectedRngReturn(0, true, 0x80380528));
    assert(!SelectedRngReturn(0x80380524, false, 0x8038057c));
  }
  else if (action == "rng-updates") {
    assert(settings.valid && settings.rng_returns && settings.rng_updates && settings.rng_return_pc == 0);
    for(u32 pc:{0x80380524U,0x8038057cU,0x803805b8U}) assert(SelectedRngReturn(0,true,pc,true));
    assert(!SelectedRngReturn(0,true,0x803805bc,true));
    assert(std::string(CombinedRngSelection(true)).find("randi") == std::string::npos);
    assert(std::string(CombinedRngSelection(true)).find("0x803805b8") != std::string::npos);
    assert(std::string(CombinedRngSelection(false)).find("0x803805b8") == std::string::npos);
  }
  else if (action == "rand-return") assert(settings.valid && settings.rng_return_site == "rand_return" && settings.rng_return_pc == 0x80380524);
  else if (action == "randf-return") assert(settings.valid && settings.rng_return_site == "randf_return" && settings.rng_return_pc == 0x8038057c);
  else assert(false);
}
'''
        path = directory / "probe.cpp"
        path.write_text(harness)
        cls.binary = directory / "probe"
        built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror", str(path),
                                "-o", str(cls.binary)], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(built.stderr)

    def run_probe(self, action, **settings):
        environment = {k: v for k, v in os.environ.items() if not k.startswith("MWRC_")}
        environment.update(settings)
        result = subprocess.run([str(self.binary), action], env=environment,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_native_emitter_with_source_declared_scheduler_context(self):
        # Actual declarations/bodies, modeled dispatcher state. This does not
        # execute HSD dispatch or prove object generation/lifetime.
        native = Path(os.environ.get("MELEE_RNG_OWNER_NATIVE_SOURCE", ROOT / "src/gameplay_rng_draw_probe.c")).read_text()
        if not (ROOT / "build/gameplay-source/src/sysdolphin/baselib/gobj.h").is_file():
            self.skipTest("Prepared actual HSD declarations are required for the native owner boundary control")
        gobj = (ROOT / "build/gameplay-source/src/sysdolphin/baselib/gobj.h").read_text()
        proc = (ROOT / "build/gameplay-source/src/sysdolphin/baselib/gobjproc.h").read_text()
        gobj = gobj[gobj.index("typedef struct HSD_GObj {"):gobj.index("} HSD_GObj;") + len("} HSD_GObj;")]
        proc = proc[proc.index("struct HSD_GObjProc {"):proc.index("};") + 2]
        prefix = '''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint64_t u64;
typedef struct HSD_GObj HSD_GObj;
typedef struct HSD_GObjProc HSD_GObjProc;
typedef void (*HSD_GObjEvent)(HSD_GObj*);
typedef void (*GObj_RenderFunc)(HSD_GObj*, int);
''' + proc + gobj + '''
/* Modeled field owners; the affected target build checks real Fighter/Player declarations. */
typedef struct { HSD_GObj* gobj; unsigned kind; u8 player_id; } Fighter;
typedef struct { HSD_GObj* player_entity[2]; } StaticPlayer;
static StaticPlayer players[6];
StaticPlayer* Player_GetPtrForSlot(int slot) { assert(slot>=0 && slot<6); return &players[slot]; }
HSD_GObj* HSD_GObj_804D781C;
HSD_GObjProc* HSD_GObj_804D7838;
void grStadium_801D1390(HSD_GObj* o) { (void)o; }
void efLib_particles_proc_main(HSD_GObj* o) { (void)o; }
void Fighter_8006A360(HSD_GObj* o) { (void)o; }
enum { MELEE_WEB_RNG_DRAW_RAND=0, MELEE_WEB_RNG_DRAW_RANDF=1 };
'''
        native = native.replace("#include <melee/ef/eflib.h>\n", "")
        native = native.replace("#include <melee/ft/fighter.h>\n", "")
        native = native.replace("#include <melee/ft/types.h>\n", "")
        native = native.replace("#include <melee/pl/player.h>\n", "")
        native = native.replace("#include <melee/gr/grpstadium.h>\n", "")
        native = re.sub(r'^#include "gameplay_cpu_observation.h"\n', '', native)
        native = re.sub(r'^#include <sysdolphin/baselib/(gobj|gobjproc).h>\n', '', native, flags=re.M)
        harness = prefix + native + '''
static void callback(HSD_GObj* o) { (void)o; }
int main(int argc, char** argv) {
  assert(argc == 2);
  HSD_GObj gobj = {0}; HSD_GObjProc proc = {0};
  Fighter fighter = {&gobj, 0, 1};
  gobj.user_data = &fighter; players[1].player_entity[0] = &gobj;
  gobj.classifier=13; gobj.p_link=4; proc.s_link=7; proc.gobj=&gobj;
  if(strcmp(argv[1],"particle")==0) {gobj.classifier=8;gobj.p_link=11;proc.s_link=15;proc.on_invoke=efLib_particles_proc_main;}
  else if(strncmp(argv[1],"fighter",7)==0) {gobj.classifier=4;gobj.p_link=8;proc.s_link=1;proc.on_invoke=Fighter_8006A360;}
  else if(strcmp(argv[1],"unknown")==0) proc.on_invoke=callback;
  else proc.on_invoke=grStadium_801D1390;
  rng_draw_probe_active=1; rng_draw_probe_cursor=12;
  rng_draw_probe_owners=strcmp(argv[1],"legacy")!=0;
  rng_draw_probe_fighter_owners=strncmp(argv[1],"fighter-v3",10)==0;
  if(strcmp(argv[1],"fighter-v3-entity1")==0) {
    players[1].player_entity[0]=NULL; players[1].player_entity[1]=&gobj;
  }
  if(strcmp(argv[1],"fighter-v3-null")==0) gobj.user_data=NULL;
  if(strcmp(argv[1],"fighter-v3-link")==0) fighter.gobj=NULL;
  if(strcmp(argv[1],"fighter-v3-slot")==0) fighter.player_id=6;
  if(strcmp(argv[1],"fighter-v3-unregistered")==0) players[1].player_entity[0]=NULL;
  if(strcmp(argv[1],"fighter-v3-duplicate")==0) players[1].player_entity[1]=&gobj;
  if(strcmp(argv[1],"fighter-v3-tuple")==0) proc.s_link=2;
  melee_web_cpu_observation_rng_draw(0x12345678,0);
  if(strcmp(argv[1],"partial")==0) HSD_GObj_804D781C=&gobj;
  else { HSD_GObj_804D781C=&gobj; HSD_GObj_804D7838=&proc; }
  if(strcmp(argv[1],"backpointer")==0) proc.gobj=NULL;
  melee_web_cpu_observation_rng_draw(0x23456789,1);
  if(rng_draw_probe_owners) {
    assert(rng_draw_probe_entries[0].proc==0);
    assert(rng_draw_probe_entries[1].classifier==gobj.classifier);
    assert(rng_draw_probe_entries[1].p_link==gobj.p_link);
    assert(rng_draw_probe_entries[1].s_link==proc.s_link);
  }
  if(strcmp(argv[1],"capacity")==0) {
    while(rng_draw_probe_count<RNG_DRAW_PROBE_MAX_PER_FRAME)
      melee_web_cpu_observation_rng_draw(0xffffffff,1);
    assert(rng_draw_probe_count==1024);
    for(size_t i=1;i<1024;i++) {
      rng_draw_probe_entries[i].gobj=UINTPTR_MAX;
      rng_draw_probe_entries[i].proc=UINTPTR_MAX;
      rng_draw_probe_entries[i].classifier=65535;
      rng_draw_probe_entries[i].p_link=255;
      rng_draw_probe_entries[i].s_link=255;
    }
  }
  melee_web_rng_draw_probe_scheduler_return();
  return 0;
}
'''
        path = self.binary.parent / "native-owner.c"
        path.write_text(harness)
        compiler = shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(compiler)
        binary = path.with_suffix("")
        built = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Wno-unused-variable", str(path), "-o", str(binary)], capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr)
        for mode in ("legacy", "owners", "particle", "fighter", "unknown", "capacity", "fighter-v3", "fighter-v3-entity1"):
            result = subprocess.run([str(binary), mode], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            row = json.loads(result.stderr.removeprefix("RNG_DRAW_PROBE "))
            self.assertEqual(row["version"], 1 if mode == "legacy" else 3 if mode.startswith("fighter-v3") else 2)
            self.assertEqual([x["seed_after"] for x in row["draws"][:2]], ["12345678","23456789"])
            self.assertEqual([x["kind"] for x in row["draws"][:2]], ["HSD_Rand","HSD_Randf"])
            if mode == "legacy": self.assertNotIn("owner", row["draws"][0])
            else:
                self.assertEqual(row["draws"][0]["owner"]["status"], "outside_proc")
                self.assertEqual(row["draws"][1]["owner"]["status"], "scheduled_proc")
                if mode == "particle":
                    self.assertEqual(row["draws"][1]["owner"]["callback_symbol"], "efLib_particles_proc_main")
                    self.assertEqual((row["draws"][1]["owner"]["classifier"],
                                      row["draws"][1]["owner"]["p_link"],
                                      row["draws"][1]["owner"]["s_link"]), (8, 11, 15))
                elif mode.startswith("fighter"):
                    self.assertEqual(row["draws"][1]["owner"]["callback_symbol"], "Fighter_8006A360")
                    self.assertEqual((row["draws"][1]["owner"]["classifier"],
                                      row["draws"][1]["owner"]["p_link"],
                                      row["draws"][1]["owner"]["s_link"]), (4, 8, 1))
                elif mode == "unknown":
                    self.assertEqual(row["draws"][1]["owner"]["callback_symbol"], "unknown")
                else:
                    self.assertEqual(row["draws"][1]["owner"]["callback_symbol"], "grStadium_801D1390")
                if mode.startswith("fighter-v3"):
                    self.assertIsNone(row["draws"][0]["owner"]["fighter"])
                    self.assertEqual(row["draws"][1]["owner"]["fighter"],
                        {"slot":1,"entity":int(mode.endswith("entity1")),"kind":0})
                else:
                    self.assertNotIn("fighter",row["draws"][1]["owner"])
                if mode == "capacity": self.assertEqual(len(row["draws"]),1024)
        for mode in ("partial", "backpointer", "fighter-v3-null", "fighter-v3-link", "fighter-v3-slot", "fighter-v3-unregistered", "fighter-v3-duplicate", "fighter-v3-tuple"):
            result = subprocess.run([str(binary), mode], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("RNG_DRAW_PROBE", result.stderr)

    def test_actual_guest_owner_block_checks_spans_and_backpointer(self):
        source = SOURCE.read_text()
        start = source.index("    if (cpu_probe_rng_returns)\n", source.index("  void RecordCpuProbe("))
        body = source[start:source.index("    if (samus_effect_bank_load_probe)", start)]
        harness = r'''
#include <array>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
u32 gobj=0,proc=0; bool invalid=false, readable=true; unsigned reads=0;
std::array<u8,4> gb{}; std::array<u8,24> pb{};
u64 next_sequence=123;u32 draw_ordinal=7;
struct {std::array<u8,8> random_seed_and_pointer{};u32 seed_owner_pointer=0,seed_after=0;u64 observer_next_sequence=0;u32 rng_draw_ordinal=0;u32 owner_gobj=0,owner_proc=0,owner_callback=0;u16 owner_classifier=0;u8 owner_p_link=0,owner_s_link=0;} record;
bool cpu_probe_rng_returns=true;
void SetInvalid(const char*) {invalid=true;}
bool IsMem1Range(u32 a, size_t n) {return a>=0x80000000 && uint64_t(a)+n<=0x81800000;}
bool ReadU32(void*,u32 a,u32* v) {if(a==0x804d781c)*v=gobj;else if(a==0x804d7838)*v=proc;else if(a==0x80412000)*v=0xabcdef01;else return false;return true;}
bool ReadMem1(void*,u32 a,size_t n,u8* out) {
  ++reads; if(!readable||!IsMem1Range(a,n))return false;
  if(a==gobj&&n==4)std::memcpy(out,gb.data(),n);
  else if(a==proc&&n==24)std::memcpy(out,pb.data(),n);else return false;return true;
}
void inspect() {
void* system=nullptr;
''' + body + r'''
}
void reset() {record={};record.random_seed_and_pointer={0,0,0,0,0x80,0x41,0x20,0};invalid=false;reads=0;readable=true;gb={};pb={};gobj=0;proc=0;}
void be32(size_t o,u32 value) {for(int i=0;i<4;i++)pb[o+i]=value>>(24-8*i);}
void scheduled() {gobj=0x80410000;proc=0x80411000;gb={0,13,4,0};pb[12]=7;be32(16,gobj);be32(20,0x801d1390);}
int main() {
 reset();inspect();assert(!invalid&&reads==0&&record.owner_proc==0);
 assert(record.observer_next_sequence==123&&record.rng_draw_ordinal==7&&record.seed_after==0xabcdef01&&record.seed_owner_pointer==0x80412000);
 reset();scheduled();inspect();assert(!invalid&&reads==2&&record.owner_classifier==13&&record.owner_p_link==4&&record.owner_s_link==7&&record.owner_callback==0x801d1390);
 reset();gobj=0x80410000;inspect();assert(invalid&&reads==0);
 reset();scheduled();proc++;inspect();assert(invalid&&reads==0);
 reset();scheduled();be32(16,gobj+4);inspect();assert(invalid&&record.owner_callback==0);
 reset();scheduled();readable=false;inspect();assert(invalid);
 reset();scheduled();be32(20,0x1000);inspect();assert(invalid);
 reset();record.random_seed_and_pointer.fill(0);inspect();assert(invalid);
 reset();record.random_seed_and_pointer[7]=1;inspect();assert(invalid);
 reset();record.random_seed_and_pointer[4]=0x10;inspect();assert(invalid);
 reset();record.random_seed_and_pointer[6]=0x30;inspect();assert(invalid);
}
'''
        path = self.binary.parent / "guest-owner.cpp"
        path.write_text(harness)
        compiler = shutil.which("clang++") or shutil.which("g++")
        binary = path.with_suffix("")
        result = subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror",str(path),"-o",str(binary)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        result = subprocess.run([str(binary)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_particle_callback_mapping_is_source_backed(self):
        symbols = (ROOT / '.deps/melee/config/GALE01/symbols.txt').read_text()
        source = (ROOT / '.deps/melee/src/melee/ef/eflib.c').read_text()
        self.assertIn('efLib_particles_proc_main = .text:0x8005C9A4;', symbols)
        self.assertIn('gobj = GObj_Create(8U, 0xBU, 1U);', source)
        self.assertIn('HSD_GObj_SetupProc(gobj, efLib_particles_proc_main, 0xFU);', source)

    def test_fighter_callback_mapping_is_source_backed(self):
        symbols = (ROOT / '.deps/melee/config/GALE01/symbols.txt').read_text()
        source = (ROOT / '.deps/melee/src/melee/ft/fighter.c').read_text()
        header = (ROOT / '.deps/melee/src/melee/ft/fighter.h').read_text()
        self.assertIn('Fighter_8006A360 = .text:0x8006A360;', symbols)
        self.assertIn('gobj = GObj_Create(HSD_GOBJ_CLASS_FIGHTER, 8, 0);', source)
        self.assertIn('HSD_GObj_SetupProc(gobj, &Fighter_8006A360, 1);', source)
        self.assertIn('void Fighter_8006A360(Fighter_GObj* gobj);', header)

    def test_compiled_bounds_and_bit_serialization(self):
        self.run_probe("helpers")

    def test_item_probe_summary_stream_is_opt_in_and_keeps_full_mode(self):
        self.run_probe("stream-policy")
        source = SOURCE.read_text()
        self.assertIn('Env("MWRC_ITEM_PROBE_SUMMARY_STREAM")', source)
        self.assertIn('MWRC_ITEM_PROBE_SUMMARY_STREAM requires a configured item probe', source)
        self.assertIn('ShouldWriteObserverEvent(slot.event, item_probe_summary_stream)', source)

    def test_opt_in_requires_complete_bounded_configuration(self):
        self.run_probe("absent")
        good = dict(MWRC_ENABLE="1", MWRC_CPU_PROBE_OUTPUT="probe.json",
                    MWRC_CPU_PROBE_MATCH="0", MWRC_CPU_PROBE_FIRST_TICK="100",
                    MWRC_CPU_PROBE_LAST_TICK="163")
        self.run_probe("enabled", **good)
        self.run_probe("disabled", **dict(good, MWRC_ENABLE="0"))
        self.run_probe("rng-returns", **dict(good, MWRC_CPU_PROBE_RNG_RETURN_SITE="rng_returns"))
        self.run_probe("rng-updates", **dict(good, MWRC_CPU_PROBE_RNG_RETURN_SITE="rng_updates"))
        self.run_probe("rand-return", **dict(good, MWRC_ENABLE="1",
                                               MWRC_CPU_PROBE_RNG_RETURN_SITE="rand_return"))
        self.run_probe("randf-return", **dict(good, MWRC_ENABLE="1",
                                                MWRC_CPU_PROBE_RNG_RETURN_SITE="randf_return"))
        for patch in ({"MWRC_CPU_PROBE_OUTPUT": ""}, {"MWRC_CPU_PROBE_MATCH": "64"},
                      {"MWRC_CPU_PROBE_FIRST_TICK": "-1"}, {"MWRC_CPU_PROBE_LAST_TICK": "99"},
                      {"MWRC_CPU_PROBE_LAST_TICK": "164"}, {"MWRC_CPU_PROBE_MATCH": ""},
                      {"MWRC_CPU_PROBE_LAST_TICK": "4294967296"},
                      {"MWRC_CPU_PROBE_RNG_RETURN_SITE": "0x80380524"}):
            with self.subTest(patch=patch):
                self.run_probe("invalid", **dict(good, **patch))

    def test_item_boundary_probe_requires_complete_bounded_configuration(self):
        self.run_probe("item-absent")
        good = dict(MWRC_ENABLE="1", MWRC_ITEM_PROBE_OUTPUT="item-probe.json",
                    MWRC_ITEM_PROBE_MATCH="1", MWRC_ITEM_PROBE_FIRST_TICK="11144",
                    MWRC_ITEM_PROBE_LAST_TICK="11159")
        self.run_probe("item-enabled", **good)
        self.run_probe("item-disabled", **dict(good, MWRC_ENABLE="0"))
        for patch in (
            {"MWRC_ITEM_PROBE_OUTPUT": ""},
            {"MWRC_ITEM_PROBE_MATCH": "64"},
            {"MWRC_ITEM_PROBE_FIRST_TICK": "-1"},
            {"MWRC_ITEM_PROBE_LAST_TICK": "11143"},
            {"MWRC_ITEM_PROBE_LAST_TICK": "11208"},
            {"MWRC_ITEM_PROBE_MATCH": ""},
            {"MWRC_ITEM_PROBE_LAST_TICK": "4294967296"},
        ):
            with self.subTest(patch=patch):
                self.run_probe("item-invalid", **dict(good, **patch))

    def test_compiled_probe_inventory_matches_verified_profile(self):
        source = SOURCE.read_text()
        found = re.findall(r'\{"([a-z0-9_]+)", (0x[0-9a-f]+), (0x[0-9a-f]+)\}', source)
        expected = json.loads((ROOT / "tools/cpu-register-gale01r2.json").read_text())["probes"]
        self.assertEqual([(label, int(pc, 16), int(word, 16)) for label, pc, word in found],
                         [(row["label"], int(row["address"], 16), int(row["expected_word"], 16))
                          for row in expected])

    def test_item_boundary_inventory_matches_pinned_profile(self):
        source = SOURCE.read_text()
        found = re.findall(
            r'\{"([a-z0-9_]+)", (0x[0-9a-f]+), (0x[0-9a-f]+), '
            r'ItemProbeEvent::([A-Za-z]+), ([0-9]+)\}',
            source,
        )
        expected = json.loads(
            (ROOT / "tools/cpu-item-boundary-gale01r2.json").read_text()
        )["points"]
        self.assertEqual(
            [(label, int(pc, 16), int(word, 16), event, int(pair))
             for label, pc, word, event, pair in found],
            [(row["label"], int(row["address"], 16), int(row["expected_word"], 16),
              row["event"], row["pair"])
             for row in expected],
        )
