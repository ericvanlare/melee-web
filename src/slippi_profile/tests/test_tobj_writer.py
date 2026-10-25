"""Actual MakeTextureMtx + runtime correlation; descriptor/outer boundary models explicit."""
from pathlib import Path
import base64
import hashlib
import json
import os
import re
import shutil
import shlex
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(os.environ.get('MELEE_TOBJ_WRITER_REPO_ROOT',Path(__file__).resolve().parents[3])).resolve()
SOURCE=Path(os.environ.get('MELEE_TOBJ_WRITER_CANDIDATE_ROOT',ROOT)).resolve()
_import_path=sys.path.copy();sys.path.insert(0,str(ROOT/'tests'));sys.path.insert(0,str(ROOT/'scripts'))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime
sys.path[:]=_import_path

def function(text,name):
 m=re.search(r'^static void '+re.escape(name)+r'\([^;]*?\)\s*\{',text,re.M)
 if not m:raise AssertionError('Actual source function absent: '+name)
 b=text.index('{',m.start());depth=1;i=b+1
 while depth:
  if text[i]=='{':depth+=1
  elif text[i]=='}':depth-=1
  i+=1
 return text[m.start():i]

class TObjWriterTests(OwnedWorkspaceTests):
 @classmethod
 def setUpClass(cls):
  parent=os.environ.get('MELEE_TOBJ_WRITER_OUTPUT_ROOT')
  if parent:
   requested=Path(parent)
   if requested.is_symlink():raise ValueError('evidence root symlink refused')
   p=requested.resolve();p.mkdir(parents=True,exist_ok=True);cls.work=Path(tempfile.mkdtemp(prefix='tobj-writer-',dir=p))
  else:cls.work=cls.new_workspace(ROOT,'tobj-writer-')
 def test_actual_producer_pre_post_and_refusal(self):
  em=ROOT/'.deps/emsdk/upstream/emscripten'
  if not (em/'emcc').is_file():self.skipTest('Pinned Emscripten unavailable')
  lock=json.loads((ROOT/'dependencies.lock.json').read_text())
  self.assertEqual((em/'emscripten-version.txt').read_text().strip().strip(chr(34)),lock['emscripten'])
  prepared=Path(os.environ.get('MELEE_TOBJ_WRITER_PREPARED_SOURCE',ROOT/'build/gameplay-source-slippi-profile/src')).resolve()
  if not (prepared/'sysdolphin/baselib/tobj.h').is_file():
   if 'MELEE_TOBJ_WRITER_PREPARED_SOURCE' in os.environ:self.fail('Explicit prepared headers unavailable')
   self.skipTest('Existing generated source headers unavailable; use build entrypoint')
  overlay=self.work/'overlay';(overlay/'src').mkdir(parents=True);(overlay/'tests').mkdir()
  # Real copies preserve quoted forced-header sibling resolution; no preparation.
  for p in (ROOT/'src').rglob('*'):
   if p.is_file() and p.suffix in ('.h','.hpp','.inc'):
    dest=overlay/'src'/p.relative_to(ROOT/'src');dest.parent.mkdir(parents=True,exist_ok=True)
    q=SOURCE/'src'/p.relative_to(ROOT/'src');shutil.copy2(q if q.is_file() else p,dest)
  for p in (SOURCE/'src').rglob('*'):
   if p.is_file() and p.suffix in ('.h','.hpp','.inc'):
    q=overlay/'src'/p.relative_to(SOURCE/'src');q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
  for rel in ['src/gameplay_source_memory_runtime.cpp','tests/source_class_piece_control.cpp']:
   q=SOURCE/rel;p=q if q.is_file() else ROOT/rel;dest=overlay/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
  shutil.copy2(SOURCE/'src/slippi_profile/tests/tobj_writer_control.cpp.in',overlay/'tests/source_tobj_writer_control.cpp')
  tree=self.work/'original';tree.mkdir();env=dict(os.environ,TMPDIR=str(self.work),GIT_CEILING_DIRECTORIES=str(tree.parent))
  for k in ['GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE','GIT_COMMON_DIR']:env.pop(k,None)
  def run(name,argv,cwd=None):
   p=subprocess.run(list(map(str,argv)),cwd=cwd or self.work,env=env,text=True,capture_output=True,timeout=120)
   (self.work/(name+'.json')).write_text(json.dumps({'argv':list(map(str,argv)),'cwd':str(cwd or self.work),'exit_code':p.returncode,'stdout':p.stdout,'stderr':p.stderr},indent=2)+'\n')
   self.assertEqual(p.returncode,0,p.stdout+p.stderr);return p
  ceiling=subprocess.run(['git','rev-parse','--show-toplevel'],cwd=tree,env=env,capture_output=True,text=True)
  self.assertNotEqual(ceiling.returncode,0,'Private source tree discovered outer checkout')
  commit=json.loads((ROOT/'dependencies.lock.json').read_text())['repositories']['melee']['commit']
  rels=['src/sysdolphin/baselib/'+n for n in ['memory.c','memory.h','class.c','tobj.c']]
  run('clone-readonly-dependency',['git','clone','--shared','--no-checkout',ROOT/'.deps/melee',tree])
  self.assertEqual(subprocess.check_output(['git','rev-parse','HEAD'],cwd=tree,text=True).strip(),commit)
  patch=SOURCE/'patches/melee-gameplay.patch';patch=patch if patch.is_file() else ROOT/'patches/melee-gameplay.patch'
  profile=SOURCE/'src/slippi_profile/patches/melee-profile-hooks.patch'
  saved=sys.path.copy();sys.path.insert(0,str(ROOT/'scripts'))
  from gameplay_bool import composed_patch
  from gameplay_sources import compose_slippi_profile_patch
  sys.path[:]=saved
  # Actual production order/APIs in a private clone. Their caches/temp source
  # are external; no checkout build, source preparation, or SDK mutation.
  lowered=composed_patch(tree,patch)
  final=compose_slippi_profile_patch(tree,lowered,profile)
  for rel in rels:
   q=tree/rel;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(subprocess.check_output(['git','show',commit+':'+rel],cwd=tree))
  run('apply-actual-composed',['git','apply',*['--include='+x for x in rels],final],tree)
  body=function((tree/'src/sysdolphin/baselib/tobj.c').read_text(),'MakeTextureMtx')
  self.assertEqual(body.count('melee_web_tobj_writer_before('),1);self.assertEqual(body.count('melee_web_tobj_writer_after('),1)
  producer=self.work/'producer.c'
  math_text=(prepared/'sysdolphin/baselib/mtx.c').read_text()
  rotation=function(math_text.replace('void HSD_MkRotationMtx','static void HSD_MkRotationMtx'),'HSD_MkRotationMtx').replace('static void HSD_MkRotationMtx','void HSD_MkRotationMtx',1)
  producer.write_text('#include <placeholder.h>\n#include <sysdolphin/baselib/tobj.h>\n#include <sysdolphin/baselib/mtx.h>\n#include <sysdolphin/baselib/debug.h>\n#include <math.h>\n#include <string.h>\n#include <stddef.h>\n#include "gameplay_tobj_writer_diagnostic.h"\n#define FLT_EPSILON 1.00000001335e-10F\n_Static_assert(sizeof(HSD_TObj)==172,"extent");\n_Static_assert(offsetof(HSD_TObj,flags)==76,"flags");\n_Static_assert(offsetof(HSD_TObj,mtx)==116,"matrix");\n'+rotation+'\n'+body+'\nvoid writer_actual_tobj_producer(HSD_TObj* p){MakeTextureMtx(p); }\n')
  common=['-DTARGET_PC','-ffp-contract=off','-I'+str(overlay/'src'),'-I'+str(tree/'src'),'-I'+str(prepared),'-I'+str(prepared/'sysdolphin/baselib'),'-I'+str(ROOT/'.deps/aurora/include'),'-include',str(overlay/'src/gameplay_compat.h')]
  recipes=None
  if 'MELEE_TOBJ_WRITER_COMPILE_COMMANDS' not in os.environ:
   candidate=ROOT/'build/slippi-synctest-logical-frame-lobj-presentation-stage-arena-aurora-staging-release/compile_commands.json'
   self.assertTrue(candidate.is_file(),'Actual v7 graph required; no preparation in controls');recipes=json.loads(candidate.read_text())
  if 'MELEE_TOBJ_WRITER_COMPILE_COMMANDS' in os.environ:
   rp=Path(os.environ['MELEE_TOBJ_WRITER_COMPILE_COMMANDS']);self.assertTrue(rp.is_file(),'Explicit compile graph missing');recipes=json.loads(rp.read_text())
  def compile_source(name,source,mode,cpp=False):
   out=self.work/(mode+'-'+name+'.o');active=mode=='active'
   if recipes and name in ['producer','memory','class','sdkmtx','psmath']:
    suffix={'producer':'/sysdolphin/baselib/tobj.c','memory':'/sysdolphin/baselib/memory.c','class':'/sysdolphin/baselib/class.c','sdkmtx':'/lib/dolphin/mtx/mtx.c','psmath':'/gameplay_ps_math.c'}[name]
    matches=[x for x in recipes if x['file'].endswith(suffix)];self.assertEqual(len(matches),1)
    cmd=shlex.split(matches[0]['command']);i=cmd.index('-o');del cmd[i:i+2];i=cmd.index('-c');del cmd[i:i+2]
    cmd=[x.replace(str(ROOT/'src/gameplay_compat.h'),str(overlay/'src/gameplay_compat.h')) for x in cmd if not x.startswith('-DMELEE_WEB_CPU_INTERVAL_ENABLED')]
    cmd+=[*common,'-DMELEE_WEB_CPU_INTERVAL_ENABLED=1' if active else '-DMELEE_WEB_CPU_INTERVAL_ENABLED=0','-c',str(source),'-o',str(out)]
   else:cmd=[em/('em++' if cpp else 'emcc'),'-std=c++20' if cpp else '-std=gnu11','-O3','-DNDEBUG',*common,'-DMELEE_WEB_CPU_INTERVAL_ENABLED=1' if active else '-DMELEE_WEB_CPU_INTERVAL_ENABLED=0','-c',source,'-o',out]
   run(mode+'-compile-'+name,cmd);return out
  for mode in ['active','inactive']:
   objects=[compile_source(n,tree/('src/sysdolphin/baselib/'+n+'.c'),mode) for n in ['memory','class']]
   objects+=[compile_source('producer',producer,mode),compile_source('psmath',ROOT/'src/gameplay_ps_math.c',mode)]
   for n,suffix in [('sdkmtx','mtx.c')]:
    q=ROOT/'.deps/aurora/lib/dolphin/mtx'/suffix
    objects.append(compile_source(n,q,mode))
   inputs=[overlay/'tests/source_tobj_writer_control.cpp',*objects]
   if mode=='active':inputs += [ROOT/'src/source_address_context.cpp',ROOT/'src/source_game_heap_context.cpp']
   binary=self.work/(mode+'.js');run(mode+'-link',[em/'em++','-std=c++20','-O1','-fexceptions',*common,'-DMELEE_WEB_CPU_INTERVAL_ENABLED=1' if mode=='active' else '-DMELEE_WEB_CPU_INTERVAL_ENABLED=0',*inputs,'-Wl,--wrap=calloc','-sDISABLE_EXCEPTION_CATCHING=0','-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-o',binary])
   for case in (['phase','bounds','extent','full172','pending','missing','retired','overflow','reuse','wrong-comparison','wrong-row','unknown','recreated','mirror','wrong-ticket','custom','missing-pre','wrong-boundary','wrong-epoch','full-post48','late','bad-magic','bad-layout','bad-epoch'] if mode=='active' else ['inactive']):
    result=run(mode+'-'+case,[node_runtime(),binary,case])
    if mode=='active' and case=='phase':
     marker='ACTUAL_TOBJ_WRITER_BASE64=';encoded=next(x.split(marker,1)[1] for x in result.stdout.splitlines() if x.startswith(marker));raw=base64.b64decode(encoded,validate=True);self.assertEqual(len(raw),28048)
     raw_path=self.work/'actual-writer.bin';raw_path.write_bytes(raw)
     jsroot=self.work/'js';jsroot.mkdir()
     for q in (ROOT/'src/slippi_profile/tests').glob('*.mjs'):
      override=SOURCE/'src/slippi_profile/tests'/q.name;shutil.copy2(override if override.is_file() else q,jsroot/q.name)
     module=jsroot/'cpu_two_tick_runner.mjs'
     js="import fs from 'node:fs';import {decodeCpuTObjWriterEvidence} from "+json.dumps(module.as_uri())+";const e=decodeCpuTObjWriterEvidence(new Uint8Array(fs.readFileSync("+json.dumps(str(raw_path))+")));if(e.status!==1||e.postCount!==1||e.drawnCount!==2||e.matchedIndex!==0||e.livePieceLifetime!=='UNKNOWN')throw Error('actual native decoder bridge');console.log('actual28048 bridge POST writer9, excluded later writer10; lifetime UNKNOWN');"
     run('actual-decoder-bridge',[node_runtime(),'--input-type=module','--eval',js])
  print('Actual producer/runtime controls retained at',self.work)
if __name__=='__main__':unittest.main()
