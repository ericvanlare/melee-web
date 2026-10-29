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
        patch_rels=["src/sysdolphin/baselib/"+x for x in ("memory.c","memory.h","class.c","class.h","mobj.c","tobj.c","texp.c","pobj.c")]
        for rel in patch_rels:
            q=tree/rel;q.parent.mkdir(parents=True,exist_ok=True)
            q.write_bytes(subprocess.check_output(["git","show",commit+":"+rel],cwd=ROOT/".deps/melee"))
        env=dict(os.environ,TMPDIR=str(self.work),GIT_CEILING_DIRECTORIES=str(tree.parent))
        for k in ("GIT_DIR","GIT_WORK_TREE","GIT_INDEX_FILE","GIT_COMMON_DIR"):env.pop(k,None)
        ceiling=subprocess.run(["git","rev-parse","--show-toplevel"],cwd=tree,env=env,capture_output=True,text=True)
        self.assertNotEqual(ceiling.returncode,0,"Private apply tree discovered outer checkout")
        applied=subprocess.run(["git","apply",*["--include="+x for x in patch_rels],str((SOURCE/"patches/melee-gameplay.patch") if (SOURCE/"patches/melee-gameplay.patch").is_file() else ROOT/"patches/melee-gameplay.patch")],cwd=tree,env=env,capture_output=True,text=True)
        self.assertEqual(applied.returncode,0,applied.stdout+applied.stderr)
        sites=[
            (1,"class.c",250,"HSD_Class* mem_piece = hsdAllocMemPiece(info->head.obj_size);","HSD_CLASS_ALLOC","9c9c6f8bf436a9bbdc3dea7797a601b4edaab6fa521faef0bb1f2c79cbcbcd24"),
            (2,"mobj.c",160,"mobj->pe = hsdAllocMemPiece(sizeof(HSD_PEDesc));","MOBJ_PEDESC","c7dd30cef04812880bc49f42531cc8ff3eb94c2754f00b1d1b072541b9f7f6ed"),
            (3,"mobj.c",498,"HSD_Material* mat = hsdAllocMemPiece(sizeof(HSD_Material));","MATERIAL","c7dd30cef04812880bc49f42531cc8ff3eb94c2754f00b1d1b072541b9f7f6ed"),
            (4,"tobj.c",1524,"HSD_Tlut* tlut = hsdAllocMemPiece(sizeof(HSD_Tlut));","TLUT","16f2d1f2fe32f92f933fd05a2f68c883b4754134b9a0d06fe2c0931ddec9f195"),
            (5,"tobj.c",1544,"HSD_TObjTev* tev = hsdAllocMemPiece(sizeof(HSD_TObjTev));","TOBJ_TEV","16f2d1f2fe32f92f933fd05a2f68c883b4754134b9a0d06fe2c0931ddec9f195"),
            (6,"tobj.c",1564,"HSD_ImageDesc* idesc = hsdAllocMemPiece(sizeof(HSD_ImageDesc));","IMAGE_DESC","16f2d1f2fe32f92f933fd05a2f68c883b4754134b9a0d06fe2c0931ddec9f195"),
            (7,"texp.c",27,"HSD_TExp* texp = hsdAllocMemPiece(sizeof(HSD_TETev));","TEXP_TEV","55d765ae4d223e42e357ea6177caa9099953b80b26fd52b8b7f42af886d76949"),
            (8,"texp.c",34,"HSD_TExp* texp = hsdAllocMemPiece(sizeof(HSD_TECnst));","TEXP_CNST","55d765ae4d223e42e357ea6177caa9099953b80b26fd52b8b7f42af886d76949"),
            (9,"texp.c",1215,"HSD_TExpTevDesc* tdesc = hsdAllocMemPiece(sizeof(HSD_TExpTevDesc));","TEXP_TEV_DESC","55d765ae4d223e42e357ea6177caa9099953b80b26fd52b8b7f42af886d76949"),
            (10,"pobj.c",175,"HSD_Envelope* envelope = hsdAllocMemPiece(sizeof(HSD_Envelope));","ENVELOPE","cb5e3d1e38492e285798415e8a3368f62abd8486aad6011e73dab5f5b07d1a1c"),
            (11,"pobj.c",257,"HSD_ShapeSet* shape_set = hsdAllocMemPiece(sizeof(HSD_ShapeSet));","SHAPE_SET","cb5e3d1e38492e285798415e8a3368f62abd8486aad6011e73dab5f5b07d1a1c"),
        ]
        for site,name,line,expression,site_name,expected_hash in sites:
            rel="src/sysdolphin/baselib/"+name
            original=subprocess.check_output(["git","show",commit+":"+rel],cwd=ROOT/".deps/melee")
            self.assertEqual(__import__("hashlib").sha256(original).hexdigest(),expected_hash,f"pinned source hash for site {site}")
            self.assertEqual(original.decode().splitlines()[line-1].strip(),expression,f"pinned source line for site {site}")
            patched=(tree/rel).read_text()
            self.assertEqual(len(re.findall(r"\bMELEE_WEB_CLASS_PIECE_SITE_"+re.escape(site_name)+r"\b",patched)),1,f"patched site id {site}")
            expected_calls={"class.c":2,"mobj.c":2,"tobj.c":3,"texp.c":3,"pobj.c":2}[name]
            self.assertEqual(patched.count("hsdAllocMemPieceAt("),expected_calls,f"patched wrapper call count for {name}")
            if name=="class.c":
                wrapper=re.search(r"void\* melee_web_hsdAllocMemPieceAt\(s32 size, u32 site\)\s*\{([^}]*)\}",patched,re.S)
                self.assertIsNotNone(wrapper,"private allocator wrapper is present")
                body=wrapper.group(1)
                self.assertEqual(len(re.findall(r"\bhsdAllocMemPiece\(size\)",body)),1,"wrapper makes one source allocator call with the evaluated size")
                self.assertRegex(body,r"if\s*\(piece\)\s*melee_web_class_piece_origin\(piece,\s*size,\s*site\)","wrapper labels only a non-NULL source result")
                self.assertRegex(body,r"return\s+piece\s*;","wrapper returns the exact source result")
        # Apply ONLY the real JObj diagnostic section and prove both original
        # math bodies are byte-identical after removing entry/exit observation.
        jobj_rel="src/sysdolphin/baselib/jobj.c"
        jobj_original=subprocess.check_output(["git","show",commit+":"+jobj_rel],cwd=ROOT/".deps/melee",text=True)
        (tree/jobj_rel).write_text(jobj_original)
        profile=SOURCE/"src/slippi_profile/patches/melee-profile-hooks.patch"
        if not profile.is_file():profile=ROOT/"src/slippi_profile/patches/melee-profile-hooks.patch"
        observed=subprocess.run(["git","apply","--include="+jobj_rel,str(profile)],cwd=tree,env=env,capture_output=True,text=True)
        self.assertEqual(observed.returncode,0,observed.stdout+observed.stderr)
        jobj_patched=(tree/jobj_rel).read_text()
        def body(text,name):
            start=text.index("void "+name+"("); opening=text.index("{",start); depth=1; end=opening+1
            while depth:
                depth += (text[end]=="{")-(text[end]=="}");end+=1
            return text[start:end]
        for name,leaf in (("HSD_JObjMakeMatrix",1),("HSD_JObjSetupMatrixSub",2)):
            actual=body(jobj_patched,name)
            self.assertEqual(actual.count("PIECE_JOBJ_BEGIN(jobj, "+str(leaf)+");"),1)
            self.assertEqual(actual.count("PIECE_JOBJ_END(jobj);"),1)
            stripped=actual.replace("    PIECE_JOBJ_BEGIN(jobj, "+str(leaf)+");\n\n","").replace("    PIECE_JOBJ_END(jobj);\n","")
            self.assertEqual(stripped,body(jobj_original,name),name+" original math/order unchanged")
        self.assertIn("MELEE_WEB_CPU_INTERVAL_ENABLED",jobj_patched)
        self.assertIn("MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE",jobj_patched)
        # The existing source-preparation bool ABI lowering, applied only to
        # this private patched actual C TU. No source preparation command runs.
        class_header=tree/"src/sysdolphin/baselib/class.h"
        class_header.write_text(re.sub(r"\bbool\b","melee_source_bool",class_header.read_text()))
        raw=(tree/"src/sysdolphin/baselib/class.c").read_text()
        (tree/"src/sysdolphin/baselib/class.c").write_text(re.sub(r"\bbool\b","melee_source_bool",raw))
        common=["-DTARGET_PC","-ffp-contract=off","-I"+str(headers),"-I"+str(ROOT/"src"),"-I"+str(tree/"src"),"-I"+str(prepared),
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
            for case in (["jobj-probe","jobj-probe-stale","jobj-probe-overflow","origin","origin-duplicate","origin-unknown","raw","class","drift","missing","mirror","retired","promotion","descriptor","overflow","serial","noncold","extent","oom","topology","disarm",*["capture-"+x for x in ["armed","frozen","reuse","disarm","duplicate","topology","descriptor","amnesia","serial","wrong-header","stale-row","extent","anchor","cold","driftserial","bounds","pending","null-class","raw","missing","mirror","retired","recreated","trace-positive","trace-close","trace-overflow","trace-unavailable","trace-nojoin"]]] if active else ["inactive"]):
                result=run(mode+"-"+case,[node_runtime(),js,case])
                if active and case.startswith("jobj-probe"):
                    records=[json.loads(line.split(" ",1)[1]) for line in result.stdout.splitlines() if line.startswith("MELEE_CLASS_PIECE_SLAB_TRACE ")]
                    self.assertTrue(all(len(line)<=4096 for line in result.stdout.splitlines() if line.startswith("MELEE_CLASS_PIECE_SLAB_TRACE ")))
                    creators=[x for x in records if x["type"]=="jobj_creator"]
                    writers=[x for x in records if x["type"]=="jobj_writer"]
                    summary=next(x for x in records if x["type"]=="jobj_summary")
                    self.assertEqual(len(creators),1 if case.endswith("stale") else 2)
                    self.assertEqual(summary["creator_count"],len(creators))
                    self.assertEqual(summary["writer_count"],len(writers))
                    for creator in creators:
                        self.assertRegex(creator["host"],r"^0x[0-9a-f]{16}$")
                        self.assertIn(creator["stack_status"],(0,1,2,3))
                    if case.endswith("stale"):
                        self.assertTrue(summary["refused"]);self.assertFalse(writers)
                    else:
                        self.assertEqual([x["leaf"] for x in writers[:2]],["HSD_JObjMakeMatrix","HSD_JObjSetupMatrixSub"])
                        self.assertEqual(writers[0]["before"][0],"00000000")
                        self.assertEqual(writers[0]["after"][0],"3f800000")
                        self.assertLess(writers[1]["begin_sequence"],writers[0]["begin_sequence"])
                        self.assertLess(writers[0]["begin_sequence"],writers[0]["end_sequence"])
                        self.assertLess(writers[0]["end_sequence"],writers[1]["end_sequence"])
                        self.assertEqual(writers[0]["flags_after"],"00000040")
                        self.assertEqual((writers[0]["command"],writers[0]["scope"],writers[0]["frame"],writers[0]["sample"]),(9,"STEP",176,300))
                        self.assertEqual(summary["overflow"],case.endswith("overflow"))
                if active and case in ("capture-trace-positive","capture-trace-close","capture-trace-overflow","capture-trace-unavailable","capture-trace-nojoin"):
                    prefix="MELEE_CLASS_PIECE_SLAB_TRACE "
                    lines=[line[len(prefix):] for line in result.stdout.splitlines() if line.startswith(prefix)]
                    self.assertTrue(lines,"trace output is present on the retained native stdout path")
                    self.assertLessEqual(len(lines),34,"slab summary plus32 events and empty JObj summary")
                    self.assertEqual(len([line for line in lines if json.loads(line).get("type")=="jobj_summary"]),1)
                    self.assertTrue(all(len(line)<=4096 for line in lines),"each record fits retained stdout bound")
                    records=[json.loads(line) for line in lines]
                    summaries=[record for record in records if record.get("type")=="summary"]
                    events=[record for record in records if record.get("type")=="event"]
                    self.assertEqual(len(summaries),1,"close/finalize emits one summary")
                    summary=summaries[0]
                    self.assertEqual(len(events),summary["event_count"])
                    self.assertLessEqual(summary["event_count"],32)
                    if case=="capture-trace-positive":
                        self.assertTrue(summary["joined"])
                        self.assertEqual(summary["join_reason"],"EXACT_FINAL_WITNESS_JOIN")
                        self.assertTrue(summary["complete"])
                        self.assertFalse(summary["overflow"])
                        self.assertEqual(summary["comparison"],"MISMATCH")
                        matching=[event for event in events if event["kind_name"]=="FREE"]
                        self.assertTrue(matching,"actual class free is observed")
                        event=matching[0]
                        self.assertEqual((event["command"],event["scope"],event["frame"],event["sample"]),(9,"STEP",176,300))
                        self.assertEqual((event["requested_extent"],event["physical_extent"],event["target_part"]),(48,64,"PHYSICAL_PADDING"))
                        self.assertRegex(event["row_host"],r"^0x[0-9a-f]{16}$")
                        self.assertRegex(event["generation"],r"^0x[0-9a-f]{16}$")
                        self.assertEqual(event["row_host"],summary["row_host"])
                    elif case=="capture-trace-close":
                        self.assertEqual(summary["close_status"],0)
                        self.assertIsNone(summary["comparison_status"])
                        self.assertFalse(summary["joined"])
                        self.assertEqual(summary["join_reason"],"EARLY_CLOSE")
                        self.assertFalse(summary["complete"])
                    elif case=="capture-trace-overflow":
                        self.assertTrue(summary["joined"],"identity join may remain useful despite event overflow")
                        self.assertEqual(summary["join_reason"],"OVERFLOW_INCONCLUSIVE")
                        self.assertTrue(summary["overflow"])
                        self.assertFalse(summary["complete"])
                        self.assertEqual(summary["event_count"],32)
                    elif case=="capture-trace-unavailable":
                        self.assertEqual(summary["arm_status"],2)
                        self.assertEqual(summary["arm"],"UNAVAILABLE")
                        self.assertEqual(summary["join_reason"],"ARM_UNAVAILABLE")
                        self.assertFalse(summary["joined"])
                        self.assertEqual(summary["comparison"],"EQUAL")
                        self.assertEqual(summary["event_count"],0)
                        self.assertFalse(summary["complete"])
                    else:
                        self.assertEqual(summary["arm_status"],1)
                        self.assertFalse(summary["joined"],"a different exact-diff offset cannot join the selected target")
                        self.assertTrue(summary["difference_valid"])
                        self.assertEqual(summary["difference_offset"],summary["target_offset"]+1)
                        self.assertEqual(summary["comparison"],"MISMATCH")
                        self.assertEqual(summary["join_reason"],"CANDIDATE_WITNESS_MISMATCH")
                        self.assertEqual(summary["event_count"],1)
                if active and case=="capture-frozen":
                    marker="ACTUAL_CLASS_CAPTURE_BASE64="
                    values=[line[len(marker):] for line in result.stdout.splitlines() if line.startswith(marker)]
                    self.assertEqual(len(values),1);raw=base64.b64decode(values[0],validate=True);self.assertEqual(len(raw),160)
                    blob=self.work/"actual-class-capture.bin";blob.write_bytes(raw)
                    helper=Path(os.environ.get("MELEE_CLASS_PIECE_JS_HELPER",ROOT/"src/slippi_profile/tests/cpu_two_tick_runner.mjs"))
                    bridge="""const fs=await import('node:fs');const {pathToFileURL}=await import('node:url');
const {decodeCpuClassPieceCaptureWitness}=await import(pathToFileURL(process.argv[1]));
const d=decodeCpuClassPieceCaptureWitness(new Uint8Array(fs.readFileSync(process.argv[2])));
                    if(d.schema!==2||d.status!==1||d.scope!=='CAPTURE_TIME_ONLY'||d.liveReason!==7||d.recordState!==3||d.originalRequested!==48||d.physical!==64||d.pieceOffset!==8||d.originSite!==1||d.originStatus!==1||!d.originAvailable||d.livePieceLifetime!=='UNKNOWN')throw Error('actual historical bridge drift');
console.log('PASS actual memory/class/runtime160 schema2 CAPTURE_TIME_ONLY; modeled OS and descriptor fixture');"""
                    run("actual-capture-js-bridge",[node_runtime(),"--input-type=module","-e",bridge,helper,blob])
                if active and case=="capture-raw":
                    marker="ACTUAL_CLASS_CAPTURE_RAW_BASE64="
                    values=[line[len(marker):] for line in result.stdout.splitlines() if line.startswith(marker)]
                    self.assertEqual(len(values),1);raw=base64.b64decode(values[0],validate=True);self.assertEqual(len(raw),160)
                    blob=self.work/"actual-class-capture-raw.bin";blob.write_bytes(raw)
                    helper=Path(os.environ.get("MELEE_CLASS_PIECE_JS_HELPER",ROOT/"src/slippi_profile/tests/cpu_two_tick_runner.mjs"))
                    bridge="""const fs=await import('node:fs');const {pathToFileURL}=await import('node:url');
const {decodeCpuClassPieceCaptureWitness}=await import(pathToFileURL(process.argv[1]));
const d=decodeCpuClassPieceCaptureWitness(new Uint8Array(fs.readFileSync(process.argv[2])));
if(d.schema!==2||d.status!==1||d.recordState!==1||d.classInfo!==0||d.savedClassInfo!==0||d.originSite!==4||d.originStatus!==1||!d.originAvailable||d.originScope!=='ALLOCATION_CALLER_AT_CAPTURE_ONLY')throw Error('actual raw caller bridge drift');
console.log('PASS actual raw source piece: caller 4 at capture, class identity UNKNOWN');"""
                    run("actual-capture-raw-js-bridge",[node_runtime(),"--input-type=module","-e",bridge,helper,blob])
                if active and case=="capture-null-class":
                    marker="ACTUAL_CLASS_CAPTURE_NULL_BASE64="
                    values=[line[len(marker):] for line in result.stdout.splitlines() if line.startswith(marker)]
                    self.assertEqual(len(values),1);raw=base64.b64decode(values[0],validate=True);self.assertEqual(len(raw),160)
                    blob=self.work/"actual-class-capture-null.bin";blob.write_bytes(raw)
                    helper=Path(os.environ.get("MELEE_CLASS_PIECE_JS_HELPER",ROOT/"src/slippi_profile/tests/cpu_two_tick_runner.mjs"))
                    bridge="""const fs=await import('node:fs');const {pathToFileURL}=await import('node:url');
const {decodeCpuClassPieceCaptureWitness}=await import(pathToFileURL(process.argv[1]));
const b=new Uint8Array(fs.readFileSync(process.argv[2]));const d=decodeCpuClassPieceCaptureWitness(b);
                    if(d.schema!==2||d.status!==2||d.reason!==10||d.classInfo!==0||d.savedClassInfo!==0||d.originSite!==0||d.originStatus!==0||d.scope!=='CAPTURE_TIME_ONLY')throw Error('null-class native refusal drift');
const v=new DataView(b.buffer);v.setUint32(8,1,true);v.setUint32(12,0,true);v.setUint32(152,1,true);v.setUint32(156,1,true);
let refused=false;try{decodeCpuClassPieceCaptureWitness(b);}catch(e){refused=/saved header/.test(String(e));}
if(!refused)throw Error('decoder accepted malformed schema2 success');
const legacy=new Uint8Array(160);const lv=new DataView(legacy.buffer);lv.setUint32(0,1,true);lv.setUint32(4,160,true);lv.setUint32(16,1,true);
const ld=decodeCpuClassPieceCaptureWitness(legacy);if(ld.originSite!==null||ld.originStatus!==0||ld.originScope!=='LEGACY_SCHEMA_UNAVAILABLE')throw Error('legacy schema fabricated caller');
legacy[152]=1;let legacyRefused=false;try{decodeCpuClassPieceCaptureWitness(legacy);}catch(e){legacyRefused=/legacy reserved/.test(String(e));}
if(!legacyRefused)throw Error('legacy schema accepted nonzero reserved bytes');
console.log('PASS actual schema2 null-class REFUSED; schema1 remains readable without caller attribution');"""
                    run("actual-capture-null-js-bridge",[node_runtime(),"--input-type=module","-e",bridge,helper,blob])
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
