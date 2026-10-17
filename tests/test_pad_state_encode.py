"""Read-only retained PAD codec; actual serializers with source field layouts."""
from pathlib import Path
import os,shlex,subprocess,unittest
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
ROOT=Path(__file__).resolve().parents[1]
class PadStateEncodeTests(OwnedWorkspaceTests):
 def test_actual_retained_codec(self):
  upstream=Path(os.environ.get('MELEE_WEB_SSS_PREFIX_UPSTREAM',ROOT/'.deps/melee'))
  header=upstream/'src/sysdolphin/baselib/controller.h'
  if not header.is_file():
   if 'MELEE_WEB_SSS_PREFIX_UPSTREAM' in os.environ:self.fail('Configured controller header missing')
   self.skipTest('Pinned controller source unavailable')
  h=header.read_text()
  body='#include <stdint.h>\n#include <assert.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n'
  body+='typedef uint8_t u8;typedef int8_t s8;typedef uint32_t u32;typedef int32_t s32;typedef float f32;typedef struct HSD_PadData HSD_PadData;struct RumbleInfo{void* fixture;};\n'
  for name in ['HSD_PadStatus','PadLibData']:
   body+=_function(h,'struct '+name)+';\ntypedef struct '+name+' '+name+';\n'
  body+='PadLibData HSD_PadLibData;HSD_PadStatus HSD_PadMasterStatus[4],HSD_PadCopyStatus[4],HSD_PadGameStatus[4];\n'
  body+=(ROOT/'src/gameplay_pad_state.h').read_text().replace('#pragma once','')
  source=(ROOT/'src/gameplay_pad_state.c').read_text()
  body+='\n'.join(x for x in source.splitlines() if not x.startswith('#include'))
  body+=r'''
int main(void){
 unsigned char bytes[822]={0},out[822];char error[256];
 const unsigned char config[30]={0,0,0,45,0,0,0,8,0,30,0,0,0,0,0,0,127,0,0,255,0,0,255,0,127,255,255,0,0,0};memcpy(bytes,config,30);
 bytes[30+24]=219;bytes[30+32]=0xbf;bytes[30+33]=0;bytes[30+34]=0;bytes[30+35]=0;bytes[30+66+3]=8;
 MeleeWebPadState* p=melee_web_pad_state_decode(bytes,sizeof bytes,error,sizeof error);assert(p);
 memset(&HSD_PadLibData,0x5a,sizeof HSD_PadLibData);PadLibData live=HSD_PadLibData;
 memset(out,0x77,sizeof out);assert(!melee_web_pad_state_encode(NULL,out,sizeof out)&&out[0]==0x77);assert(!melee_web_pad_state_encode(p,NULL,sizeof out));assert(!melee_web_pad_state_encode(p,out,sizeof out-1)&&out[0]==0x77);
 assert(melee_web_pad_state_encode(p,out,sizeof out));assert(!memcmp(out,bytes,sizeof bytes));assert(!memcmp(&live,&HSD_PadLibData,sizeof live));
 melee_web_pad_state_free(p);puts("PASS actual 822-byte retained encode/decode; signed/float/bank bytes; null/size refusal without live mutation");}
'''
  w=self.new_workspace(ROOT,'pad-encode-');c=w/'control.c';exe=w/'control';c.write_text(body)
  argv=shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)]
  r=subprocess.run(argv,capture_output=True,text=True);(w/'compile.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  r=subprocess.run([str(exe)],capture_output=True,text=True);(w/'run.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
