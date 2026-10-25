"""Actual allocator event + runtime provenance; modeled OS/memory leaves."""
from pathlib import Path
import json
import base64
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(os.environ.get("MELEE_ALLOCATION_ORIGIN_REPO_ROOT",Path(__file__).resolve().parents[1])).resolve()
SOURCE=Path(os.environ.get("MELEE_ALLOCATION_ORIGIN_CANDIDATE_ROOT",ROOT)).resolve()
sys.path.insert(0,str(ROOT/"tests"))
sys.path.insert(0,str(ROOT/"scripts"))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime

class SourceAllocationOriginTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        if "MELEE_ALLOCATION_ORIGIN_OUTPUT_ROOT" in os.environ:
            parent=Path(os.environ["MELEE_ALLOCATION_ORIGIN_OUTPUT_ROOT"]).resolve()
            parent.mkdir(parents=True,exist_ok=True)
            cls.work=Path(tempfile.mkdtemp(prefix="allocation-origin-",dir=parent))
        else:cls.work=cls.new_workspace(ROOT,"allocation-origin-")

    def test_actual_memory_class_allocator_and_origin(self):
        em=ROOT/".deps/emsdk/upstream/emscripten"
        if not (em/"emcc").is_file():self.skipTest("Pinned Emscripten unavailable")
        prepared=Path(os.environ.get("MELEE_ALLOCATION_ORIGIN_PREPARED_SOURCE",ROOT/"build/gameplay-source/src")).resolve()
        if not (prepared/"sysdolphin/baselib/objalloc.h").is_file():
            if "MELEE_ALLOCATION_ORIGIN_PREPARED_SOURCE" in os.environ:self.fail("Explicit prepared source unavailable")
            self.skipTest("Existing generated source headers unavailable; use supported build entry point")
        headers=self.work/"headers";headers.mkdir()
        # Real copied headers preserve the forced include's quoted sibling chain.
        for p in (ROOT/"src").rglob("*"):
            if p.is_file() and p.suffix in (".h",".hpp"):
                rel=p.relative_to(ROOT/"src");target=SOURCE/"src"/rel
                if not target.is_file():target=p
                q=headers/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(target,q)
        tree=self.work/"original";tree.mkdir()
        lock=json.loads((ROOT/"dependencies.lock.json").read_text());commit=lock["repositories"]["melee"]["commit"]
        rels=["src/sysdolphin/baselib/"+x for x in ("memory.c","memory.h","class.c")]
        for rel in rels:
            q=tree/rel;q.parent.mkdir(parents=True,exist_ok=True)
            q.write_bytes(subprocess.check_output(["git","show",commit+":"+rel],cwd=ROOT/".deps/melee"))
        env=dict(os.environ,TMPDIR=str(self.work),GIT_CEILING_DIRECTORIES=str(tree.parent))
        for k in ("GIT_DIR","GIT_WORK_TREE","GIT_INDEX_FILE","GIT_COMMON_DIR"):env.pop(k,None)
        ceiling=subprocess.run(["git","rev-parse","--show-toplevel"],cwd=tree,env=env,capture_output=True,text=True)
        self.assertNotEqual(ceiling.returncode,0,"Private apply tree discovered outer checkout")
        applied=subprocess.run(["git","apply",*["--include="+x for x in rels],str(SOURCE/"patches/melee-gameplay.patch")],cwd=tree,env=env,capture_output=True,text=True)
        self.assertEqual(applied.returncode,0,applied.stdout+applied.stderr)
        # Source preparation's existing bool ABI lowering is required with its
        # matching headers. Copy the already-prepared actual class TU read-only;
        # never invoke preparation from a test. Allocation body is unchanged.
        raw_class=(tree/"src/sysdolphin/baselib/class.c").read_text()
        prepared_class=(prepared/"sysdolphin/baselib/class.c").read_text()
        def alloc_body(text):return text.split("void* hsdAllocMemPiece(s32 size)",1)[1].split("void hsdFreeMemPiece",1)[0]
        self.assertEqual(alloc_body(raw_class),alloc_body(prepared_class),"Prepared allocator body drift")
        (tree/"src/sysdolphin/baselib/class.c").write_text(prepared_class)
        (self.work/"prepared-class-input.json").write_text(json.dumps({"path":str(prepared/"sysdolphin/baselib/class.c"),"sha256":__import__('hashlib').sha256(prepared_class.encode()).hexdigest()},indent=2)+"\n")
        common=["-DTARGET_PC","-ffp-contract=off","-I"+str(headers),"-I"+str(tree/"src"),"-I"+str(prepared),
                "-I"+str(prepared/"sysdolphin/baselib"),"-I"+str(ROOT/".deps/aurora/include"),
                "-include",str(headers/"gameplay_compat.h")]
        def run(name,command):
            p=subprocess.run(list(map(str,command)),cwd=self.work,env=env,text=True,capture_output=True,timeout=120)
            (self.work/(name+".json")).write_text(json.dumps({"command":list(map(str,command)),"exit_code":p.returncode,"stdout":p.stdout,"stderr":p.stderr},indent=2)+"\n")
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            return p
        for active in (True,False):
            mode="active" if active else "inactive";flags=["-DMELEE_WEB_CPU_INTERVAL_ENABLED=1"] if active else []
            objects=[]
            for name in ("memory","class"):
                obj=self.work/(mode+"-"+name+".o");objects.append(obj)
                run(mode+"-"+name,[em/"emcc","-std=gnu11","-O1",*common,*flags,"-c",tree/("src/sysdolphin/baselib/"+name+".c"),"-o",obj])
            js=self.work/(mode+".js");inputs=[SOURCE/"tests/source_allocation_origin_control.cpp",*objects]
            if active:inputs += [SOURCE/"src/gameplay_source_memory_runtime.cpp",ROOT/"src/source_address_context.cpp",ROOT/"src/source_game_heap_context.cpp"]
            run(mode+"-link",[em/"em++","-std=c++17","-O1","-fexceptions",*common,*flags,*inputs,
                              "-sDISABLE_EXCEPTION_CATCHING=0","-sENVIRONMENT=node","-sEXIT_RUNTIME=1","-o",js])
            for case in (["class","conflict","foreign","extent","retired","os","bounds","mirror","reuse"] if active else ["inactive"]):
                result=run(mode+"-"+case,[node_runtime(),js,case])
                if active and case=="class":
                    marker="ACTUAL_ALLOCATION_ORIGIN_BASE64="
                    records=[x[len(marker):] for x in result.stdout.splitlines() if x.startswith(marker)]
                    self.assertEqual(len(records),1);raw=base64.b64decode(records[0],validate=True);self.assertEqual(len(raw),320)
                    (self.work/"actual-origin.bin").write_bytes(raw)
                    helpers=self.work/"js";helpers.mkdir()
                    for name in ("cpu_two_tick_runner.mjs","cpu_restore_runner.mjs"):
                        source=SOURCE/"src/slippi_profile/tests"/name
                        if not source.is_file():source=ROOT/"src/slippi_profile/tests"/name
                        shutil.copy2(source,helpers/name)
                    bridge="""const fs=await import('node:fs');const {pathToFileURL}=await import('node:url');
const {decodeCpuAllocationOriginWitness}=await import(pathToFileURL(process.argv[1]));
const d=decodeCpuAllocationOriginWitness(new Uint8Array(fs.readFileSync(process.argv[2])));
if(d.status!==1||d.siteKind!==1||!d.path.endsWith('/class.c')||!d.line||!d.firstDifferenceMatches)throw Error('actual class origin bridge drift');
console.log(JSON.stringify({status:'PASS',scope:'actual memory/class/runtime; OS/assert leaves modeled',path:d.path,line:d.line}));"""
                    run("actual-origin-js-bridge",[node_runtime(),"--input-type=module","-e",bridge,helpers/"cpu_two_tick_runner.mjs",self.work/"actual-origin.bin"])
            failure=subprocess.run([str(node_runtime()),str(js),"assert-failure"],cwd=self.work,env=env,capture_output=True,text=True,timeout=30)
            (self.work/(mode+"-original-assert.json")).write_text(json.dumps({"exit_code":failure.returncode,"stdout":failure.stdout,"stderr":failure.stderr},indent=2)+"\n")
            self.assertEqual(failure.returncode,73);self.assertIn("ORIGINAL_ASSERT calls=1 assertions=1",failure.stdout)


if __name__=="__main__":unittest.main()
