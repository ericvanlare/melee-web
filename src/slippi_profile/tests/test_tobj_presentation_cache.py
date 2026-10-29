# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual canonical TObj/class sources and typed V8 owner; explicit providers.

No source preparation or build mutation. Original and proposed executables use
one composed source tree, matching prepared headers and production FP options.
"""
from pathlib import Path
import hashlib,json,os,re,shlex,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(os.environ.get('MELEE_TOBJ_CACHE_REPO_ROOT',Path(__file__).resolve().parents[3])).resolve()
SOURCE=Path(os.environ.get('MELEE_TOBJ_CACHE_CANDIDATE_ROOT',ROOT)).resolve()
_saved_path=sys.path.copy();sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'scripts')]
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime
from gameplay_bool import composed_patch,transform_source
from gameplay_sources import compose_slippi_profile_patch
sys.path[:]=_saved_path

def function(text,name):
    m=re.search(r'^(?:static )?(?:void|HSD_TObj\*) '+re.escape(name)+r'\([^;]*?\)\s*\{',text,re.M)
    if not m:raise AssertionError('Actual function missing: '+name)
    i=text.index('{',m.start())+1;depth=1
    while depth:
        if text[i]=='{':depth+=1
        elif text[i]=='}':depth-=1
        i+=1
    return text[m.start():i]

def selected(relative):
    p=SOURCE/relative
    return p if p.is_file() else ROOT/relative

class TObjPresentationCacheTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        external=os.environ.get('MELEE_TOBJ_CACHE_OUTPUT_ROOT')
        if not external:cls.work=cls.new_workspace(ROOT,'tobj-cache-');return
        requested=Path(external)
        if requested.is_symlink():raise ValueError('external evidence symlink refused')
        parent=requested.resolve();parent.mkdir(parents=True,exist_ok=True)
        cls.work=Path(tempfile.mkdtemp(prefix='tobj-cache-',dir=parent));cls._workspace_failed=False;cls._workspace_tests_started=False;cls._workspace_result=None
        # Explicit external evidence is retained, never blanket-cleaned.

    def run_command(self,name,argv,cwd=None,env=None,expected=0):
        command=list(map(str,argv));record={'argv':command,'cwd':str(cwd or self.work)}
        try:
            p=subprocess.run(command,cwd=cwd or self.work,env=env,text=True,capture_output=True,timeout=180)
            record.update(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
        except subprocess.TimeoutExpired as e:
            record.update(returncode=None,timed_out=True,stdout=(e.stdout or b'').decode(errors='replace') if isinstance(e.stdout,bytes) else e.stdout,stderr=(e.stderr or b'').decode(errors='replace') if isinstance(e.stderr,bytes) else e.stderr)
            (self.work/(name+'.json')).write_text(json.dumps(record,indent=2)+'\n');raise
        (self.work/(name+'.json')).write_text(json.dumps(record,indent=2)+'\n')
        self.assertEqual(p.returncode,expected,p.stdout+p.stderr);return p.stdout

    def test_actual_typed_owner_and_original_same_history(self):
        em=ROOT/'.deps/emsdk/upstream/emscripten'
        if not (em/'emcc').is_file():self.skipTest('Pinned Emscripten unavailable')
        lock=json.loads((ROOT/'dependencies.lock.json').read_text());self.assertEqual((em/'emscripten-version.txt').read_text().strip().strip('"'),lock['emscripten'])
        db=Path(os.environ.get('MELEE_TOBJ_CACHE_COMPILE_COMMANDS',ROOT/'build/slippi-synctest-logical-frame-lobj-presentation-stage-arena-aurora-staging-release/compile_commands.json'))
        if not db.is_file():
            if 'MELEE_TOBJ_CACHE_COMPILE_COMMANDS' in os.environ:self.fail('Explicit compile graph missing')
            self.skipTest('Existing private compile graph unavailable; unit tests do not prepare it')
        entries=json.loads(db.read_text());class_entry=[x for x in entries if x['file'].endswith('/src/sysdolphin/baselib/class.c')];self.assertEqual(len(class_entry),1)
        prepared=Path(class_entry[0]['file']).resolve().parents[2]
        for rel in ['Runtime/platform.h','sysdolphin/baselib/class.h','sysdolphin/baselib/object.h','sysdolphin/baselib/tobj.h']:
            self.assertTrue((prepared/rel).is_file(),'Actual prepared declaration missing '+rel)
        overlay=self.work/'overlay';(overlay/'src/slippi_profile/tests').mkdir(parents=True)
        for base in [ROOT,SOURCE]:
            for p in (base/'src').rglob('*'):
                if p.is_file() and p.suffix in ['.h','.hpp','.inc']:
                    q=overlay/'src'/p.relative_to(base/'src');q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
        shutil.copy2(selected('src/slippi_profile/tobj_presentation_cache.c'),overlay/'src/slippi_profile/tobj_presentation_cache.c')
        control=overlay/'src/slippi_profile/tests/control.c';control.write_text(selected('src/slippi_profile/tests/tobj_presentation_owner_control.c.in').read_text())
        tree=self.work/'source';tree.mkdir();env=dict(os.environ,TMPDIR=str(self.work),GIT_CEILING_DIRECTORIES=str(tree.parent))
        for k in ['GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE','GIT_COMMON_DIR']:env.pop(k,None)
        ceiling=subprocess.run(['git','rev-parse','--show-toplevel'],cwd=tree,env=env,text=True,capture_output=True);self.assertNotEqual(ceiling.returncode,0)
        self.run_command('clone',['git','clone','--shared','--no-checkout',ROOT/'.deps/melee',tree],env=env)
        commit=lock['repositories']['melee']['commit'];self.assertEqual(subprocess.check_output(['git','rev-parse','HEAD'],cwd=tree,text=True).strip(),commit)
        composed=compose_slippi_profile_patch(tree,composed_patch(tree,selected('patches/melee-gameplay.patch')),selected('src/slippi_profile/patches/melee-profile-hooks.patch'))
        rels=['src/sysdolphin/baselib/'+x+'.c' for x in ['class','object','tobj','shadow']]
        for rel in rels:
            p=tree/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(subprocess.check_output(['git','show',commit+':'+rel],cwd=tree))
        self.run_command('apply-composed',['git','apply',*['--include='+r for r in rels],composed],cwd=tree,env=env)
        for rel in rels:
            p=tree/rel;p.write_text(transform_source(Path(rel),p.read_text()))
        # Whole class/object/TObj source TUs; only named Shadow construction
        # extracted, with original source providers. Full Shadow Setup/GPU unrun.
        tobj=tree/rels[2]
        tobj.write_text(tobj.read_text()+'''
void fixture_tobj_update_type(HSD_TObj* p,u32 type,float v){HSD_ObjData d;memset(&d,0,sizeof(d));d.fv=v;TObjUpdateFunc(p,(enum_t)type,&d);}
void fixture_tobj_update(HSD_TObj* p,float v){fixture_tobj_update_type(p,HSD_A_T_TRAV,v);}
void fixture_tobj_setup(HSD_TObj* p){TObjSetupMtx(p);}
void fixture_actual_make(HSD_TObj* p){MakeTextureMtx(p);}
void fixture_tobj_amnesia(void){TObjAmnesia((HSD_ClassInfo*)&hsdTObj);}
int fixture_tobj_default_callbacks(void){return hsdTObj.make_mtx==MakeTextureMtx&&hsdTObj.load==TObjLoad&&hsdTObj.make_texp==TObjMakeTExp&&hsdTObj.parent.release==TObjRelease&&hsdTObj.parent.amnesia==TObjAmnesia;}
int fixture_tobj_default_is_null(void){return default_class==NULL;}
int fixture_reload(HSD_TObj* p,HSD_TObjDesc* d){return HSD_TOBJ_METHOD(p)->load(p,d);}
u32 fixture_flags(HSD_TObj* p){
#if MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
return slippi_tobj_effective_flags(p);
#else
return p->flags;
#endif
}
MtxPtr fixture_matrix(HSD_TObj* p){
#if MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
return slippi_tobj_matrix_require(p);
#else
return p->mtx;
#endif
}
''')
        shadow=self.work/'shadow-construction.c';shadow.write_text('#include <sysdolphin/baselib/tobj.h>\n#include "slippi_profile/tobj_presentation_cache.h"\n'+function((tree/rels[3]).read_text(),'makeShadowTObj')+'\nHSD_TObj* fixture_actual_shadow(void){return makeShadowTObj();}\n')
        mathsrc=self.work/'rotation.c';mathsrc.write_text('#include <sysdolphin/baselib/mtx.h>\n#include <math.h>\n'+function((prepared/'sysdolphin/baselib/mtx.c').read_text(),'HSD_MkRotationMtx')+'\n')
        def compile_one(name,source,mode,suffix):
            rows=[x for x in entries if x['file'].endswith(suffix)];self.assertEqual(len(rows),1,suffix)
            args=shlex.split(rows[0]['command']);i=args.index('-o');del args[i:i+2];i=args.index('-c');del args[i:i+2]
            # Only opt-in mode changes; preserve port, math, ABI and all actual
            # dependency flags. Prepared declarations remain first, not raw SDK.
            args=[x.replace(str(ROOT/'src/gameplay_compat.h'),str(overlay/'src/gameplay_compat.h')) for x in args if not x.startswith(('-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE','-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE'))]
            active=mode=='active';out=self.work/(mode+'-'+name+'.o')
            args += ['-I'+str(overlay/'src'),'-I'+str(prepared),'-I'+str(prepared/'sysdolphin/baselib'),'-UNDEBUG','-DPROPOSED_LANE='+str(int(active)),'-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE='+str(int(active)),'-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE='+str(int(active)),'-c',str(source),'-o',str(out)]
            self.run_command(mode+'-compile-'+name,args,env=env);return out
        source_map=[('class',tree/rels[0],'/src/sysdolphin/baselib/class.c'),('object',tree/rels[1],'/src/sysdolphin/baselib/class.c'),('tobj',tobj,'/src/sysdolphin/baselib/tobj.c'),('shadow',shadow,'/src/sysdolphin/baselib/tobj.c'),('rotation',mathsrc,'/src/sysdolphin/baselib/tobj.c'),('psmath',ROOT/'src/gameplay_ps_math.c','/src/gameplay_ps_math.c'),('sdkmtx',ROOT/'.deps/aurora/lib/dolphin/mtx/mtx.c','/lib/dolphin/mtx/mtx.c'),('bounds',ROOT/'src/hsd_texture_bounds.c','/src/hsd_texture_bounds.c'),('control',control,'/src/sysdolphin/baselib/tobj.c')]
        common_cases=['changed','unchanged','same-value','toon','toon-uv','shadow-loaded','reuse','rebind','amnesia','admission','missing-load','bounds-release'];outputs={}
        for mode in ['original','active']:
            objects=[compile_one(n,p,mode,s) for n,p,s in source_map]
            binary=self.work/(mode+'.js');self.run_command(mode+'-link',[em/'emcc',*objects,'-fexceptions','-sDISABLE_EXCEPTION_CATCHING=0','-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-Wl,--wrap=malloc','-Wl,--wrap=calloc','-o',binary],env=env)
            for case in common_cases+(['oom-observe-malloc-only','oom','pending-anchor','active-interval-load','shadow','all-managed','lifecycle-drift','unknown-selection','custom-default','unsupported-coordinate','world-recreation'] if mode=='active' else []):
                outputs[(mode,case)]=self.run_command(mode+'-'+case,[node_runtime(),binary,case],env=env)
        # These are two distinct HSD_Panic edges inside the same actual
        # animation callback: the original TIMG table assertion and the
        # private cache's transform-owner refusal. Keep both messages visible
        # so a browser startup abort can be attributed before changing policy.
        for mode in ['original','active']:
            name=mode+'-timg-null-table'
            self.run_command(name,[node_runtime(),self.work/(mode+'.js'),'timg-null-table'],env=env,expected=93)
            record=json.loads((self.work/(name+'.json')).read_text())
            self.assertIn('tobj->imagetbl',record['stderr']+record['stdout'])
            self.assertNotIn('Unsupported Slippi TObj presentation owner',record['stderr']+record['stdout'])
        name='active-pending-update'
        self.run_command(name,[node_runtime(),self.work/'active.js','pending-update'],env=env,expected=94)
        record=json.loads((self.work/(name+'.json')).read_text())
        self.assertIn('Unsupported Slippi TObj presentation owner',record['stderr']+record['stdout'])
        name='active-unsupported-coordinate-update'
        self.run_command(name,[node_runtime(),self.work/'active.js',name.removeprefix('active-')],env=env,expected=94)
        record=json.loads((self.work/(name+'.json')).read_text())
        diagnostic=record['stderr']+record['stdout']
        context=re.search(r'FIRST_FAILURE_CONTEXT=\{"code":9,"line":(\d+),"detail":3\}',diagnostic)
        self.assertIsNotNone(context,'poisoned animated TObj panic must report the earlier OWNER_ROLE line and SHADOW coord')
        self.assertGreater(int(context.group(1)),0)
        self.assertIn('Unsupported Slippi TObj presentation owner',diagnostic)
        self.assertLess(diagnostic.index('FIRST_FAILURE_CONTEXT='),diagnostic.index('Unsupported Slippi TObj presentation owner'))
        name='active-unsupported-highlight-update'
        self.run_command(name,[node_runtime(),self.work/'active.js','unsupported-highlight-update'],env=env,expected=94)
        record=json.loads((self.work/(name+'.json')).read_text());diagnostic=record['stderr']+record['stdout']
        self.assertRegex(diagnostic,r'FIRST_FAILURE_CONTEXT=\{"code":9,"line":\d+,"detail":2\}')
        self.assertIn('Unsupported Slippi TObj presentation owner',diagnostic)
        for case in ['shadow-cache','shadow-flags-cache','shadow-consumed-cache',
                     'shadow-loaded-matrix-cache','shadow-loaded-flags-cache',
                     'shadow-loaded-consumed-cache','unknown-cache']:
            self.run_command('active-'+case,[node_runtime(),self.work/'active.js',case],env=env,expected=94)
        reflection_cases=['reflection-unchanged','reflection-same-value','reflection-changed','reflection-reload','reflection-bump']
        reflection_outputs={}
        for case in reflection_cases:
            for mode in ['original','active']:
                name=mode+'-'+case
                reflection_outputs[(mode,case)]=self.run_command(name,[node_runtime(),self.work/(mode+'.js'),case],env=env)
            original=reflection_outputs[('original',case)]
            active=reflection_outputs[('active',case)]
            self.assertEqual(original,active,case)
            self.assertIn('ACTUAL:canonical Reflection matrix/GX histories',active,case)
            self.assertIn('GX:reflection-',active,case)
            self.assertIn('OBS:reflection-',active,case)
            for mode in ['original','active']:
                record=json.loads((self.work/(mode+'-'+case+'.json')).read_text())
                self.assertRegex(record['stderr'],r'SOURCE_CLASS_IDENTITY:[^\n]+:0x[0-9a-fA-F]+:HSD_TObj',case)
            active_record=json.loads((self.work/('active-'+case+'.json')).read_text())
            self.assertEqual(active_record['stderr'].count('SOURCE172_ACTIVE_DRAW:UNCHANGED'),active.count('GX:'),case)
        for case in common_cases:self.assertEqual(outputs[('original',case)],outputs[('active',case)],case)
        (self.work/'input-pins.json').write_text(json.dumps({'compile_graph':str(db),'sha256':hashlib.sha256(db.read_bytes()).hexdigest(),'composed_patch_sha256':hashlib.sha256(composed.read_bytes()).hexdigest(),'texture_bounds_source':str(ROOT/'src/hsd_texture_bounds.c'),'texture_bounds_sha256':hashlib.sha256((ROOT/'src/hsd_texture_bounds.c').read_bytes()).hexdigest(),'source':{r:hashlib.sha256((tree/r).read_bytes()).hexdigest() for r in rels},'scope':'actual TObj module/static source control; each lane asserts canonical HSD_TObj class identity and retains raw relocated class pointer separately; normalized SOURCE172 projection compared (not raw 172-byte binary equality); active lane checks complete 172-byte source preservation during presentation; Reflection matrix and GX 3x4 arguments captured; modeled memory/null-AObj/abort-only custom/GX sink; no full TObj resource assignment, texgen, PObj or aggregate renderer claim'},indent=2)+'\n')
        print('PASS actual typed TObj module/component scope only',self.work)

if __name__=='__main__':unittest.main()
