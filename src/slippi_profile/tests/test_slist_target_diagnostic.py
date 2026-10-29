# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual patched SList/ObjAlloc stores and runtime; modeled OS backing leaves."""
from pathlib import Path
import json,os,sys,tempfile,subprocess,unittest,hashlib,re
ROOT=Path(__file__).resolve().parents[3]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'tests')]
from gameplay_bool import transform_source
from check_gameplay import node_runtime
from owned_test_workspace import OwnedWorkspaceTests
class SListTargetDiagnosticTests(OwnedWorkspaceTests):
 @classmethod
 def setUpClass(cls):
  parent=Path(os.environ.get('MELEE_SLIST_TRACE_OUTPUT_ROOT',ROOT/'work'));parent.mkdir(parents=True,exist_ok=True)
  cls.work=(Path(tempfile.mkdtemp(prefix='slist-target-',dir=parent)) if 'MELEE_SLIST_TRACE_OUTPUT_ROOT' in os.environ else cls.new_workspace(ROOT,'slist-target-'))
 def test_actual_list_stores_and_safe_membership(self):
  em=ROOT/'.deps/emsdk/upstream/emscripten';prepared=ROOT/'build/gameplay-source/src'
  if not (em/'emcc').is_file() or not (prepared/'sysdolphin/baselib/list.h').is_file():self.skipTest('Pinned source/compiler unavailable')
  work=self.work;tree=work/'source';tree.mkdir();env=dict(os.environ,TMPDIR=str(work),GIT_CEILING_DIRECTORIES=str(work))
  for k in ('GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE','GIT_COMMON_DIR'):env.pop(k,None)
  def run(name,args):
   p=subprocess.run(list(map(str,args)),cwd=tree,env=env,capture_output=True,text=True,timeout=90)
   (work/(name+'.json')).write_text(json.dumps({'argv':list(map(str,args)),'exit_code':p.returncode,'stdout':p.stdout,'stderr':p.stderr},indent=2)+'\n')
   self.assertEqual(p.returncode,0,p.stdout+p.stderr);return p
  names=['memory','class','objalloc','list','lobj'];rels=['src/sysdolphin/baselib/'+n+'.c' for n in names]
  for rel in rels:
   p=tree/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(subprocess.check_output(['git','show','HEAD:'+rel],cwd=ROOT/'.deps/melee'))
  # class.h must have the actual private allocator wrapper declaration.
  header='src/sysdolphin/baselib/class.h';(tree/header).write_bytes(subprocess.check_output(['git','show','HEAD:'+header],cwd=ROOT/'.deps/melee'))
  run('base-apply',['git','apply',*['--include='+x for x in [*rels,header]],ROOT/'patches/melee-gameplay.patch'])
  for rel in [*rels,header]:
   p=tree/rel;p.write_text(transform_source(Path(rel),p.read_text()))
  before={rel:(tree/rel).read_text() for rel in rels}
  run('profile-apply',['git','apply',*['--include='+x for x in rels[2:]],ROOT/'src/slippi_profile/patches/melee-profile-hooks.patch'])
  # Exact original list/allocator expressions and call order survive diagnostic
  # removal (including both AppendList stores and the source alias case).
  for rel in rels[2:4]:
   lines=(tree/rel).read_text().splitlines(True);kept=[];i=0
   while i<len(lines):
    if lines[i].startswith('#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED)') and i+1<len(lines) and 'MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE' in lines[i+1]:
     depth=1;i+=1
     while depth:
      if lines[i].startswith('#if'):depth+=1
      if lines[i].startswith('#endif'):depth-=1
      i+=1
     continue
    if re.match(r'\s*SLIST_TRACE_(BEGIN|END)\(',lines[i]):i+=1;continue
    kept.append(lines[i]);i+=1
   self.assertEqual(re.sub(r'\s+','',before[rel]),re.sub(r'\s+','',''.join(kept)),rel)
  # Bounded source-hook inventory catches a missing definition-site insertion.
  list_source=(tree/rels[3]).read_text()
  self.assertEqual(list_source.count('SLIST_TRACE_BEGIN(trace0, MELEE_WEB_SLIST_TRACE_APPEND_LIST'),1)
  self.assertEqual(list_source.count('SLIST_TRACE_BEGIN(trace1, MELEE_WEB_SLIST_TRACE_APPEND_LIST'),1)
  self.assertEqual(list_source.count('SLIST_TRACE_BEGIN(trace, MELEE_WEB_SLIST_TRACE_PREPEND_LIST'),1)
  for rel in rels[2:4]:
   text=(tree/rel).read_text();self.assertEqual(text.count('SLIST_TRACE_BEGIN(trace, MELEE_WEB_SLIST_TRACE_ALLOC_APPEND_DATA'),1 if rel.endswith('list.c') else 0)
  self.assertEqual((tree/rels[4]).read_text().count('void melee_web_lobj_slist_trace_sample('),1)
  common=['-DTARGET_PC','-DMELEE_WEB_CPU_INTERVAL_ENABLED=1','-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1','-ffp-contract=off','-I'+str(ROOT/'src'),'-I'+str(tree/'src'),'-I'+str(prepared),'-I'+str(prepared/'sysdolphin/baselib'),'-I'+str(ROOT/'.deps/aurora/include'),'-include',str(ROOT/'src/gameplay_compat.h')]
  objs=[]
  for name in names[:4]:
   obj=work/(name+'.o');run('compile-'+name,[em/'emcc','-std=gnu11','-O1',*common,'-c',tree/('src/sysdolphin/baselib/'+name+'.c'),'-o',obj]);objs.append(obj)
  fixture=work/'control.cpp';fixture.write_text((ROOT/'src/slippi_profile/tests/slist_target_control.cpp.in').read_text())
  binary=work/'control.js';run('link',[em/'em++','-std=c++17','-O1','-fexceptions',*common,fixture,*objs,ROOT/'src/source_address_context.cpp',ROOT/'src/source_game_heap_context.cpp','-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-o',binary])
  for mode in ('stores','bounds','retained','overflow'):
   p=run(mode,[node_runtime(),binary,mode]);lines=[l for l in p.stdout.splitlines() if l.startswith('MELEE_CLASS_PIECE_SLAB_TRACE ')]
   self.assertTrue(lines);self.assertTrue(all(len(l)<=4096 for l in lines));rows=[json.loads(l.split(' ',1)[1]) for l in lines];summary=next(x for x in rows if x['type']=='slist_summary');self.assertFalse(summary['complete'])
   events=[e for x in rows if x['type']=='slist_events' for e in x['events']]
   if mode=='stores':
    self.assertTrue(any(x['op']=='OBJFREE' and x['changed'] for x in events));self.assertTrue(any(x['op']=='ALLOC_ZERO' for x in events))
    for e in events:
     if e['op'] not in ('OBJALLOC','OBJFREE'):self.assertEqual(e['used_free'][:2],[None,None])
   if mode=='retained':self.assertGreater(summary['refusal'],0)
   if mode=='overflow':self.assertTrue(summary['overflow']);self.assertEqual(summary['event_count'],32)
  (work/'pins.json').write_text(json.dumps({str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'src/slippi_profile/slist_target_diagnostic.inc',ROOT/'src/slippi_profile/patches/melee-profile-hooks.patch']},indent=2)+'\n')
if __name__=='__main__':unittest.main()
