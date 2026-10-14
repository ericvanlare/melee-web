"""Focused actual-source first-pair audio/file guard controls.

The fixture translation units include the checkout's actual owners. An ordinary
configured runtime row supplies ABI/header flags; private runtime profiles are
not a test prerequisite. Only fixture source, output/dependency paths and the
private diagnostic define are adjusted.
"""
from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import sys
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from first_pair_test_support import pinned_node_or_skip

ORDINARY_RUNTIME_PROFILE_DIRS = ("browser-release", "browser")


def _configured_compile_database():
    for name in ORDINARY_RUNTIME_PROFILE_DIRS:
        path = ROOT / "build" / name / "compile_commands.json"
        if path.is_file() and not path.is_symlink():
            return path
    return None


def _select_row(entries, relative, target_fragment, profile):
    source = (ROOT / relative).resolve()
    expected_directory = (ROOT / "build" / profile).resolve()
    matches = []
    for entry in entries:
        if Path(entry.get("file", "")).resolve() != source:
            continue
        if Path(entry.get("directory", "")).resolve() != expected_directory:
            continue
        argv = shlex.split(entry.get("command", ""))
        if "-o" not in argv:
            continue
        output = argv[argv.index("-o") + 1]
        if target_fragment in output:
            matches.append((entry, argv))
    if len(matches) != 1:
        raise AssertionError(
            f"expected one ordinary runtime row for {relative} in {profile}/{target_fragment}; got {len(matches)}"
        )
    entry, argv = matches[0]
    if "-DTARGET_PC" not in argv or "-DNDEBUG" not in argv or not any(
            flag in argv for flag in ("-O2", "-O3")):
        raise AssertionError(f"ordinary runtime row for {relative} lost its optimized TARGET_PC ABI flags")
    forbidden = (
        "-DMELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC=1",
        "-DMELEE_WEB_MATCH_FRAME_EFFECT_DIAGNOSTIC=1",
        "-DMELEE_WEB_MATCH_DRAW_DISCARD_DIAGNOSTIC=1",
    )
    if any(flag in argv for flag in forbidden):
        raise AssertionError(f"ordinary runtime row for {relative} unexpectedly enables a private profile")
    return entry, argv


def _function_body(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 1
    cursor = brace + 1
    while depth:
        if source[cursor] == "{":
            depth += 1
        elif source[cursor] == "}":
            depth -= 1
        cursor += 1
    return source[start:cursor]


def _function_definition_body(source, marker):
    definitions = []
    cursor = 0
    while True:
        start = source.find(marker, cursor)
        if start < 0:
            break
        opening = start + len(marker)
        while opening < len(source) and source[opening].isspace():
            opening += 1
        if opening >= len(source) or source[opening] != "(":
            cursor = start + len(marker)
            continue
        depth = 0
        position = opening
        while position < len(source):
            if source[position] == "(":
                depth += 1
            elif source[position] == ")":
                depth -= 1
                if depth == 0:
                    break
            position += 1
        if depth != 0:
            raise AssertionError(f"unterminated signature for {marker}")
        after_signature = position + 1
        while after_signature < len(source) and source[after_signature].isspace():
            after_signature += 1
        if after_signature < len(source) and source[after_signature] == "{":
            brace = after_signature
            body_depth = 1
            end = brace + 1
            while body_depth:
                if source[end] == "{":
                    body_depth += 1
                elif source[end] == "}":
                    body_depth -= 1
                end += 1
            definitions.append(source[start:end])
            cursor = end
        else:
            cursor = position + 1
    if len(definitions) != 1:
        raise AssertionError(f"expected exactly one definition for {marker}; found {len(definitions)}")
    return definitions[0]


def _append(log, label, argv, result):
    with log.open("a", encoding="utf-8") as stream:
        stream.write(f"[{label}] {' '.join(shlex.quote(str(arg)) for arg in argv)}\n")
        stream.write(result.stdout or "")
        stream.write(result.stderr or "")
        stream.write(f"[exit {result.returncode}]\n")


def _prepare_compile_argv(entry, argv, replacement, output, private=True):
    args = list(argv)
    if not args or Path(args[-1]).resolve() != Path(entry["file"]).resolve():
        raise AssertionError("configured source compile row shape changed")
    args[-1] = str(replacement)
    if args.count("-o") != 1:
        raise AssertionError("configured source compile row must have one output")
    args[args.index("-o") + 1] = str(output)
    if "-MF" in args:
        args[args.index("-MF") + 1] = str(output.with_suffix(output.suffix + ".d"))
    elif "-MD" in args or "-MMD" in args:
        raise AssertionError("dependency-generating row lacks a redirectable -MF path")
    if "-MT" in args:
        args[args.index("-MT") + 1] = str(output)
    if private and "-DMELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC=1" not in args:
        args.insert(1, "-DMELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC=1")
    return args


OWNER_STUBS = r'''
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
struct MeleeWebAudio{}; struct MeleeWebSourceFileScope{};
MeleeWebAudio ownedAudio, wrongAudio; MeleeWebSourceFileScope ownedFiles, wrongFiles;
bool hpsIdle=true, ssmIdle=true, filesIdle=true;
struct Session { MeleeWebAudio* a=&ownedAudio; MeleeWebSourceFileScope* f=&ownedFiles;
MeleeWebAudio* audio(){return a;} const MeleeWebSourceFileScope* source_file_scope(){return f;} } session;
Session* match=&session;
bool running=true,faulted=false,pending=false,replay=false,results=false,prize=false,scoped_assets=true;
struct Preparation {bool active=false;bool busy(){return active;}} preparation;
enum class AssetDestination {None, Match};AssetDestination asset_destination=AssetDestination::None;
bool melee_web_audio_stream_quiescent_for(MeleeWebAudio* a){return a==&ownedAudio&&hpsIdle;}
bool melee_web_audio_bank_transport_quiescent_for(MeleeWebAudio* a){return a==&ownedAudio&&ssmIdle;}
bool melee_web_source_files_quiescent_for(const MeleeWebSourceFileScope* f){return f==&ownedFiles&&filesIdle;}
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"owner CHECK %d\n",__LINE__);std::exit(86);}}while(0)
'''
OWNER_MAIN = r'''
int main(){CHECK(melee_web_native_menu_first_pair_file_owners_quiescent()==1);
scoped_assets=false;CHECK(melee_web_native_menu_first_pair_file_owners_quiescent()==1);scoped_assets=true;
asset_destination=AssetDestination::Match;CHECK(!melee_web_native_menu_first_pair_file_owners_quiescent());asset_destination=AssetDestination::None;
preparation.active=true;CHECK(!melee_web_native_menu_first_pair_file_owners_quiescent());preparation.active=false;
session.a=&wrongAudio;CHECK(!melee_web_native_menu_first_pair_file_owners_quiescent());session.a=&ownedAudio;
session.f=&wrongFiles;CHECK(!melee_web_native_menu_first_pair_file_owners_quiescent());session.f=&ownedFiles;
for(bool* value:{&hpsIdle,&ssmIdle,&filesIdle,&running}){*value=false;CHECK(!melee_web_native_menu_first_pair_file_owners_quiescent());*value=true;}
for(bool* value:{&faulted,&pending,&replay,&results,&prize}){*value=true;CHECK(!melee_web_native_menu_first_pair_file_owners_quiescent());*value=false;}
match=nullptr;CHECK(!melee_web_native_menu_first_pair_file_owners_quiescent());match=&session;
std::puts("Actual scoped-disc owner accessor PASS");}
'''


class FirstPairAudioFileNativeTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, "first-pair-audio-file-native-")

    def test_function_definition_extraction_requires_one_body(self):
        marker = "int source_cancel"
        source = ("int source_cancel(int id);\n"
                  "void unrelated(void) { int value = 1; (void)value; }\n"
                  "int source_cancelfake(int id) { return -id; }\n"
                  "int source_cancel(int id) { return id; }\n")
        self.assertEqual(_function_definition_body(source, marker),
                         "int source_cancel(int id) { return id; }")
        with self.assertRaisesRegex(AssertionError, "found 0"):
            _function_definition_body("int source_cancel(int id);\n", marker)
        with self.assertRaisesRegex(AssertionError, "found 2"):
            _function_definition_body(
                "int source_cancel(int id) { return id; }\n"
                "int source_cancel(int id) { return id + 1; }\n", marker)

    def _prerequisites(self):
        emsdk = ROOT / ".deps/emsdk"
        emcc = emsdk / "upstream/emscripten/emcc"
        emxx = emsdk / "upstream/emscripten/em++"
        generated = ROOT / "build/gameplay-source/src"
        database = _configured_compile_database()
        if not emcc.is_file() or not emxx.is_file():
            self.skipTest("Pinned Emscripten compiler unavailable")
        if not generated.is_dir():
            self.skipTest("Prepared Emscripten generated source headers unavailable")
        if database is None:
            raise AssertionError(
                "ordinary runtime compile database is missing despite prepared source headers; "
                "private runtime compile databases are not accepted"
            )
        node = pinned_node_or_skip(self, ROOT)
        return emsdk, emcc, emxx, database, node

    def _run(self, label, argv, cwd, env, timeout=60):
        result = subprocess.run([str(value) for value in argv], cwd=cwd, env=env,
                                capture_output=True, text=True, timeout=timeout)
        _append(self.work / "first_pair_native_controls.log", label, argv, result)
        self.assertEqual(result.returncode, 0,
                         f"{label} failed ({result.returncode}); see retained workspace log\n"
                         f"{result.stdout}{result.stderr}")
        return result

    def test_actual_hps_ssm_source_file_and_owner_controls(self):
        emsdk, emcc, emxx, database, node = self._prerequisites()
        entries = json.loads(database.read_text(encoding="utf-8"))
        self.assertIsInstance(entries, list)
        profile = database.parent.name
        self.assertIn(profile, ORDINARY_RUNTIME_PROFILE_DIRS)
        env = dict(os.environ)
        env["EMSDK"] = str(emsdk)
        env["EM_CONFIG"] = str(emsdk / ".emscripten")
        env["EM_CACHE"] = str(emsdk / "upstream/emscripten/cache")
        env["EMSDK_PYTHON"] = sys.executable
        env["PATH"] = str(Path(node).parent) + os.pathsep + env.get("PATH", "")
        env["TMPDIR"] = str(self.work)
        log = self.work / "first_pair_native_controls.log"
        log.write_text("", encoding="utf-8")
        sys.path.insert(0, str(ROOT / "scripts"))
        from bootstrap import read_lock
        compiler_version = subprocess.run([str(emcc), "--version"], cwd=ROOT, env=env,
                                          capture_output=True, text=True, timeout=30)
        _append(log, "pinned Emscripten compiler version", [emcc, "--version"], compiler_version)
        self.assertEqual(compiler_version.returncode, 0,
                         compiler_version.stdout + compiler_version.stderr)
        self.assertIn(read_lock(ROOT)["emscripten"], compiler_version.stdout)

        # Default-off projection is compiled by the actual Emscripten preprocessor
        # with the private define removed. Discard stays void and cancellation
        # keeps the ordinary mark-then-discard order.
        entry, argv = _select_row(entries, "src/gameplay_source_files.c",
                                  "fighter_source_runtime.dir", profile)
        projection = [arg for arg in argv
                      if arg != "-DMELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC=1"]
        while "-c" in projection:
            projection.remove("-c")
        for flag in ("-o", "-MF", "-MT", "-MQ"):
            if flag in projection:
                index = projection.index(flag)
                del projection[index:index + 2]
        projection = [arg for arg in projection if arg not in ("-MD", "-MMD")]
        projection.extend(["-E", "-P"])
        result = subprocess.run(projection, cwd=entry["directory"], env=env,
                                capture_output=True, text=True, timeout=60)
        _append(log, "Emscripten default-off source-file projection", projection, result)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lowered = re.sub(r"\s+", "", result.stdout)
        discard_start = lowered.find("voidsource_stream_discard(intrequest)")
        self.assertGreaterEqual(discard_start, 0, "default-off source discard must remain void")
        cancel = re.sub(r"\s+", "", _function_definition_body(
            result.stdout, "int melee_web_source_file_cancel"))
        self.assertGreaterEqual(cancel.find("request->cancelled=1"), 0)
        self.assertGreaterEqual(cancel.find("source_stream_discard(request_id)"), 0)
        self.assertLess(cancel.find("request->cancelled=1"),
                        cancel.find("source_stream_discard(request_id)"),
                        "ordinary source cancellation marks native state before its original discard")
        self.assertNotIn("first-pair host guard refused source-read discard", lowered)

        compile_specs = (
            ("hps", "src/gameplay_audio_stream.c", "fighter_source_runtime.dir",
             "tests/first_pair_hps_native_controls.c"),
            ("ssm", "src/gameplay_audio_bank_transport.c", "fighter_source_runtime.dir",
             "tests/first_pair_ssm_native_controls.c"),
            ("source", "src/gameplay_source_files.c", "fighter_source_runtime.dir",
             "tests/first_pair_source_file_controls.c"),
        )
        objects = {}
        for label, relative, target, fixture in compile_specs:
            item, row = _select_row(entries, relative, target, profile)
            output = self.work / f"{label}_controls.o"
            compile_argv = _prepare_compile_argv(item, row, ROOT / fixture, output)
            self._run(f"compile actual {label.upper()} source fixture", compile_argv,
                      item["directory"], env)
            objects[label] = output

        io_entry, io_row = _select_row(entries, "src/gameplay_io.cpp",
                                       "fighter_source_runtime.dir", profile)
        io_obj = self.work / "gameplay_io.o"
        io_argv = _prepare_compile_argv(io_entry, io_row, ROOT / "src/gameplay_io.cpp", io_obj)
        self._run("compile actual gameplay_io.cpp owner", io_argv, io_entry["directory"], env)

        guard_entry, guard_row = _select_row(entries, "src/gameplay_audio_stream.c",
                                              "fighter_source_runtime.dir", profile)
        guard_obj = self.work / "gameplay_first_pair_file_guard.o"
        guard_argv = _prepare_compile_argv(
            guard_entry, guard_row, ROOT / "src/gameplay_first_pair_file_guard.c", guard_obj)
        self._run("compile actual first-pair C file guard", guard_argv,
                  guard_entry["directory"], env)

        link_specs = (
            ("hps", (objects["hps"], io_obj, guard_obj), ()),
            ("ssm", (objects["ssm"], guard_obj), ()),
            ("source", (objects["source"], guard_obj), ("-sASYNCIFY=1",)),
        )
        for label, link_objects, flags in link_specs:
            executable = self.work / f"first_pair_{label}_controls.js"
            link_argv = [str(emxx), "-O3", "-DNDEBUG", "-sENVIRONMENT=node",
                         "-sEXIT_RUNTIME=1", *flags,
                         *map(str, link_objects), "-Wl,--gc-sections", "-o", str(executable)]
            self._run(f"link {label.upper()} actual-source fixture", link_argv,
                      self.work, env)
            if label == "hps":
                negative = subprocess.run([str(node), str(executable), "--fail-check"],
                                          cwd=self.work, env=env, capture_output=True,
                                          text=True, timeout=30)
                _append(log, "run HPS always-on CHECK negative sentinel",
                        [node, executable, "--fail-check"], negative)
                self.assertEqual(negative.returncode, 86,
                                 "Release sentinel must fail through the always-on CHECK macro; "
                                 f"see retained workspace log\n{negative.stdout}{negative.stderr}")
                self.assertIn("intentional CHECK failure sentinel", negative.stderr)
            run = self._run(f"run {label.upper()} actual-source controls",
                            [node, executable], self.work, env, timeout=30)
            self.assertIn("Actual ", run.stdout)

        # Extract the actual live predicate body rather than copying its code.
        source = (ROOT / "src/gameplay_menu_browser.cpp").read_text(encoding="utf-8")
        marker = "int melee_web_native_menu_first_pair_file_owners_quiescent()"
        self.assertEqual(source.count(marker), 1, "expected one actual scoped owner predicate")
        owner_source = _function_body(source, marker)
        owner_fixture = self.work / "owner_accessor.cpp"
        owner_fixture.write_text(OWNER_STUBS + owner_source + OWNER_MAIN, encoding="utf-8")
        owner_executable = self.work / "owner_accessor.js"
        owner_compile = [str(emxx), "-O3", "-DNDEBUG", "-std=gnu++20", "-DTARGET_PC",
                         "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", str(owner_fixture),
                         "-o", str(owner_executable)]
        self._run("compile actual owner predicate extraction", owner_compile, self.work, env)
        owner_run = self._run("run actual owner predicate controls",
                              [node, owner_executable], self.work, env, timeout=30)
        self.assertIn("Actual scoped-disc owner accessor PASS", owner_run.stdout)


if __name__ == "__main__":
    unittest.main()
