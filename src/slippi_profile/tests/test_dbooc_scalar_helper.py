# SPDX-License-Identifier: GPL-3.0-or-later
"""Native and Node/Wasm checks for the isolated DBOOC scalar helper."""

import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

from src.slippi_profile.tests import test_dbooc_binary32_oracle as oracle


ROOT = Path(__file__).resolve().parents[3]
MELEE = ROOT / ".deps/melee"
UPSTREAM_SQUAT = MELEE / "src/melee/ft/kinds/ftCommon/ftCo_SquatRv.c"
UPSTREAM_STATES = MELEE / "src/melee/ft/ftmotionstates.c"
UPSTREAM_FORWARD = MELEE / "src/melee/ft/forward.h"
UPSTREAM_COMMON_FORWARD = MELEE / "src/melee/ft/kinds/ftCommon/forward.h"
BASE_PATCH = ROOT / "patches/melee-gameplay.patch"
BOOL_TRANSFORM = ROOT / "scripts/gameplay_bool.py"
TRIG_HELPER = ROOT / "src/gameplay_trig.h"
PROFILE_HEADER = ROOT / "src/slippi_profile/ucf_dbooc_fix.h"
PROFILE_HELPER = ROOT / "src/slippi_profile/ucf_dbooc_fix.c"
PROFILE_PATCH = ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch"
FIXTURE_TEMPLATE = ROOT / "src/slippi_profile/tests/dbooc_scalar_fixture.c.in"
MELEE_SOURCE_PIN = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"
SQUAT_SOURCE_SHA256 = "7a31e8bda702f6448ebeda5613f71f75a220ae302f1f3713bab4db26e6f9d4b3"
STATES_SOURCE_SHA256 = "8785cfcb7091ce29268a8c2172aecd97a60bc6352c545a472aa77e4cbd66141a"
FORWARD_SOURCE_SHA256 = "73cf4918e768aa15477d7c9e17d6fa58ded77ee60c9e0398820dda1ac6cfc7fe"
COMMON_FORWARD_SOURCE_SHA256 = "514cd2e4a2ebf3729255efce6b88bb6b91acd685a519e2b4bb01bcb09f7492ca"
BOOL_TRANSFORM_SHA256 = "7609be54e0e7c29c86a63fd4dfa37078d2d7baba643600e6c1fff40e4f2c46b7"
TRIG_HELPER_SHA256 = "f5d724d31a79b3e42cfbe2a7a07a63184b9636481b2653d9d3ff8c1ad7c82014"
TRANSFORMED_SQUAT_SHA256 = "e51b8756a21c7074502023da8b2bd6b0b8da9688a6b07bf26d7c188af1060777"
PROFILE_TRANSFORMED_SQUAT_SHA256 = "9adf2f60b69f330932ab6c4663c4ab2dffb701bd43126e6c75a50f97e2066cad"
ORIGINAL_CHECK_INPUT_FUNCTION_SHA256 = "3f832be570d9d72d0e820c51c301528e66b425fadee941bc68a8e00c8719fe39"
BASE_CHECK_INPUT_FUNCTION_SHA256 = "1c9fbe6b5fa9f7b4afcf3070c9699951afcbef53e18d1c037545ee9f00c1ed38"
ENTER_FUNCTION_SHA256 = "5922425ace3e31d87f6b6ea47b62ed39c510793e16fbcfa17f7289518134b13d"
COMMON_BITS = 0x3F200000


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _file_sha256(path):
    return _sha256(path.read_bytes())


def _matching_brace(source, opening):
    """Return the end of one source brace block, ignoring comments/strings."""
    if source[opening] != "{":
        raise AssertionError("source extraction did not start at an opening brace")
    depth = 0
    state = "code"
    index = opening
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and following == "/":
                state = "code"
                index += 1
        elif state in {"string", "character"}:
            quote = '"' if state == "string" else "'"
            if char == "\\":
                index += 1
            elif char == quote:
                state = "code"
        elif char == "/" and following == "/":
            state = "line_comment"
            index += 1
        elif char == "/" and following == "*":
            state = "block_comment"
            index += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "character"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    raise AssertionError("unterminated source function")


def _extract_function(source, signature, *, following_signature=None):
    starts = []
    offset = 0
    while True:
        start = source.find(signature, offset)
        if start < 0:
            break
        opening = start + len(signature)
        while opening < len(source) and source[opening].isspace():
            opening += 1
        if opening < len(source) and source[opening] == "{":
            starts.append(start)
        offset = start + len(signature)
    if len(starts) != 1:
        raise AssertionError(
            f"expected one pinned definition of {signature!r}, found {len(starts)}",
        )
    start = starts[0]
    if following_signature is not None:
        # A guarded if/else source alternative can contain two opening braces
        # that the C preprocessor reduces to one. Preserve that raw source
        # verbatim up to its pinned next definition instead of balancing both
        # mutually exclusive branches as if they were simultaneously active.
        marker = "\n" + following_signature + "\n{"
        if source.count(marker) != 1:
            raise AssertionError("expected one following source definition")
        end = source.index(marker, start)
        function = source[start:end].rstrip() + "\n"
        if not function.endswith("\n}\n"):
            raise AssertionError("conditional source slice lacks function close")
        return function
    opening = source.index("{", start)
    return source[start:_matching_brace(source, opening)] + "\n"


def _run(command, *, cwd=ROOT, env=None, input_text=None, timeout=120):
    result = subprocess.run(
        command, cwd=cwd, env=env, input=input_text, capture_output=True,
        text=True, timeout=timeout,
    )
    return result


FINITE_VECTORS = (
    ("zero", 0x00000000, 0x00000000, 0, COMMON_BITS),
    ("negative_zero", 0x80000000, 0x00000000, 0, COMMON_BITS),
    ("min_subnormal", 0x00000001, 0x80000001, 0, COMMON_BITS),
    ("max_subnormal", 0x007FFFFF, 0x807FFFFF, 0, COMMON_BITS),
    ("endpoint_one", 0x3F800000, 0xBF800000, 0, COMMON_BITS),
    ("breakpoint_integer_q", 0x3D999A41, 0x00000000, 0, COMMON_BITS),
    ("breakpoint_bits_only", 0x3F133346, 0x00000000, 0, COMMON_BITS),
    ("radius_below", 0x3F147AE1, 0x3F451EB8, 0, COMMON_BITS),
    ("radius_equal", 0x3F147AE1, 0x3F47AE14, 0, COMMON_BITS),
    ("radius_above", 0x3F147AE1, 0x3F4A3D71, 0, COMMON_BITS),
    ("timer_one", 0x3F147AE1, 0x3F4A3D71, 1, COMMON_BITS),
    ("timer_two", 0x3F147AE1, 0x3F4A3D71, 2, COMMON_BITS),
    ("timer_127", 0x3F147AE1, 0x3F4A3D71, 127, COMMON_BITS),
    ("timer_128", 0x3F147AE1, 0x3F4A3D71, 128, COMMON_BITS),
    ("timer_255", 0x3F147AE1, 0x3F4A3D71, 255, COMMON_BITS),
)


def _transition_vectors():
    # Input neighbors are selected by exact bisection, not host arithmetic or
    # production quantization.  Exercise every reachable cell boundary in C.
    vectors = []
    for integer in range(1, 80):
        boundary = oracle._integer_transition_boundary(integer)
        upper = oracle._first_axis_bits_at_least(boundary)
        for label, bits in (("lower", upper - 1), ("upper", upper)):
            for sign_label, sign in (("positive", 0), ("negative", 0x80000000)):
                vectors.append((f"transition_{integer}_{label}_{sign_label}",
                                bits | sign, 0, 0, COMMON_BITS))
    return tuple(vectors)


FINITE_VECTORS += _transition_vectors()


def _vector_input():
    return "".join(
        f"{name} {x:08x} {y:08x} {timer:02x} {common:08x}\n"
        for name, x, y, timer, common in FINITE_VECTORS
    )


def _finite_expected(vector):
    name, x_bits, y_bits, timer, common_bits = vector
    if timer:
        fields = (0, 0, 0, 0, 0, 0, 0, 0, 0, common_bits, 0, timer)
    else:
        x_fma = oracle._fmsubs_axis_bits(x_bits)
        y_fma = oracle._fmsubs_axis_bits(y_bits)
        x_int = oracle._truncate_toward_zero(oracle._binary32_fraction(x_fma))
        y_int = oracle._truncate_toward_zero(oracle._binary32_fraction(y_fma))
        x_conversion = x_int & 0xFFFFFFFF
        y_conversion = y_int & 0xFFFFFFFF
        x_q = (x_conversion + 2) & 0xFFFFFFFF
        y_q = (y_conversion + 2) & 0xFFFFFFFF
        x_square = (x_q * x_q) & 0xFFFFFFFF
        y_square = (y_q * y_q) & 0xFFFFFFFF
        radius = (x_square + y_square) & 0xFFFFFFFF
        selected = oracle._select_threshold_bits(0, x_bits, y_bits, common_bits)
        override = int(selected != common_bits)
        fields = (
            x_fma, y_fma, x_conversion, y_conversion, x_q, y_q,
            x_square, y_square, radius, selected, override, timer,
        )
    return ("R", name, *(
        "nan" if value == "nan" else f"{value:08x}"
        for value in fields[:2]
    ), *(f"{value:08x}" for value in fields[2:10]),
           str(fields[10]), str(fields[11]))


class DboocScalarHelperTests(unittest.TestCase):
    def test_total_scalar_helper_matches_oracle_and_actual_source(self):
        evidence_root = Path(os.environ.get(
            "MELEE_WEB_TEST_OUTPUT", ROOT / "work",
        )).resolve()
        evidence_root.mkdir(parents=True, exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix="issue335-dbooc-scalar-", dir=evidence_root))
        succeeded = False
        try:
            generated, source_identity = self._compose_fixture(scratch)
            vector_input = _vector_input()
            (scratch / "vectors.txt").write_text(vector_input, encoding="ascii")

            sdk = (ROOT / ".deps/emsdk").resolve()
            emcc = sdk / "upstream/emscripten/emcc.py"
            if not emcc.is_file():
                raise AssertionError("pinned project Emscripten compiler is unavailable")
            sys.path.insert(0, str(ROOT / "scripts"))
            from check_gameplay import node_runtime

            native_compiler = shlex.split(os.environ.get("CC", "cc"))
            if not native_compiler or shutil.which(native_compiler[0]) is None:
                raise AssertionError(f"native compiler unavailable: {native_compiler!r}")
            native = scratch / "dbooc-native"
            wasm = scratch / "dbooc-wasm.js"
            common = [
                "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-fno-fast-math", "-ffp-contract=off",
                "-DMELEE_WEB_SLIPPI_PROFILE=1",
                "-I", str(ROOT / "src"), str(generated), str(PROFILE_HELPER),
                "-lm",
            ]
            # Native fenv can vary; Wasm's scalar FP mode is fixed RN-even,
            # and its compiler rejects dynamic-rounding flags under -Werror.
            native_command = [*native_compiler, *common, "-frounding-math",
                              "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                              "-o", str(native)]
            native_compile = self._capture_command(
                scratch, "native-compile", native_command,
            )
            self.assertEqual(native_compile.returncode, 0, native_compile.stdout + native_compile.stderr)

            env = dict(
                os.environ,
                EMSDK=str(sdk),
                EM_CONFIG=str(sdk / ".emscripten"),
                EM_CACHE=str(sdk / "upstream/emscripten/cache"),
                EMSDK_PYTHON=sys.executable,
            )
            wasm_command = [
                sys.executable, str(emcc), *common, "-sENVIRONMENT=node",
                "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
                "-sERROR_ON_UNDEFINED_SYMBOLS=1", "-o", str(wasm),
            ]
            wasm_compile = self._capture_command(
                scratch, "wasm-compile", wasm_command, env=env,
            )
            self.assertEqual(wasm_compile.returncode, 0, wasm_compile.stdout + wasm_compile.stderr)

            native_run = self._capture_command(
                scratch, "native-run", [str(native)], input_text=vector_input,
            )
            self.assertEqual(native_run.returncode, 0, native_run.stdout + native_run.stderr)
            node = node_runtime()
            wasm_run = self._capture_command(
                scratch, "wasm-run", [str(node), str(wasm)], env=env,
                input_text=vector_input,
            )
            self.assertEqual(wasm_run.returncode, 0, wasm_run.stdout + wasm_run.stderr)

            native_rows = self._parse_rows(native_run.stdout)
            wasm_rows = self._parse_rows(wasm_run.stdout)
            self.assertEqual(native_rows["ENV"], wasm_rows["ENV"])
            self.assertEqual(native_rows["ENV"], ("ENV", str(self._rounding_mode_nearest(scratch, native, env))))
            self.assertEqual(native_rows, wasm_rows)
            self._assert_finite_vectors(native_rows)
            self._assert_scalar_exception_controls(native_rows)
            self._assert_conversion_controls(native_rows)
            self._assert_radius_word_controls(native_rows)
            self._assert_actual_source_controls(native_rows)

            result = {
                "schema": "issue335-dbooc-native-wasm-reducer-result-v1",
                "status": "passed",
                "rn_even_checked_by_fixture": True,
                "masked_exceptions_assumption": "process defaults; no trap-enable equivalence claimed",
                "source_identity": source_identity,
                "input_vectors_sha256": _sha256(vector_input.encode("ascii")),
                "native_stdout_sha256": _sha256(native_run.stdout.encode()),
                "wasm_stdout_sha256": _sha256(wasm_run.stdout.encode()),
                "native_equals_wasm": True,
                "finite_oracle_cases": len(FINITE_VECTORS),
                "conversion_cases": len(self.CONVERSIONS),
                "modular_radius_word_cases": len(self.RADIUS_WORDS),
                "source_cases": len(self.SOURCE_CASES) * 3,
                "claims": [
                    "own-C scalar helper agrees with independent finite rational oracle vectors",
                    "native and Node/Wasm helper outputs agree for the listed finite and exceptional controls",
                    "pinned original and overlay-composed SquatRv predicates execute beside an explicit omitted-helper control",
                ],
                "exclusions": [
                    "no FPSCR flag/trap or non-default rounding equivalence",
                    "no original PPC execution, patched Slippi activation, live UCF or gameplay parity",
                    "source services and Fighter data are fixture-only synthetic boundaries",
                ],
            }
            result_path = scratch / "result.json"
            result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            # Keep small provenance and outputs; compiled binaries are disposable.
            success_output = evidence_root / f"{scratch.name}-evidence"
            success_output.mkdir()
            for filename in (
                "source_identity.json", "fixture.c", "vectors.txt",
                "original-preprocess.log", "profile-off-preprocess.log",
                "native-compile.log", "wasm-compile.log",
                "native-run.log", "wasm-run.log", "result.json",
            ):
                shutil.copy2(scratch / filename, success_output / filename)
            shutil.rmtree(scratch)
            succeeded = True
        finally:
            if succeeded:
                self.assertFalse(scratch.exists())

    @staticmethod
    def _capture_command(scratch, label, command, *, env=None, input_text=None):
        result = _run(command, env=env, input_text=input_text)
        (scratch / f"{label}.log").write_text(
            json.dumps({
                "command": [str(argument) for argument in command],
                "returncode": result.returncode,
                "stdout": result.stdout,
                "stderr": result.stderr,
            }, indent=2) + "\n",
            encoding="utf-8",
        )
        return result

    def _compose_fixture(self, scratch):
        lock = json.loads((ROOT / "dependencies.lock.json").read_text(encoding="utf-8"))
        actual_commit = subprocess.check_output(
            ["git", "-C", str(MELEE), "rev-parse", "HEAD"], cwd=ROOT, text=True,
        ).strip()
        self.assertEqual(actual_commit, MELEE_SOURCE_PIN)
        self.assertEqual(lock["repositories"]["melee"]["commit"], MELEE_SOURCE_PIN)
        for path in (UPSTREAM_SQUAT, UPSTREAM_STATES, UPSTREAM_FORWARD, UPSTREAM_COMMON_FORWARD):
            status = subprocess.check_output(
                ["git", "-C", str(MELEE), "status", "--porcelain", "--", str(path.relative_to(MELEE))],
                cwd=ROOT, text=True,
            )
            self.assertEqual(status, "", f"pinned source is locally modified: {path}")
        expected_files = (
            (UPSTREAM_SQUAT, SQUAT_SOURCE_SHA256),
            (UPSTREAM_STATES, STATES_SOURCE_SHA256),
            (UPSTREAM_FORWARD, FORWARD_SOURCE_SHA256),
            (UPSTREAM_COMMON_FORWARD, COMMON_FORWARD_SOURCE_SHA256),
            (BOOL_TRANSFORM, BOOL_TRANSFORM_SHA256),
            (TRIG_HELPER, TRIG_HELPER_SHA256),
        )
        for path, expected_hash in expected_files:
            self.assertEqual(_file_sha256(path), expected_hash, f"source pin changed: {path}")

        states_text = UPSTREAM_STATES.read_text(encoding="utf-8")
        self.assertEqual(states_text.count("// ftCo_MS_SquatRv = 41"), 1)
        forward_text = UPSTREAM_FORWARD.read_text(encoding="utf-8")
        self.assertEqual(forward_text.count("static MotionFlags const Ft_MF_None = 0;"), 1)
        common_forward_text = UPSTREAM_COMMON_FORWARD.read_text(encoding="utf-8")
        self.assertEqual(common_forward_text.count("ftCo_MS_SquatRv,"), 1)

        # The reviewed base patch has no hunk for this source file; confirm
        # that fact and then apply the production boolean-token transform.
        target = "src/melee/ft/kinds/ftCommon/ftCo_SquatRv.c"
        patch_stat = subprocess.check_output(
            ["git", "apply", "--stat", f"--include={target}", str(BASE_PATCH)],
            cwd=ROOT, text=True,
        )
        self.assertIn("0 files changed", patch_stat)
        source_repo = scratch / "composed-source"
        cloned = _run(["git", "clone", "--quiet", "--shared", "--no-checkout",
                       str(MELEE), str(source_repo)])
        self.assertEqual(cloned.returncode, 0, cloned.stdout + cloned.stderr)
        source_path = source_repo / target
        source_path.parent.mkdir(parents=True)
        source_path.write_bytes(UPSTREAM_SQUAT.read_bytes())
        sys.path.insert(0, str(ROOT / "scripts"))
        import gameplay_bool
        composed_text = gameplay_bool.transform_source(Path(target), source_path.read_text(encoding="utf-8"))
        self.assertEqual(_sha256(composed_text.encode()), TRANSFORMED_SQUAT_SHA256)

        # This pinned translation unit has exactly one bool token changed by
        # the production transform.  The predicate/body and Enter function
        # remain byte-identical; the pins are not inferred from a failed run.
        original_text = source_path.read_text(encoding="utf-8")
        original_signature = "bool ftCo_SquatRv_CheckInput(Fighter_GObj* gobj)"
        transformed_signature = "melee_source_bool ftCo_SquatRv_CheckInput(Fighter_GObj* gobj)"
        self.assertEqual(original_text.count(original_signature), 1)
        self.assertEqual(composed_text, original_text.replace(original_signature, transformed_signature, 1))
        original_check = _extract_function(original_text, original_signature)
        self.assertEqual(_sha256(original_check.encode()), ORIGINAL_CHECK_INPUT_FUNCTION_SHA256)
        base_check = _extract_function(composed_text, transformed_signature)
        self.assertEqual(_sha256(base_check.encode()), BASE_CHECK_INPUT_FUNCTION_SHA256)

        # Exercise the actual production composition functions on an owned
        # clone, including every base/boolean/profile hunk. Keep their
        # temporary full source trees on this test's evidence volume.
        from gameplay_sources import compose_slippi_profile_patch
        previous_tempdir = tempfile.tempdir
        try:
            tempfile.tempdir = str(scratch)
            production_base = gameplay_bool.composed_patch(source_repo, BASE_PATCH)
            production_profile = compose_slippi_profile_patch(
                source_repo, production_base, PROFILE_PATCH,
            )
        finally:
            tempfile.tempdir = previous_tempdir

        # This product is HEAD->profile; apply to the original pinned target,
        # then extract the resulting function without rewriting its body.
        checked_patch = _run(
            ["git", "apply", "--check", "--include=" + target,
             str(production_profile)], cwd=source_repo,
        )
        self.assertEqual(checked_patch.returncode, 0,
                         checked_patch.stdout + checked_patch.stderr)
        applied_patch = _run(
            ["git", "apply", "--include=" + target, str(production_profile)],
            cwd=source_repo,
        )
        self.assertEqual(applied_patch.returncode, 0,
                         applied_patch.stdout + applied_patch.stderr)
        profile_text = source_path.read_text(encoding="utf-8")
        self.assertEqual(_sha256(profile_text.encode()), PROFILE_TRANSFORMED_SQUAT_SHA256)
        profile_check = _extract_function(
            profile_text, transformed_signature,
            following_signature="void ftCo_SquatRv_Enter(Fighter_GObj* gobj)",
        )
        profile_enter = _extract_function(
            profile_text, "void ftCo_SquatRv_Enter(Fighter_GObj* gobj)",
        )
        original_enter = _extract_function(
            original_text, "void ftCo_SquatRv_Enter(Fighter_GObj* gobj)",
        )
        self.assertEqual(profile_enter, original_enter)
        self.assertEqual(profile_enter.count(
            "Fighter_ChangeMotionState(gobj, ftCo_MS_SquatRv, Ft_MF_None"), 1)
        self.assertEqual(profile_check.count(
            "slippi_profile_ucf_dbooc_squat_reversal_threshold("), 1)
        self.assertEqual(profile_check.count("fp->x670_timer_lstick_tilt_x"), 1)
        self.assertNotIn("x671_timer_lstick_tilt_y", profile_check)
        self.assertEqual(profile_check.count(
            "fp->input.lstick[0].y > -squat_reversal_threshold"), 1)
        self.assertEqual(profile_check.count(
            "fp->input.lstick[0].y > -p_ftCommonData->x94"), 1)
        self.assertEqual(profile_check.count("ftCo_SquatRv_Enter(gobj);"), 1)
        # Check the actual compiler-preprocessed profile-off branch against
        # the original body, rather than reconstructing its expected logic.
        compiler = shlex.split(os.environ.get("CC", "cc"))
        preprocessed = []
        for label, function in (("original", base_check), ("profile-off", profile_check)):
            output = self._capture_command(
                scratch, label + "-preprocess",
                [*compiler, "-E", "-P", "-x", "c", "-"],
                input_text=function,
            )
            self.assertEqual(output.returncode, 0, output.stderr)
            preprocessed.append(output.stdout)
        self.assertEqual(preprocessed[0], preprocessed[1])
        self.assertIn(
            "fp->input.lstick[0].x, fp->input.lstick[0].y,\n"
            "            fp->x670_timer_lstick_tilt_x, p_ftCommonData->x94",
            profile_check,
        )
        check_hash = _sha256(profile_check.encode())
        enter_hash = _sha256(profile_enter.encode())
        self.assertEqual(enter_hash, ENTER_FUNCTION_SHA256)
        declaration_start = profile_text.index(
            "static void ftCo_SquatRv_Enter(Fighter_GObj* gobj);",
        )
        declaration_end = profile_text.index(";", declaration_start) + 1
        declaration = profile_text[declaration_start:declaration_end] + "\n"

        original_named = base_check.replace(
            transformed_signature,
            "melee_source_bool ftCo_SquatRv_CheckInput_original(Fighter_GObj* gobj)",
            1,
        )
        profile_named = profile_check.replace(
            transformed_signature,
            "melee_source_bool ftCo_SquatRv_CheckInput_profile(Fighter_GObj* gobj)",
            1,
        )
        omitted_named = profile_check.replace(
            transformed_signature,
            "melee_source_bool ftCo_SquatRv_CheckInput_omitted(Fighter_GObj* gobj)",
            1,
        )
        omitted_named = (
            "#define slippi_profile_ucf_dbooc_squat_reversal_threshold "
            "slippi_profile_ucf_dbooc_squat_reversal_threshold_omitted_for_test\n"
            + omitted_named
            + "\n#undef slippi_profile_ucf_dbooc_squat_reversal_threshold\n"
        )

        template = FIXTURE_TEMPLATE.read_text(encoding="utf-8")
        substitutions = {
            "@@SOURCE_ENTER_DECLARATION@@": declaration,
            "@@SOURCE_ORIGINAL_CHECK_INPUT@@": original_named,
            "@@SOURCE_PROFILE_CHECK_INPUT@@": profile_named,
            "@@SOURCE_OMITTED_CHECK_INPUT@@": omitted_named,
            "@@SOURCE_ENTER_FUNCTION@@": profile_enter,
        }
        for marker, replacement in substitutions.items():
            self.assertEqual(template.count(marker), 1, f"fixture marker mismatch: {marker}")
            template = template.replace(marker, replacement)
        generated = scratch / "fixture.c"
        generated.write_text(template, encoding="utf-8")

        import check_gameplay
        sdk = ROOT / ".deps/emsdk"
        source_identity = {
            "production_base_composed_patch_sha256": _file_sha256(production_base),
            "production_profile_composed_patch_sha256": _file_sha256(production_profile),
            "production_composition_functions": [
                "gameplay_bool.composed_patch", "gameplay_sources.compose_slippi_profile_patch",
            ],
            "melee_commit": actual_commit,
            "dependency_lock_sha256": _file_sha256(ROOT / "dependencies.lock.json"),
            "upstream_squat_sha256": _file_sha256(UPSTREAM_SQUAT),
            "base_patch_sha256": _file_sha256(BASE_PATCH),
            "base_patch_changes_target": False,
            "boolean_transform_sha256": _file_sha256(BOOL_TRANSFORM),
            "composed_squat_sha256": _sha256(composed_text.encode()),
            "profile_overlay_sha256": _file_sha256(PROFILE_PATCH),
            "profile_composed_squat_sha256": _sha256(profile_text.encode()),
            "extracted_profile_check_input_sha256": check_hash,
            "extracted_base_check_input_sha256": _sha256(base_check.encode()),
            "original_check_input_sha256": _sha256(original_check.encode()),
            "profile_overlay_applied_after_bool_transform": True,
            "profile_off_preprocessed_equals_original": True,
            "hook_changes_threshold_expression_only": True,
            "extracted_enter_sha256": enter_hash,
            "motion_state_catalog_sha256": _file_sha256(UPSTREAM_STATES),
            "common_forward_sha256": _file_sha256(UPSTREAM_COMMON_FORWARD),
            "ft_forward_sha256": _file_sha256(UPSTREAM_FORWARD),
            "motion_state_source_value": 41,
            "motion_flags_source_value": 0,
            "fixture_template_sha256": _file_sha256(FIXTURE_TEMPLATE),
            "fixture_generated_sha256": _file_sha256(generated),
            "profile_header_sha256": _file_sha256(PROFILE_HEADER),
            "profile_helper_sha256": _file_sha256(PROFILE_HELPER),
            "profile_identity": "slippi-render-dependencies-dbooc-v4",
            "gameplay_trig_sha256": _file_sha256(TRIG_HELPER),
            "emscripten_version": (sdk / "upstream/emscripten/emscripten-version.txt").read_text().strip().strip('"'),
            "node_path": str(check_gameplay.node_runtime()),
        }
        (scratch / "source_identity.json").write_text(
            json.dumps(source_identity, indent=2) + "\n", encoding="utf-8",
        )
        return generated, source_identity

    @staticmethod
    def _parse_rows(text):
        rows = {}
        for line in text.splitlines():
            fields = tuple(line.split("|"))
            if not fields or not fields[0]:
                continue
            if fields[0] == "ENV":
                rows["ENV"] = fields
                continue
            key = fields[1] if fields[0] in {"R", "C", "S"} else fields[1]
            if key in rows:
                raise AssertionError(f"duplicate fixture output row {key!r}")
            rows[key] = fields
        return rows

    @staticmethod
    def _assert_finite_vectors(rows):
        expected_rows = [_finite_expected(vector) for vector in FINITE_VECTORS]
        for expected in expected_rows:
            name = expected[1]
            if name not in rows:
                raise AssertionError(f"missing finite helper row {name!r}")
            if rows[name] != expected:
                raise AssertionError(f"finite helper mismatch for {name}: {rows[name]} != {expected}")

    CONVERSIONS = {
        "pos_inf": (0x7F800000, 0x7FFFFFFF),
        "neg_inf": (0xFF800000, 0x80000000),
        "qnan": (0x7FC00000, 0x80000000),
        "snan": (0x7F800001, 0x80000000),
        "negative_qnan": (0xFFC00000, 0x80000000),
        "negative_snan": (0xFF800001, 0x80000000),
        "conversion_positive_zero": (0x00000000, 0),
        "conversion_negative_zero": (0x80000000, 0),
        "negative_int32": (0xCF000000, 0x80000000),
        "negative_below_int32": (0xCF000001, 0x80000000),
        "negative_above_int32": (0xCEFFFFFF, 0x80000080),
        "positive_int32": (0x4F000000, 0x7FFFFFFF),
        "positive_below_int32": (0x4EFFFFFF, 0x7FFFFF80),
        "positive_above_int32": (0x4F000001, 0x7FFFFFFF),
    }
    RADIUS_WORDS = {
        0x00001900: 0,
        0x00001901: 1,
        0x7FFFFFFF: 1,
        0x80000000: 0,
        0xFFFFFFFF: 0,
    }
    SOURCE_CASES = {
        # x/y, typed X/Y timer bytes, common positive threshold, then expected
        # original/profile/omitted-helper branch results.
        "causal": (0x3F4CCCCD, 0xBF19999A, 0, 255, 0x3F200000, (1, 0, 1)),
        "x_timer_bypass": (0x3F4CCCCD, 0xBF19999A, 1, 0, 0x3F200000, (1, 1, 1)),
        "override_equal": (0x3F800000, 0xBF170A3D, 0, 0, 0x3F200000, (1, 0, 1)),
        "override_adjacent_above": (0x3F800000, 0xBF170A3C, 0, 0, 0x3F200000, (1, 1, 1)),
        "override_adjacent_below": (0x3F800000, 0xBF170A3E, 0, 0, 0x3F200000, (1, 0, 1)),
        "common_equal": (0, 0xBF200000, 0, 0, 0x3F200000, (0, 0, 0)),
        "common_adjacent_above": (0, 0xBF1FFFFF, 0, 0, 0x3F200000, (1, 1, 1)),
    }

    @staticmethod
    def _assert_conversion_controls(rows):
        for name, (input_bits, expected_word) in DboocScalarHelperTests.CONVERSIONS.items():
            expected = ("C", name, f"{input_bits:08x}", f"{expected_word:08x}")
            if rows.get(name) != expected:
                raise AssertionError(f"conversion control mismatch for {name}: {rows.get(name)} != {expected}")

    @staticmethod
    def _assert_radius_word_controls(rows):
        actual = {
            int(fields[1], 16): int(fields[2])
            for fields in rows.values() if fields[0] == "B"
        }
        if actual != DboocScalarHelperTests.RADIUS_WORDS:
            raise AssertionError(f"radius-word comparator mismatch: {actual}")

    @staticmethod
    def _assert_scalar_exception_controls(rows):
        expected = {
            "large_600_single": ("473b8000", "b8d1b717", "0000bb80", "00000000",
                                 "0000bb82", "00000002", "89572e04", "00000004",
                                 "89572e08", "3f200000", "0", "0"),
            "large_600_pair": ("473b8000", "473b8000", "0000bb80", "0000bb80",
                               "0000bb82", "0000bb82", "89572e04", "89572e04",
                               "12ae5c08", "3f170a3d", "1", "0"),
            "max_finite": ("7f800000", "b8d1b717", "7fffffff", "00000000",
                           "80000001", "00000002", "00000001", "00000004",
                           "00000005", "3f200000", "0", "0"),
            # Independently derived from exact rational axis*80-epsilon,
            # RN-even, pinned fctiwz saturation, then modulo32 arithmetic.
            "axis_before_conversion_overflow": (
                "4effffff", "b8d1b717", "7fffff80", "00000000",
                "7fffff82", "00000002", "00003e04", "00000004",
                "00003e08", "3f170a3d", "1", "0"),
            "axis_at_conversion_overflow": (
                "4f000000", "b8d1b717", "7fffffff", "00000000",
                "80000001", "00000002", "00000001", "00000004",
                "00000005", "3f200000", "0", "0"),
            "positive_infinity": ("7f800000", "b8d1b717", "7fffffff", "00000000",
                                 "80000001", "00000002", "00000001", "00000004",
                                 "00000005", "3f200000", "0", "0"),
            "negative_infinity_abs": ("7f800000", "b8d1b717", "7fffffff", "00000000",
                                      "80000001", "00000002", "00000001", "00000004",
                                      "00000005", "3f200000", "0", "0"),
            "quiet_nan": ("nan", "b8d1b717", "80000000", "00000000",
                          "80000002", "00000002", "00000004", "00000004",
                          "00000008", "3f200000", "0", "0"),
            "signaling_nan": ("nan", "b8d1b717", "80000000", "00000000",
                              "80000002", "00000002", "00000004", "00000004",
                              "00000008", "3f200000", "0", "0"),
            "timer_before_snan": ("00000000", "00000000", "00000000", "00000000",
                                  "00000000", "00000000", "00000000", "00000000",
                                  "00000000", "3f200000", "0", "128"),
        }
        expected["negative_axis_before_conversion_overflow"] = expected["axis_before_conversion_overflow"]
        expected["negative_signaling_nan"] = expected["signaling_nan"]
        expected["timer_one_before_snan"] = (*expected["timer_before_snan"][:-1], "1")
        expected["timer_255_before_snan"] = (*expected["timer_before_snan"][:-1], "255")
        for name, expected_tail in expected.items():
            row = rows.get(name)
            if row is None or row[0] != "R" or tuple(row[2:14]) != expected_tail:
                raise AssertionError(f"exception/modular control mismatch for {name}: {row}")

    @staticmethod
    def _assert_actual_source_controls(rows):
        variants = ("original", "profile", "omitted")
        for label, (x_bits, y_bits, x_timer, y_timer, common_bits,
                    expected_results) in DboocScalarHelperTests.SOURCE_CASES.items():
            for variant, expected_result in zip(variants, expected_results):
                name = f"{label}_{variant}"
                row = rows.get(name)
                if row is None or row[0] != "S":
                    raise AssertionError(f"missing extracted source case {name}")
                expected_inputs = (
                    x_bits, y_bits, x_timer, y_timer, common_bits,
                )
                actual_inputs = (
                    int(row[2], 16), int(row[3], 16), int(row[4]),
                    int(row[5]), int(row[6], 16),
                )
                if actual_inputs != expected_inputs or int(row[7]) != expected_result:
                    raise AssertionError(
                        f"source predicate/mapping mismatch for {name}: {row}")
                entered = bool(expected_result)
                expected_calls = 1 if entered else 0
                expected_tail = (
                    expected_calls,
                    expected_calls,
                    int(entered),
                    41 if entered else 0,
                    0,
                    0,
                    0x3F800000 if entered else 0,
                    0,
                    int(entered),
                    int(variant == "omitted"),
                )
                actual_tail = (
                    int(row[8]), int(row[9]), int(row[10]), int(row[11]),
                    int(row[12]), int(row[13], 16), int(row[14], 16),
                    int(row[15], 16), int(row[16]), int(row[17]),
                )
                if actual_tail != expected_tail:
                    raise AssertionError(
                        f"source transition/order/helper control mismatch for {name}: {row}")

        causal_original = rows["causal_original"]
        causal_profile = rows["causal_profile"]
        causal_omitted = rows["causal_omitted"]
        if (causal_original[7], causal_profile[7], causal_omitted[7]) != ("1", "0", "1"):
            raise AssertionError("DBOOC source causal difference was not reproduced")

    @staticmethod
    def _rounding_mode_nearest(scratch, native, env):
        # The fixture itself checked fegetround()==FE_TONEAREST before emitting
        # any results.  This sentinel is reported as an implementation value.
        log = json.loads((scratch / "native-run.log").read_text(encoding="utf-8"))
        first_line = log["stdout"].splitlines()[0]
        if not first_line.startswith("ENV|"):
            raise AssertionError("native fixture did not report its checked rounding mode")
        return int(first_line.split("|", 1)[1])


if __name__ == "__main__":
    unittest.main()
