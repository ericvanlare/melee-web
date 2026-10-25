"""Actual allocator event + runtime provenance; modeled OS/memory leaves."""
from pathlib import Path
import json
import re
import base64
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(os.environ.get("MELEE_CLASS_PIECE_REPO_ROOT",Path(__file__).resolve().parents[1])).resolve()
SOURCE=Path(os.environ.get("MELEE_CLASS_PIECE_CANDIDATE_ROOT",ROOT)).resolve()
_import_path=sys.path.copy()
sys.path.insert(0,str(ROOT/"tests"))
sys.path.insert(0,str(ROOT/"scripts"))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime
sys.path[:]=_import_path

class SourceClassPieceTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        if "MELEE_CLASS_PIECE_OUTPUT_ROOT" in os.environ:
            parent=Path(os.environ["MELEE_CLASS_PIECE_OUTPUT_ROOT"]).resolve()
            parent.mkdir(parents=True,exist_ok=True)
            cls.work=Path(tempfile.mkdtemp(prefix="class-piece-",dir=parent))
        else:cls.work=cls.new_workspace(ROOT,"class-piece-")

    def test_actual_class_piece_lifecycle(self):
        em=ROOT/".deps/emsdk/upstream/emscripten"
        if not (em/"emcc").is_file():self.skipTest("Pinned Emscripten unavailable")
        prepared=Path(os.environ.get("MELEE_CLASS_PIECE_PREPARED_SOURCE",ROOT/"build/gameplay-source/src")).resolve()
        if not (prepared/"sysdolphin/baselib/objalloc.h").is_file():
            if "MELEE_CLASS_PIECE_PREPARED_SOURCE" in os.environ:self.fail("Explicit prepared source unavailable")
            self.skipTest("Existing generated source headers unavailable; use supported build entry point")
        headers=self.work/"headers";headers.mkdir()
        # Real copied headers preserve the forced include's quoted sibling chain.
        for p in (ROOT/"src").rglob("*"):
            if p.is_file() and p.suffix in (".h",".hpp"):
                rel=p.relative_to(ROOT/"src");target=SOURCE/"src"/rel
                if not target.is_file():target=p
                q=headers/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(target,q)
        for p in (SOURCE/"src").rglob("*"):
            if p.is_file() and p.suffix in (".h",".hpp"):
                q=headers/p.relative_to(SOURCE/"src");q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
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
        # The existing source-preparation bool ABI lowering, applied only to
        # this private patched actual C TU. No source preparation command runs.
        raw=(tree/"src/sysdolphin/baselib/class.c").read_text()
        (tree/"src/sysdolphin/baselib/class.c").write_text(re.sub(r"\bbool\b","melee_source_bool",raw))
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
            js=self.work/(mode+".js");inputs=[SOURCE/"tests/source_class_piece_control.cpp",*objects]
            if active:inputs += [ROOT/"src/source_address_context.cpp",ROOT/"src/source_game_heap_context.cpp"]
            run(mode+"-link",[em/"em++","-std=c++17","-O1","-fexceptions",*common,*flags,*inputs,
                              "-Wl,--wrap=calloc","-sDISABLE_EXCEPTION_CATCHING=0","-sENVIRONMENT=node","-sEXIT_RUNTIME=1","-o",js])
            for case in (["raw","class","drift","missing","mirror","retired","promotion","descriptor","overflow","serial","noncold","extent","oom","topology","disarm"] if active else ["inactive"]):
                result=run(mode+"-"+case,[node_runtime(),js,case])
                if active and case=="class":
                    marker="ACTUAL_CLASS_PIECE_BASE64="
                    values=[line[len(marker):] for line in result.stdout.splitlines() if line.startswith(marker)]
                    self.assertEqual(len(values),1);raw=base64.b64decode(values[0],validate=True);self.assertEqual(len(raw),208)
                    blob=self.work/"actual-class-piece.bin";blob.write_bytes(raw)
                    helper=Path(os.environ.get("MELEE_CLASS_PIECE_JS_HELPER",ROOT/"src/slippi_profile/tests/cpu_two_tick_runner.mjs"))
                    bridge="""const fs=await import('node:fs');const {pathToFileURL}=await import('node:url');
const {decodeCpuClassPieceWitness}=await import(pathToFileURL(process.argv[1]));
const d=decodeCpuClassPieceWitness(new Uint8Array(fs.readFileSync(process.argv[2])));
if(d.status!==1||d.recordState!==3||d.classObjSize!==48||d.physical!==64||d.pieceOffset!==8)throw Error('actual class bridge drift');
console.log('PASS actual memory/class/runtime208; modeled OS and descriptor fixture');"""
                    run("actual-class-js-bridge",[node_runtime(),"--input-type=module","-e",bridge,helper,blob])



if __name__=="__main__":unittest.main()
