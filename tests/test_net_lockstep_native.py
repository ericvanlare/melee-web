"""Compile the production native network-input functions against narrow stubs.

This exercises the actual A2 start barrier, source-tick wait/resume, indexed
duplicate/gap/conflict handling, and terminal hold without requiring a browser
or emscripten toolchain.
"""
from __future__ import annotations

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "gameplay_net_input.c"
SCRATCH_PARENT = ROOT / "work" / "net-lockstep-native-tests"


def _function(source: str, name: str) -> str:
    match = re.search(
        r"(?:EMSCRIPTEN_KEEPALIVE\s+)?(?:static\s+)?(?:const\s+PADStatus\s*\*|const\s+char\s*\*|"
        r"void|int|unsigned|uint32_t|uint64_t)\s+" + re.escape(name) + r"\b",
        source,
    )
    if not match:
        raise AssertionError(f"source function is absent: {name}")
    brace = source.find("{", match.end())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1]
    raise AssertionError(f"unterminated source function: {name}")


def _harness() -> str:
    source = SOURCE.read_text(encoding="utf-8")
    state_start = source.index("typedef struct NetSession {")
    state_end = source.index("} NetSession;", state_start) + len("} NetSession;")
    state = source[state_start:state_end]
    names = (
        "fail", "melee_web_net_active", "melee_web_net_reset", "net_begin",
        "melee_web_net_begin", "melee_web_net_begin_lockstep",
        "melee_web_net_enable_local_input_capture", "encode_local_pad",
        "melee_web_net_capture_local_input", "ring_used",
        "capture_start_identity", "record_arena", "melee_web_net_before_step",
        "melee_web_net_after_step", "decode_frame", "melee_web_net_push",
        "melee_web_net_push_indexed", "melee_web_net_confirm_start",
        "melee_web_net_terminate", "melee_web_net_status",
    )
    functions = "\n".join(_function(source, name) for name in names)
    return f'''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
typedef uint16_t u16;
typedef int8_t s8;
typedef struct {{ uint16_t button; int8_t stickX, stickY, substickX, substickY;
  uint8_t triggerLeft, triggerRight, analogA, analogB; int8_t err; }} PADStatus;
typedef struct {{ uint32_t tick, scene, seed, frame, flags, objects;
  uint64_t input, pad, scene_state, object_state, total; }} MeleeWebNetChecksumRecord;
typedef struct {{ uint32_t tick, scene, base, bytes; uint64_t hash; }} MeleeWebNetArenaRecord;
typedef struct MeleeWebMenuHost MeleeWebMenuHost;
void melee_web_net_terminate(unsigned kind, uint32_t tick, unsigned channel);
enum {{ MELEE_WEB_NET_FRAME_BYTES=44, MELEE_WEB_NET_MAX_FRAMES=216000,
  MELEE_WEB_NET_INPUT_DELAY=2, MELEE_WEB_NET_PAD_BYTES=11,
  MELEE_WEB_NET_TERMINAL_DESYNC=1, MELEE_WEB_NET_TERMINAL_DISCONNECT=2,
  MELEE_WEB_NET_TERMINAL_PROTOCOL=3, MELEE_WEB_NET_TERMINAL_START_IDENTITY=4,
  MELEE_WEB_NET_CHECKSUM_RING=8, MELEE_WEB_NET_ARENA_RECORDS=64,
  MELEE_WEB_SAVE_PROFILE_CARD_BYTES=64, MELEE_WEB_PAD_STATE_BYTES=8 }};
enum {{ PAD_ERR_NONE=0, PAD_ERR_NO_CONTROLLER=-1 }};
#define EMSCRIPTEN_KEEPALIVE
#define FNV_OFFSET 0xcbf29ce484222325ull
static unsigned card_snapshots, pad_snapshots;
static char status_text[16384];
static unsigned local_capture_publications;
static uint32_t local_capture_tick;
static unsigned local_capture_port;
static uint64_t local_capture_serial;
static uint8_t local_capture_bytes[MELEE_WEB_NET_PAD_BYTES];
static int local_capture_accept=1;
int melee_web_net_publish_local_input(uint32_t tick,unsigned port,uint64_t serial,
  const uint8_t bytes[MELEE_WEB_NET_PAD_BYTES]) {{
  ++local_capture_publications;local_capture_tick=tick;local_capture_port=port;
  local_capture_serial=serial;memcpy(local_capture_bytes,bytes,MELEE_WEB_NET_PAD_BYTES);
  return local_capture_accept;
}}
uint64_t melee_web_net_fnv1a64(uint64_t hash, const void* bytes, size_t size) {{
  const uint8_t* p=bytes; for(size_t i=0;i<size;++i){{hash^=p[i];hash*=0x100000001b3ull;}} return hash;
}}
int melee_web_menu_host_snapshot_card_data(MeleeWebMenuHost* host,int baseline,uint8_t* out,
  size_t size,char* error,size_t capacity) {{ (void)host;(void)baseline;(void)error;(void)capacity;
  if (size != MELEE_WEB_SAVE_PROFILE_CARD_BYTES) {{
    return 0;
  }}
  memset(out, 0x5a, size);
  ++card_snapshots;
  return 1;
}}
void melee_web_pad_state_capture(uint8_t* out) {{ memset(out,0x39,MELEE_WEB_PAD_STATE_BYTES);++pad_snapshots; }}
void melee_web_net_checksum_compute(uint32_t tick,uint32_t scene,const PADStatus pads[4],
  const MeleeWebMenuHost* host,MeleeWebNetChecksumRecord* out) {{
  (void)pads;(void)host;memset(out,0,sizeof(*out));out->tick=tick;out->scene=scene;
  out->seed=7;out->frame=12;out->flags=1;out->total=0x12345678; }}
int melee_web_net_arena_hash(uint32_t* base,uint32_t* bytes,uint64_t* hash) {{
  (void)base;(void)bytes;(void)hash;return 0; }}
{state}
static NetSession net = {{.arena_fill=-1}};
{functions}
int main(void) {{
  uint8_t frame[MELEE_WEB_NET_FRAME_BYTES]={{0}};
  char* status;
  if(!melee_web_net_begin_lockstep(7,32,NULL,0))return 1;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(melee_web_net_before_step(1)!=NULL||net.cursor!=0||!net.start_recorded||
     net.start_required!=1||net.start_confirmed||card_snapshots!=1||pad_snapshots!=1)return 2;
  status=(char*)melee_web_net_status();
  if(!strstr(status,"\\\"blocker\\\":\\\"start_identity\\\"")||
     !strstr(status,"\\\"native_context\\\":\\\""))return 3;
  if(!melee_web_net_confirm_start())return 4;
  if(melee_web_net_before_step(1)!=NULL||net.wait_episodes!=1||net.wait_start_tick!=0)return 5;
  status=(char*)melee_web_net_status();
  if(!strstr(status,"\\\"blocker\\\":\\\"network_wait\\\"")||
     !strstr(status,"\\\"network_wait\\\":{{\\\"active\\\":1"))return 26;
  if(!melee_web_net_push_indexed(0,frame,1)||!melee_web_net_push_indexed(0,frame,1)||
     net.indexed_duplicates!=1||net.pushed!=1)return 6;
  if(melee_web_net_before_step(1)==NULL)return 7;
  melee_web_net_after_step();
  if(net.cursor!=1)return 8;
  if(melee_web_net_before_step(1)!=NULL||net.wait_episodes!=2||net.wait_start_tick!=1)return 9;
  if(!melee_web_net_push_indexed(1,frame,1)||melee_web_net_before_step(1)==NULL||
     net.wait_resume_count!=2)return 10;
  melee_web_net_after_step();
  if(net.cursor!=2)return 11;
  if(melee_web_net_push_indexed(3,frame,1)||net.indexed_gaps!=1||
     net.terminal_kind!=MELEE_WEB_NET_TERMINAL_PROTOCOL)return 12;
  if(melee_web_net_before_step(1)!=NULL||net.cursor!=2||!net.active)return 13;
  status=(char*)melee_web_net_status();
  if(!strstr(status,"\\\"blocker\\\":\\\"terminal\\\""))return 14;

  melee_web_net_reset();
  if(!melee_web_net_begin_lockstep(7,32,NULL,0))return 15;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(melee_web_net_before_step(1)!=NULL||!melee_web_net_confirm_start())return 16;
  if(!melee_web_net_push_indexed(0,frame,1))return 17;
  if(melee_web_net_push_indexed(0,(uint8_t[44]){{0,1}},1)||
     net.indexed_conflicts!=1||net.terminal_kind!=MELEE_WEB_NET_TERMINAL_PROTOCOL)return 18;
  if(net.pushed!=1||net.cursor!=0||melee_web_net_before_step(1)!=NULL)return 19;

  melee_web_net_reset();
  if(!melee_web_net_begin_lockstep(7,32,NULL,0))return 20;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(melee_web_net_before_step(1)!=NULL||!melee_web_net_confirm_start()||
     !melee_web_net_push_indexed(0,frame,1)||melee_web_net_before_step(1)==NULL)return 21;
  melee_web_net_after_step();
  melee_web_net_terminate(MELEE_WEB_NET_TERMINAL_DISCONNECT,1,0);
  if(melee_web_net_before_step(1)!=NULL||net.cursor!=1||!melee_web_net_active()||
     melee_web_net_push_indexed(1,frame,1))return 22;

  /* Exhausting a finite A2 input timeline is completion, not another wait. */
  melee_web_net_reset();
  if(!melee_web_net_begin_lockstep(7,1,NULL,0))return 27;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(melee_web_net_before_step(1)!=NULL||!melee_web_net_confirm_start()||
     !melee_web_net_push_indexed(0,frame,1)||melee_web_net_before_step(1)==NULL)return 28;
  melee_web_net_after_step();
  if(melee_web_net_before_step(1)!=NULL||net.cursor!=1||net.wait_callbacks||net.wait_episodes)return 29;
  status=(char*)melee_web_net_status();
  if(!strstr(status,"\\\"blocker\\\":\\\"complete\\\"")||
     !strstr(status,"\\\"network_wait\\\":{{\\\"active\\\":0"))return 30;

  /* Existing A1 sequential entry keeps its established first consumable tick. */
  melee_web_net_reset();
  unsigned before=card_snapshots;
  if(!melee_web_net_begin(7,32,NULL,0))return 23;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(!melee_web_net_push(frame,1)||melee_web_net_before_step(1)==NULL||
     !net.start_recorded||net.start_required||card_snapshots!=before)return 24;
  melee_web_net_after_step();
  if(net.cursor!=1)return 25;

  /* Native local sampling keeps the existing 11-byte conversion and does not
   * revisit a cursor while a remote contribution holds the source boundary. */
  melee_web_net_reset();
  local_capture_publications=0;local_capture_accept=1;
  if(!melee_web_net_begin_lockstep(9,6,NULL,0))return 31;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(melee_web_net_enable_local_input_capture(2,4)||
     melee_web_net_enable_local_input_capture(0,3)||
     !melee_web_net_enable_local_input_capture(0,4)||
     melee_web_net_enable_local_input_capture(0,4))return 32;
  if(!melee_web_net_capture_local_input(19,NULL)||local_capture_publications)return 33;
  if(melee_web_net_before_step(1)!=NULL||!melee_web_net_confirm_start())return 33;
  PADStatus raw[4]={{0}};
  raw[0].button=0x1234;raw[0].stickX=INT8_MIN;raw[0].stickY=INT8_MAX;
  raw[0].substickX=-1;raw[0].substickY=1;raw[0].triggerLeft=0x22;
  raw[0].triggerRight=0x33;raw[0].analogA=0x44;raw[0].analogB=0x55;
  if(!melee_web_net_capture_local_input(20,raw)||local_capture_publications!=1||
     local_capture_tick!=0||local_capture_port!=0||local_capture_serial!=20||
     memcmp(local_capture_bytes,(uint8_t[11]){{0x12,0x34,0x80,0x7f,0xff,0x01,
       0x22,0x33,0x44,0x55,0x00}},11)||
     memcmp(net.local_capture_last_bytes,local_capture_bytes,11))return 34;
  if(melee_web_net_before_step(1)!=NULL||net.wait_episodes!=1)return 35;
  raw[0].button=0;
  if(!melee_web_net_capture_local_input(21,raw)||local_capture_publications!=1||
     memcmp(net.local_capture_last_bytes,local_capture_bytes,11))return 35;
  uint8_t source_frames[MELEE_WEB_NET_FRAME_BYTES*6]={{0}};
  if(!melee_web_net_push_indexed(0,source_frames,6))return 36;
  if(melee_web_net_before_step(1)==NULL)return 37;
  melee_web_net_after_step();
  raw[0].button=0x80;
  if(!melee_web_net_capture_local_input(22,raw)||local_capture_publications!=2||
     local_capture_tick!=1||local_capture_serial!=22)return 38;
  if(melee_web_net_before_step(1)==NULL)return 39;
  melee_web_net_after_step();
  raw[0].button=0;
  for(uint32_t tick=2;tick<4;++tick){{
    if(!melee_web_net_capture_local_input(20+tick*2,raw)||
       local_capture_publications!=tick+1||local_capture_tick!=tick)return 40;
    if(melee_web_net_before_step(1)==NULL)return 41;
    melee_web_net_after_step();
  }}
  if(!melee_web_net_capture_local_input(30,raw)||local_capture_publications!=4)return 42;
  if(melee_web_net_before_step(1)==NULL)return 43;
  melee_web_net_after_step();
  if(!melee_web_net_capture_local_input(31,raw)||local_capture_publications!=4)return 44;
  if(melee_web_net_before_step(1)==NULL)return 45;
  melee_web_net_after_step();
  if(net.cursor!=6||net.local_capture_count!=4)return 46;
  melee_web_net_reset();
  if(net.local_capture_enabled||net.local_capture_count||net.local_capture_last_poll_serial)return 60;

  /* New source contributions sharing a PAD poll serial fail before any
   * second publication, and an unavailable local port fails the same way. */
  melee_web_net_reset();
  if(!melee_web_net_begin_lockstep(9,6,NULL,0))return 47;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(!melee_web_net_enable_local_input_capture(0,4)||
     melee_web_net_before_step(1)!=NULL||!melee_web_net_confirm_start())return 48;
  local_capture_publications=0;raw[0].err=PAD_ERR_NONE;
  uint8_t prefix_frames[MELEE_WEB_NET_FRAME_BYTES*2]={{0}};
  if(!melee_web_net_capture_local_input(50,raw)||
     !melee_web_net_push_indexed(0,prefix_frames,2)||
     melee_web_net_before_step(1)==NULL)return 49;
  melee_web_net_after_step();
  if(melee_web_net_capture_local_input(50,raw)||local_capture_publications!=1||
     net.terminal_kind!=MELEE_WEB_NET_TERMINAL_PROTOCOL||net.cursor!=1)return 56;
  melee_web_net_reset();
  if(!melee_web_net_begin_lockstep(9,6,NULL,0))return 50;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(!melee_web_net_enable_local_input_capture(1,4)||
     melee_web_net_before_step(1)!=NULL||!melee_web_net_confirm_start())return 51;
  raw[1].err=PAD_ERR_NO_CONTROLLER;
  if(melee_web_net_capture_local_input(60,raw)||
     net.terminal_kind!=MELEE_WEB_NET_TERMINAL_PROTOCOL||local_capture_publications!=1)return 52;
  melee_web_net_reset();
  if(!melee_web_net_begin_lockstep(9,6,NULL,0))return 57;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(!melee_web_net_enable_local_input_capture(0,4)||
     melee_web_net_before_step(1)!=NULL||!melee_web_net_confirm_start())return 58;
  net.cursor=1;local_capture_publications=0;
  if(melee_web_net_capture_local_input(65,raw)||local_capture_publications||
     net.terminal_kind!=MELEE_WEB_NET_TERMINAL_PROTOCOL)return 59;
  melee_web_net_reset();
  if(!melee_web_net_begin_lockstep(9,6,NULL,0))return 53;
  net.context_applied=1;net.host=(MeleeWebMenuHost*)1;
  if(!melee_web_net_enable_local_input_capture(0,4)||
     melee_web_net_before_step(1)!=NULL||!melee_web_net_confirm_start())return 54;
  local_capture_publications=0;local_capture_accept=0;raw[0].err=PAD_ERR_NONE;
  if(melee_web_net_capture_local_input(70,raw)||net.cursor!=0||
     net.terminal_kind!=MELEE_WEB_NET_TERMINAL_PROTOCOL)return 55;
  melee_web_net_reset();local_capture_accept=1;
  melee_web_net_reset();
  return 0;
}}
'''


class NetLockstepNativeTests(unittest.TestCase):
    def test_real_native_start_wait_indexed_and_terminal_transitions(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.fail("cc is required for the native lockstep boundary control")
        SCRATCH_PARENT.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="run-", dir=SCRATCH_PARENT))
        harness = directory / "net_lockstep_native.c"
        binary = directory / "net_lockstep_native"
        harness.write_text(_harness(), encoding="utf-8")
        result = subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                 str(harness), "-o", str(binary)],
                               capture_output=True, text=True, timeout=30, check=False)
        if result.returncode:
            self.fail(f"native harness compile failed; retained {directory}: {result.stderr}")
        result = subprocess.run([str(binary)], capture_output=True, text=True,
                                timeout=10, check=False)
        if result.returncode:
            self.fail(f"native lockstep control failed ({result.returncode}); retained {directory}")
        shutil.rmtree(directory)


if __name__ == "__main__":
    unittest.main()
