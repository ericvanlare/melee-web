"""Extracted native guard/serializer controls with synthetic memory and sink.

This compiles actual observer methods in a small isolated harness. It is not
Dolphin validation and cannot reconstruct the unrecorded rejected MatchEnd.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"


class OrdinaryTerminalGuardTests(unittest.TestCase):
    def test_extracted_native_guard_and_failure_only_typed_serialization(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("A native C++ compiler is not installed")
        source = SOURCE.read_text()
        methods = source[source.index("  void SdEvent("):source.index("  void ObserveSdInit(")]
        hex_method = source[source.index("bool AppendHexBytes("):source.index("\n}  // namespace", source.index("bool AppendHexBytes("))]
        terminal = source[source.index('if (pc == 0x8016ebbc && sd_init.phase'):source.index('// Original outcome and participant decision')]
        condition = re.search(r"if \((raw\[base\].*?)\)\n\s+return OrdinaryTerminalFailure", terminal, re.S).group(1)
        self.assertEqual(terminal.count('return OrdinaryTerminalFailure('), 3)
        self.assertNotIn('InputStream::RequestFinish', methods + terminal)
        harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
constexpr size_t CPU_PROBE_MAX_JSON_BYTES=1024*1024;
enum class Event { Progress, Error };
enum class SliceTag : u16 { Result=15, Clock=14 };
''' + hex_method + r'''
struct Reader {
  struct Slice { SliceTag tag; u16 flags; u32 address; size_t offset,size; };
  struct { unsigned consumed=29038; } sd_init;
  unsigned sd_menu_consumed=751;
  std::array<u8,65536> raw{};
  std::vector<Slice> slices;
  size_t slice_count=0;
  std::vector<Event> events;
  std::vector<std::string> output;
  std::string error;
  bool OrdinaryTimeoutRequested() const {return true;}
  void SetInvalid(std::string reason) {if(error.empty()) error=reason;}
  void PushJson(Event event,const std::string& json,u32,u32) {
    events.push_back(event); output.push_back(json);
  }
''' + methods + r'''
  bool Participant(unsigned slot) const {
    size_t base=0x58+slot*0xa8;
    return !(''' + condition + r''');
  }
};
int main() {
  Reader r;
  for(unsigned slot=0;slot<2;++slot) {
    size_t base=0x58+slot*0xa8;
    r.raw[base+1]=8; r.raw[base+3]=(slot==0?1:0)<<2;
    r.raw[base+5]=slot==0?1:0; r.raw[base+8]=slot==0?3:4;
    assert(r.Participant(slot));
    for(size_t offset : {size_t(0),size_t(1),size_t(3),size_t(5),size_t(8),size_t(12),size_t(13)}) {
      u8 saved=r.raw[base+offset]; r.raw[base+offset]^=4;
      assert(!r.Participant(slot)); r.raw[base+offset]=saved;
    }
  }
  for(auto ranks : {std::array<u8,2>{0,0},std::array<u8,2>{0,1},std::array<u8,2>{1,1}}) {
    r.raw[0x5d]=ranks[0]; r.raw[0x105]=ranks[1];
    assert(!r.Participant(0)||!r.Participant(1));
  }
  // These bytes intentionally represent a rejected synthetic terminal, not
  // the missing current-capture MatchEnd. Preserve them exactly in the error.
  r.slices={{SliceTag::Result,0,0x80479da4,0,0x448},
            {SliceTag::Clock,0,0x8046b6a0,0x448,46}};
  r.slice_count=r.slices.size();
  r.OrdinaryTerminalFailure("synthetic participant failure",0x8016ebbc,29037);
  assert(r.events.size()==1 && r.events[0]==Event::Error);
  assert(r.error=="synthetic participant failure");
  std::cout<<r.output[0]<<"\n";
  Reader regular;
  regular.SdEvent("input",0x80377584,0);
  assert(regular.events.size()==1 && regular.events[0]==Event::Progress);
  Reader oversized;
  oversized.slices={{SliceTag::Result,0,0x80479da4,0,4096}};
  oversized.slice_count=1;
  oversized.OrdinaryTerminalFailure("secondary failure",0x8016ebbc,29037);
  assert(oversized.events.empty());
  assert(oversized.error=="Ordinary timeout event exceeds its serialized ceiling");
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / "terminal.cpp").write_text(harness)
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror",
                                    str(p / "terminal.cpp"), "-o", str(p / "terminal")],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(p / "terminal")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
        payload = json.loads(run.stdout)
        self.assertEqual(payload["name"], "terminal_rejected")
        self.assertEqual(payload["pc"], 0x8016ebbc)
        self.assertEqual((payload["consumed"], payload["menu_consumed"]), (29038, 751))
        self.assertEqual([(s["tag"], s["address"], len(bytes.fromhex(s["hex"])))
                          for s in payload["slices"]], [(15, 0x80479da4, 0x448), (14, 0x8046b6a0, 46)])
        raw = bytearray(0x448)
        for slot, base in enumerate((0x58, 0x100)):
            raw[base+1]=8; raw[base+3]=(1,0)[slot]<<2
            raw[base+5]=1; raw[base+8]=(3,4)[slot]
        self.assertEqual(payload["slices"][0]["hex"], raw.hex())
        self.assertEqual(payload["slices"][1]["hex"], "00"*46)
