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

ROOT=Path(os.environ.get("MELEE_POOL_PROVENANCE_REPO_ROOT",Path(__file__).resolve().parents[1])).resolve()
SOURCE=Path(os.environ.get("MELEE_POOL_PROVENANCE_CANDIDATE_ROOT",ROOT)).resolve()
sys.path.insert(0,str(ROOT/"tests"))
sys.path.insert(0,str(ROOT/"scripts"))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime

class SourcePoolProvenanceTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        if "MELEE_POOL_PROVENANCE_OUTPUT_ROOT" in os.environ:
            parent=Path(os.environ["MELEE_POOL_PROVENANCE_OUTPUT_ROOT"]).resolve()
            parent.mkdir(parents=True,exist_ok=True)
            cls.work=Path(tempfile.mkdtemp(prefix="pool-provenance-",dir=parent))
        else:cls.work=cls.new_workspace(ROOT,"pool-provenance-")

    def test_actual_original_allocator_and_fixed_runtime_diagnostics(self):
        em=ROOT/".deps/emsdk/upstream/emscripten"
        if not (em/"emcc").is_file():self.skipTest("Pinned Emscripten unavailable")
        prepared=Path(os.environ.get("MELEE_POOL_PROVENANCE_PREPARED_SOURCE",ROOT/"build/gameplay-source/src")).resolve()
        if not (prepared/"sysdolphin/baselib/objalloc.h").is_file():
            if "MELEE_POOL_PROVENANCE_PREPARED_SOURCE" in os.environ:self.fail("Explicit prepared source unavailable")
            self.skipTest("Existing generated source headers unavailable; use supported build entry point")
        headers=self.work/"headers";headers.mkdir()
        # Real copied headers preserve the forced include's quoted sibling chain.
        for p in (ROOT/"src").rglob("*"):
            if p.is_file() and p.suffix in (".h",".hpp"):
                rel=p.relative_to(ROOT/"src");target=SOURCE/"src"/rel
                if not target.is_file():target=p
                q=headers/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(target,q)
        tree=self.work/"original";unit=tree/"src/sysdolphin/baselib/objalloc.c";unit.parent.mkdir(parents=True)
        lock=json.loads((ROOT/"dependencies.lock.json").read_text());commit=lock["repositories"]["melee"]["commit"]
        unit.write_bytes(subprocess.check_output(["git","show",commit+":src/sysdolphin/baselib/objalloc.c"],cwd=ROOT/".deps/melee"))
        env=dict(os.environ,TMPDIR=str(self.work),GIT_CEILING_DIRECTORIES=str(tree.parent))
        for k in ("GIT_DIR","GIT_WORK_TREE","GIT_INDEX_FILE","GIT_COMMON_DIR"):env.pop(k,None)
        ceiling=subprocess.run(["git","rev-parse","--show-toplevel"],cwd=tree,env=env,capture_output=True,text=True)
        self.assertNotEqual(ceiling.returncode,0,"Private apply tree discovered an outer checkout")
        applied=subprocess.run(["git","apply","--include=src/sysdolphin/baselib/objalloc.c",str(SOURCE/"patches/melee-gameplay.patch")],cwd=tree,env=env,capture_output=True,text=True)
        self.assertEqual(applied.returncode,0,applied.stdout+applied.stderr)
        self.assertIn("melee_web_source_memory_objalloc_event",unit.read_text())
        common=["-DTARGET_PC","-ffp-contract=off","-I"+str(headers),"-I"+str(prepared),
                "-I"+str(prepared/"sysdolphin/baselib"),"-I"+str(ROOT/".deps/aurora/include"),
                "-include",str(headers/"gameplay_compat.h")]
        def run(name,command):
            p=subprocess.run(list(map(str,command)),cwd=self.work,env=env,text=True,capture_output=True,timeout=120)
            (self.work/(name+".json")).write_text(json.dumps({"command":list(map(str,command)),"exit_code":p.returncode,"stdout":p.stdout,"stderr":p.stderr},indent=2)+"\n")
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            return p
        for active in (True,False):
            mode="active" if active else "inactive";flags=["-DMELEE_WEB_CPU_INTERVAL_ENABLED=1"] if active else []
            obj=self.work/(mode+".o");js=self.work/(mode+".js")
            run(mode+"-c",[em/"emcc","-std=gnu11","-O1",*common,*flags,"-c",unit,"-o",obj])
            inputs=[SOURCE/"tests/source_pool_provenance_control.cpp",obj]
            if active:inputs += [SOURCE/"src/gameplay_source_memory_runtime.cpp",ROOT/"src/source_address_context.cpp",ROOT/"src/source_game_heap_context.cpp"]
            run(mode+"-link",[em/"em++","-std=c++17","-O1","-fexceptions",*common,*flags,*inputs,
                              "-sDISABLE_EXCEPTION_CATCHING=0","-sENVIRONMENT=node","-sEXIT_RUNTIME=1","-o",js])
            for case in (["success","conflict","foreign","overflow","internal","mirror","reuse","bad-mask"] if active else ["inactive"]):
                result=run(mode+"-"+case,[node_runtime(),js,case])
                if active and case=="success":
                    marker="ACTUAL_POOL_FAILURE_WITNESS_BASE64="
                    records=[x[len(marker):] for x in result.stdout.splitlines() if x.startswith(marker)]
                    self.assertEqual(len(records),1)
                    raw=base64.b64decode(records[0],validate=True);self.assertEqual(len(raw),256)
                    binary=self.work/"actual-pool-witness.bin";binary.write_bytes(raw)
                    helpers=self.work/"js";helpers.mkdir()
                    for name in ("cpu_two_tick_runner.mjs","cpu_restore_runner.mjs"):
                        source=SOURCE/"src/slippi_profile/tests"/name
                        if not source.is_file():source=ROOT/"src/slippi_profile/tests"/name
                        shutil.copy2(source,helpers/name)
                    bridge="""const fs=await import('node:fs');const {pathToFileURL}=await import('node:url');
const {decodeCpuPayloadFailureWitness}=await import(pathToFileURL(process.argv[1]));
const d=decodeCpuPayloadFailureWitness(new Uint8Array(fs.readFileSync(process.argv[2])));
if(d.originalStatus!==2||d.firstDifferenceMatches!==1||d.attribution!==1||d.stride!==32||d.count!==2||d.alignmentMask!==3||d.cellOffset!==20)throw Error('actual source producer bridge drift');
console.log(JSON.stringify({status:'PASS',scope:'actual source allocator/runtime; memory leaves modeled',row:d.window.row,offset:d.window.offset,descriptor:d.descriptor}));"""
                    run("actual-producer-js-bridge",[node_runtime(),"--input-type=module","-e",bridge,helpers/"cpu_two_tick_runner.mjs",binary])

if __name__=="__main__":unittest.main()
