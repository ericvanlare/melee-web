# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded native/Wasm controls of the prepared Shield Drop caller source."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

from src.slippi_profile.tests.test_dbooc_scalar_helper import _extract_function

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
import gameplay_sources
from check_gameplay import node_runtime

ESCAPE = "melee/ft/kinds/ftCommon/ftCo_Escape.c"
PINS = {
    ESCAPE: "664f3a057fe21f775a382ba1afff6671185bde2e30418b66084499e1514c1c99",
    "melee/ft/ft_0DF1.c": "e13eff4bf5970f45db95b30b6f67e8a9d25149dad45046f8b91a9114ba37056a",
    "melee/ft/types.h": "74b7246a0fe4199def6610aa0692378e86a404a0eea815bca5bc422e44b125a8",
    "melee/lb/types.h": "156c7b4a597db5913674322f54cbe584f0fdb80c21be93c89e579068282c317a",
    "melee/mp/forward.h": "20c7e1a22c7252ebc4b7dd4547636b62fac75b5d798ff8d66dc51b47f2851bf2",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def capture(out, name, command, **kwargs):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                            timeout=120, **kwargs)
    (out / (name + ".json")).write_text(json.dumps({
        "command": [str(x) for x in command], "returncode": result.returncode,
        "stdout": result.stdout, "stderr": result.stderr,
    }, indent=2) + "\n")
    if result.returncode:
        raise AssertionError(f"{name} failed; retained {out}: {result.stderr}")
    return result.stdout


class UcfShieldDropTests(unittest.TestCase):
    def test_prepared_callers_native_wasm_and_source_guards(self):
        evidence = Path(os.environ.get("MELEE_WEB_TEST_OUTPUT", ROOT / "work"))
        evidence.mkdir(parents=True, exist_ok=True)
        out = Path(tempfile.mkdtemp(prefix="issue337-shield-drop-", dir=evidence))
        for name, expected in PINS.items():
            self.assertEqual(sha((ROOT / ".deps/melee/src" / name).read_bytes()), expected)
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        self.assertEqual(lock["repositories"]["melee"]["commit"],
                         "b43912cc78606f96c9569f5d6229bc9d7e265ea5")
        # Use the actual production prepare path, including base/boolean/overlay.
        ordinary = gameplay_sources.prepare_sources(ROOT)
        profile = gameplay_sources.prepare_sources(ROOT, slippi_profile=True)
        base = (ordinary / ESCAPE).read_text()
        prepared = (profile / ESCAPE).read_text()
        self.assertNotIn("ucf_shield_drop", base)
        self.assertEqual(prepared.count("slippi_profile_ucf_shield_drop_suppresses("), 2)
        (out / "ordinary-Escape.c").write_text(base)
        (out / "profile-Escape.c").write_text(prepared)
        for name in ("inlineB0", "ftCo_80099894"):
            signature = ("static inline melee_source_bool inlineB0(Fighter* fp)"
                         if name == "inlineB0" else "void ftCo_80099894(Fighter_GObj* gobj)")
            self.assertEqual(_extract_function(base, signature),
                             _extract_function(prepared, signature))
        fixture = (ROOT / "src/slippi_profile/tests/shield_drop_fixture.c.in").read_text()
        common = _extract_function(base, "static inline melee_source_bool inlineB0(Fighter* fp)")
        cstick = (ordinary / "melee/ft/ft_0DF1.c").read_text()
        common += _extract_function(cstick, "melee_source_bool ftCo_800DF8E8(Fighter* fp)")
        common += _extract_function(base, "void ftCo_80099894(Fighter_GObj* gobj)")
        variants = []
        for variant, source in (("original", base), ("profile", prepared), ("omitted", prepared)):
            definitions = ""
            if variant != "original":
                definitions += "#define MELEE_WEB_SLIPPI_PROFILE 1\n"
            if variant == "omitted":
                definitions += "#define slippi_profile_ucf_shield_drop_suppresses omitted_suppression\n"
            for name in ("ftCo_80099794", "ftCo_8009980C"):
                definitions += f"#define {name} {name}_{variant}\n"
                definitions += _extract_function(source,
                    f"melee_source_bool {name}(Fighter_GObj* gobj)")
                definitions += f"#undef {name}\n"
            if variant == "omitted":
                definitions += "#undef slippi_profile_ucf_shield_drop_suppresses\n"
            definitions += "#undef MELEE_WEB_SLIPPI_PROFILE\n"
            variants.append(definitions)
        # Profile disabled is checked by the real C preprocessor, not text removal.
        calls = "\n".join(_extract_function(prepared,
            f"melee_source_bool {name}(Fighter_GObj* gobj)")
            for name in ("ftCo_80099794", "ftCo_8009980C"))
        base_calls = "\n".join(_extract_function(base,
            f"melee_source_bool {name}(Fighter_GObj* gobj)")
            for name in ("ftCo_80099794", "ftCo_8009980C"))
        cc = shlex.split(os.environ.get("CC", "cc"))
        off = capture(out, "profile-off", [*cc, "-E", "-P", "-x", "c", "-"], input=calls)
        plain = capture(out, "original-preprocess", [*cc, "-E", "-P", "-x", "c", "-"], input=base_calls)
        self.assertEqual(off, plain)
        generated = out / "fixture.c"
        generated.write_text(fixture.replace("@COMMON@", common).replace("@VARIANTS@", "\n".join(variants)))
        sdk = ROOT / ".deps/emsdk"
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EM_CACHE=str(sdk / "upstream/emscripten/cache"), EMSDK_PYTHON=sys.executable)
        emcc = [sys.executable, str(sdk / "upstream/emscripten/emcc.py")]
        # Compile actual prepared source with the production compatibility and
        # include ordering. This is a Wasm ABI/type probe, not a full build.
        guard = out / "source-guards.c"
        guard.write_text("#include <melee/ft/fighter.h>\n#include <melee/ft/types.h>\n"
                         "#include <melee/mp/forward.h>\n"
                         "#include <slippi_profile/ucf_shield_drop_source_guards.h>\n")
        source_options = [*emcc, "-std=c11", "-DTARGET_PC",
            "-fno-fast-math", "-ffp-contract=off",
            "-I", str(ROOT / "src"), "-I", str(profile),
            "-I", str(ROOT / ".deps/aurora/include"),
            "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
            "-include", str(ROOT / "src/gameplay_compat.h"),
            "-fsyntax-only"]
        capture(out, "wasm-real-source-guards", [*source_options, str(guard)], env=env)
        capture(out, "wasm-prepared-Escape", [*source_options,
            "-DMELEE_WEB_SLIPPI_PROFILE=1", str(profile / ESCAPE)], env=env)
        options = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-fno-fast-math", "-ffp-contract=off", "-I", str(ROOT / "src"),
            str(generated), str(ROOT / "src/slippi_profile/ucf_shield_drop_fix.c"),
            str(ROOT / "src/slippi_profile/ucf_dbooc_fix.c"), "-lm"]
        native = out / "native"
        wasm = out / "wasm.js"
        capture(out, "native-compile", [*cc, *options, "-fsanitize=undefined",
            "-fno-sanitize-recover=undefined", "-o", str(native)])
        capture(out, "wasm-compile", [*emcc, *options, "-sENVIRONMENT=node",
            "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-o", str(wasm)], env=env)
        native_result = capture(out, "native-run", [str(native)])
        wasm_result = capture(out, "wasm-run", [str(node_runtime()), str(wasm)], env=env)
        self.assertEqual(native_result, wasm_result)
        self.assertIn("PASS shield-drop", native_result)
        result = {"schema": "issue337-shield-drop-focused-v1", "status": "passed",
            "ordinary_source_sha256": sha(base.encode()), "profile_source_sha256": sha(prepared.encode()),
            "fixture_sha256": sha(generated.read_bytes()), "native_equals_wasm": True,
            "scalar_scenarios": 26, "source_caller_controls": 156, "shared_trace_controls": 20,
            "profile_source_hashes": {name: sha((ROOT / "src/slippi_profile" / name).read_bytes())
                for name in ("ucf_dbooc_fix.c", "ucf_dbooc_fix.h", "ucf_shield_drop_fix.c",
                             "ucf_shield_drop_fix.h", "ucf_shield_drop_source_guards.h")},
            "stdout_sha256": sha(native_result.encode()),
            "exclusions": ["synthetic services and Fighter storage in caller reducer",
                "no original PPC execution, FPSCR flags/traps, live activation or gameplay acceptance"]}
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        # Preserve raw source/logs, remove only this successful run's binaries.
        for path in (native, wasm, wasm.with_suffix(".wasm")):
            path.unlink(missing_ok=True)
        print(f"Shield Drop focused evidence: {out}")


if __name__ == "__main__":
    unittest.main()
