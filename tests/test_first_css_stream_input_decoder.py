"""Compile the actual input decoder against SDK PADStatus; synthetic bytes only.

No source world, assets, game execution or original/native agreement is tested.
"""
from pathlib import Path
import os
import shutil
import subprocess
from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/stadium_first_css_diagnostic_input.hpp"
SOURCE_INCLUDE = ROOT / "src"
GENERATED_INCLUDE = ROOT / "build/gameplay-source/src"
AURORA_INCLUDE = ROOT / ".deps/aurora/include"
EMXX = ROOT / ".deps/emsdk/upstream/emscripten/em++"


class FirstCssStreamInputDecoderTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "first-css-stream-decoder-")

    def test_compiled_exact_header_bounds_and_pad_bytes(self):
        if not EMXX.is_file() or not (GENERATED_INCLUDE / "melee/gm/types.h").is_file():
            self.skipTest("Prepared SDK headers and installed Emscripten required")
        node = shutil.which("node")
        if not node:
            self.skipTest("Node required for asset-free decoder control")
        cpp = self.scratch / "decoder.cpp"
        binary = self.scratch / "decoder.js"
        cpp.write_text('#include "' + str(HEADER) + '"\n' + CONTROL)
        command = [str(EMXX), str(cpp), "-o", str(binary), "-std=gnu++20",
                   "-O0", "-fexceptions", "-DAURORA", "-DAURORA_ENABLE_GX",
                   "-DTARGET_PC", "-sEXIT_RUNTIME=1", "-sSTACK_SIZE=1048576",
                   "-I" + str(SOURCE_INCLUDE), "-I" + str(GENERATED_INCLUDE),
                   "-I" + str(AURORA_INCLUDE)]
        environment = dict(os.environ, TMPDIR=str(self.scratch))
        with (self.scratch / "compile.log").open("w") as log:
            compiled = subprocess.run(command, env=environment, stdout=log,
                                      stderr=subprocess.STDOUT, timeout=60)
        self.assertEqual(compiled.returncode, 0,
                         (self.scratch / "compile.log").read_text())
        with (self.scratch / "control.log").open("w") as log:
            result = subprocess.run([node, str(binary)], env=environment,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=30)
        self.assertEqual(result.returncode, 0,
                         (self.scratch / "control.log").read_text())
        self.assertIn("PASS exact decoder 148 batches/592 ports; 6573 envelope refusals; 2 index refusals",
                      (self.scratch / "control.log").read_text())


CONTROL = r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using namespace melee_web::stadium_first_css_diagnostic;
int main() {
    // Independently authored wire fixture: header constants are literal, and
    // every payload byte varies across ports/batches to expose swaps/sign loss.
    std::vector<uint8_t> wire(6564);
    std::memcpy(wire.data(), "STC1PSTR", 8);
    wire[11] = 1;
    const uint8_t source[] = {0x36,0x1e,0x8c,0x11,0x08,0xcc,0x2d,0x02,
        0xba,0x94,0xd1,0xf3,0xb3,0xbe,0x96,0x12,0xa8,0x0d,0x02,0x27,
        0x67,0x24,0x45,0xcd,0x4a,0x2b,0x0e,0x21,0x19,0x91,0x77,0x78};
    std::memcpy(wire.data()+12, source, 32);
    wire[46]=3; wire[47]=0x47; wire[51]=148;
    for (size_t i=52;i<wire.size();++i) wire[i]=uint8_t((i*37+(i/11)*19)&255);
    const auto before=wire;
    const std::string sha="361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778";
    const auto input=decode_postdraw_input(wire.data(),wire.size(),sha);
    assert(input.first_consume_sequence==839 && input.source_sha256==sha);
    assert(input.statuses.size()==6512 &&
           std::equal(input.statuses.begin(),input.statuses.end(),wire.begin()+52));
    for (unsigned batch=0;batch<148;++batch) {
        PADStatus ports[4]{};
        decode_postdraw_pad_statuses(input,batch,ports);
        for (unsigned port=0;port<4;++port) {
            const auto& p=ports[port];
            const uint8_t roundtrip[] = {uint8_t(p.button>>8),uint8_t(p.button),
                uint8_t(p.stickX),uint8_t(p.stickY),uint8_t(p.substickX),uint8_t(p.substickY),
                uint8_t(p.triggerLeft),uint8_t(p.triggerRight),uint8_t(p.analogA),
                uint8_t(p.analogB),uint8_t(p.err)};
            assert(std::equal(std::begin(roundtrip),std::end(roundtrip),
                              wire.begin()+52+batch*44+port*11));
        }
    }
    assert(wire==before);
    unsigned refused=0;
    const auto reject=[&](const uint8_t* data,size_t size,const std::string& identity) {
        bool caught=false;
        try { (void)decode_postdraw_input(data,size,identity); }
        catch (const std::runtime_error&) { caught=true; }
        assert(caught); ++refused;
    };
    // Every truncation, not merely a convenient header/payload cut.
    for(size_t size=0;size<wire.size();++size) reject(wire.data(),size,sha);
    reject(nullptr,wire.size(),sha);
    auto extra=wire; extra.push_back(0); reject(extra.data(),extra.size(),sha);
    for (size_t offset : {size_t(0),size_t(11),size_t(12),size_t(47),size_t(51)}) {
        auto changed=wire; changed[offset]^=1;
        reject(changed.data(),changed.size(),sha);
    }
    reject(wire.data(),wire.size(),std::string(64,'0'));
    auto foreign=wire; foreign[12]^=1;
    // Matching foreign context identity still cannot bypass the retained pin.
    reject(foreign.data(),foreign.size(),hex(foreign.data()+12,32));
    assert(refused==6573 && wire==before);
    for (uint32_t index : {uint32_t(148),UINT32_MAX}) {
        PADStatus ports[4]; std::memset(ports,0xA5,sizeof(ports));
        uint8_t saved[sizeof(ports)]; std::memcpy(saved,ports,sizeof(ports));
        bool caught=false;
        try { decode_postdraw_pad_statuses(input,index,ports); }
        catch(const std::runtime_error&) { caught=true; }
        assert(caught && std::memcmp(saved,ports,sizeof(ports))==0);
    }
    // Existing one-shot decoder remains callable and bit-equivalent for port0.
    ConsumedPadInput old{}; std::copy_n(input.statuses.begin(),44,old.ports.begin());
    PADStatus old_ports[4]{},stream_ports[4]{};
    decode_consumed_pad_statuses(old,old_ports);
    decode_postdraw_pad_statuses(input,0,stream_ports);
    assert(std::memcmp(old_ports,stream_ports,sizeof(old_ports))==0);
    std::cout << "PASS exact decoder 148 batches/592 ports; 6573 envelope refusals; 2 index refusals\n";
}
'''
