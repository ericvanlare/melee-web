"""Actual lvec producer + runtime correlation; service models are explicit."""
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
ROOT=Path(os.environ.get('MELEE_LOBJ_WRITER_REPO_ROOT',Path(__file__).resolve().parents[1])).resolve()
SOURCE=Path(os.environ.get('MELEE_LOBJ_WRITER_CANDIDATE_ROOT',ROOT)).resolve()
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

class LObjWriterTests(OwnedWorkspaceTests):
 @classmethod
 def setUpClass(cls):
  parent=os.environ.get('MELEE_LOBJ_WRITER_OUTPUT_ROOT')
  if parent:
   p=Path(parent).resolve();p.mkdir(parents=True,exist_ok=True);cls.work=Path(tempfile.mkdtemp(prefix='lobj-writer-',dir=p))
  else:cls.work=cls.new_workspace(ROOT,'lobj-writer-')
 def test_actual_producer_pre_post_and_refusal(self):
  em=ROOT/'.deps/emsdk/upstream/emscripten'
  if not (em/'emcc').is_file():self.skipTest('Pinned Emscripten unavailable')
  prepared=Path(os.environ.get('MELEE_LOBJ_WRITER_PREPARED_SOURCE',ROOT/'build/gameplay-source/src')).resolve()
  if not (prepared/'sysdolphin/baselib/lobj.h').is_file():
   if 'MELEE_LOBJ_WRITER_PREPARED_SOURCE' in os.environ:self.fail('Explicit prepared headers unavailable')
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
  for rel in ['src/gameplay_source_memory_runtime.cpp','tests/source_lobj_writer_control.cpp','tests/source_class_piece_control.cpp']:
   q=SOURCE/rel;p=q if q.is_file() else ROOT/rel;dest=overlay/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
  tree=self.work/'original';tree.mkdir();env=dict(os.environ,TMPDIR=str(self.work),GIT_CEILING_DIRECTORIES=str(tree.parent))
  for k in ['GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE','GIT_COMMON_DIR']:env.pop(k,None)
  def run(name,argv,cwd=None):
   p=subprocess.run(list(map(str,argv)),cwd=cwd or self.work,env=env,text=True,capture_output=True,timeout=120)
   (self.work/(name+'.json')).write_text(json.dumps({'argv':list(map(str,argv)),'cwd':str(cwd or self.work),'exit_code':p.returncode,'stdout':p.stdout,'stderr':p.stderr},indent=2)+'\n')
   self.assertEqual(p.returncode,0,p.stdout+p.stderr);return p
  ceiling=subprocess.run(['git','rev-parse','--show-toplevel'],cwd=tree,env=env,capture_output=True,text=True)
  self.assertNotEqual(ceiling.returncode,0,'Private source tree discovered outer checkout')
  commit=json.loads((ROOT/'dependencies.lock.json').read_text())['repositories']['melee']['commit']
  rels=['src/sysdolphin/baselib/'+n for n in ['memory.c','memory.h','class.c','lobj.c']]
  for rel in rels:
   q=tree/rel;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(subprocess.check_output(['git','show',commit+':'+rel],cwd=ROOT/'.deps/melee'))
  patch=SOURCE/'patches/melee-gameplay.patch';patch=patch if patch.is_file() else ROOT/'patches/melee-gameplay.patch'
  run('apply',['git','apply',*['--include='+x for x in rels],patch],tree)
  # Same bool ABI lowering as existing source preparation; private copies only.
  for name in ['class.c','lobj.c']:
   q=tree/'src/sysdolphin/baselib'/name;q.write_text(re.sub(r'\bbool\b','melee_source_bool',q.read_text()))
  body=function((tree/'src/sysdolphin/baselib/lobj.c').read_text(),'setup_spec_lightobj')
  self.assertEqual(body.count('melee_web_lobj_writer_before('),1);self.assertEqual(body.count('melee_web_lobj_writer_after('),1)
  template=SOURCE/'tests/lobj_writer_producer_control.c.in';producer=self.work/'producer.c';producer.write_text(template.read_text().replace('@ACTUAL_SETUP_SPEC_LIGHTOBJ@',body))
  common=['-DTARGET_PC','-ffp-contract=off','-I'+str(overlay/'src'),'-I'+str(tree/'src'),'-I'+str(prepared),'-I'+str(prepared/'sysdolphin/baselib'),'-I'+str(ROOT/'.deps/aurora/include'),'-include',str(overlay/'src/gameplay_compat.h')]
  recipes=None
  if 'MELEE_LOBJ_WRITER_COMPILE_COMMANDS' not in os.environ:
   for candidate in sorted((ROOT/'build').glob('*/compile_commands.json')):
    graph=json.loads(candidate.read_text())
    if any(x['file'].endswith('/GXLighting.cpp') for x in graph):recipes=graph;break
  if 'MELEE_LOBJ_WRITER_COMPILE_COMMANDS' in os.environ:
   rp=Path(os.environ['MELEE_LOBJ_WRITER_COMPILE_COMMANDS']);self.assertTrue(rp.is_file(),'Explicit compile graph missing');recipes=json.loads(rp.read_text())
  def compile_source(name,source,mode,cpp=False):
   out=self.work/(mode+'-'+name+'.o');active=mode=='active'
   if recipes and name in ['producer','vec','mtxvec','GXLighting','psmath']:
    suffix={'producer':'/lobj.c','vec':'/vec.c','mtxvec':'/mtxvec.c','GXLighting':'/GXLighting.cpp','psmath':'/gameplay_ps_math.c'}[name]
    matches=[x for x in recipes if x['file'].endswith(suffix)];self.assertEqual(len(matches),1)
    cmd=shlex.split(matches[0]['command']);i=cmd.index('-o');del cmd[i:i+2];i=cmd.index('-c');del cmd[i:i+2]
    cmd=[x.replace(str(ROOT/'src/gameplay_compat.h'),str(overlay/'src/gameplay_compat.h')) for x in cmd if not x.startswith('-DMELEE_WEB_CPU_INTERVAL_ENABLED')]
    cmd+=[*common,'-DMELEE_WEB_CPU_INTERVAL_ENABLED=1' if active else '-UMELEE_WEB_CPU_INTERVAL_ENABLED','-c',str(source),'-o',str(out)]
   else:cmd=[em/('em++' if cpp else 'emcc'),'-std=c++20' if cpp else '-std=gnu11','-O3','-DNDEBUG',*common,'-DMELEE_WEB_CPU_INTERVAL_ENABLED=1' if active else '-UMELEE_WEB_CPU_INTERVAL_ENABLED','-c',source,'-o',out]
   run(mode+'-compile-'+name,cmd);return out
  for mode in ['active','inactive']:
   objects=[compile_source(n,tree/('src/sysdolphin/baselib/'+n+'.c'),mode) for n in ['memory','class']]
   objects+=[compile_source('producer',producer,mode),compile_source('psmath',ROOT/'src/gameplay_ps_math.c',mode)]
   for n,suffix in [('vec','vec.c'),('mtxvec','mtxvec.c'),('GXLighting','GXLighting.cpp')]:
    q=ROOT/'.deps/aurora/lib/dolphin'/('gx' if n=='GXLighting' else 'mtx')/suffix
    if not recipes and n=='GXLighting':self.skipTest('Actual Aurora GX compile recipe required; configure existing runtime first or pass explicit verified compile graph')
    objects.append(compile_source(n,q,mode,cpp=n=='GXLighting'))
   inputs=[overlay/'tests/source_lobj_writer_control.cpp',*objects]
   if mode=='active':inputs += [ROOT/'src/source_address_context.cpp',ROOT/'src/source_game_heap_context.cpp']
   binary=self.work/(mode+'.js');run(mode+'-link',[em/'em++','-std=c++20','-O1','-fexceptions',*common,'-DMELEE_WEB_CPU_INTERVAL_ENABLED=1' if mode=='active' else '-UMELEE_WEB_CPU_INTERVAL_ENABLED',*inputs,'-Wl,--wrap=calloc','-sDISABLE_EXCEPTION_CATCHING=0','-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-o',binary])
   for case in (['phase','bounds','extent','pending','missing','retired','stale-session','overflow','reuse','wrong-comparison','wrong-row','unknown','recreated','mirror','wrong-ticket'] if mode=='active' else ['inactive']):
    result=run(mode+'-'+case,[node_runtime(),binary,case])
    if mode=='active' and case=='phase':
     marker='ACTUAL_LOBJ_WRITER_BASE64=';encoded=next(x.split(marker,1)[1] for x in result.stdout.splitlines() if x.startswith(marker));raw=base64.b64decode(encoded,validate=True);self.assertEqual(len(raw),16784)
     raw_path=self.work/'actual-writer.bin';raw_path.write_bytes(raw)
     module=SOURCE/'src/slippi_profile/tests/cpu_two_tick_runner.mjs'
     if not module.is_file():module=ROOT/'src/slippi_profile/tests/cpu_two_tick_runner.mjs'
     js="import fs from 'node:fs';import {decodeCpuLObjWriterEvidence} from "+json.dumps(module.as_uri())+";const e=decodeCpuLObjWriterEvidence(new Uint8Array(fs.readFileSync("+json.dumps(str(raw_path))+")));if(e.status!==1||e.postCount!==1||e.drawnCount!==2||e.matchedIndex!==0||e.livePieceLifetime!=='UNKNOWN')throw Error('actual native decoder bridge');console.log('actual16784 bridge POST writer9, excluded later writer10; lifetime UNKNOWN');"
     run('actual-decoder-bridge',[node_runtime(),'--input-type=module','--eval',js])
  print('Actual producer/runtime controls retained at',self.work)
if __name__=='__main__':unittest.main()
