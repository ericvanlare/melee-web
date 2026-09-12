"""Compile the patched retail camera translation body in a separated-global fixture."""

from __future__ import annotations

import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def _extract_function(source: str, name: str) -> str:
    match = re.search(r"\bvoid\s+" + re.escape(name) + r"\s*\(", source)
    if match is None:
        raise AssertionError(f"source does not define {name}")
    opening = source.index("{", match.end())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1]
    raise AssertionError(f"unterminated function {name}")


def _extract_declaration(source: str, type_name: str, name: str) -> str:
    start = source.index(f"static {type_name} {name} =")
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                end = source.index(";", index) + 1
                return source[start:end]
    raise AssertionError(f"unterminated declaration {name}")


def _apply_camera_patch(directory: Path) -> tuple[str, str, str]:
    original_path = ROOT / ".deps/melee/src/melee/cm/camera.c"
    patch_path = ROOT / "patches/melee-gameplay.patch"
    if not original_path.is_file() or not patch_path.is_file():
        raise unittest.SkipTest("Melee source checkout and gameplay patch are required")

    source_path = directory / "src/melee/cm/camera.c"
    source_path.parent.mkdir(parents=True)
    source_path.write_text(original_path.read_text())
    applied = subprocess.run(
        ["git", "apply", "--include=src/melee/cm/camera.c", str(patch_path)],
        cwd=directory, capture_output=True, text=True, timeout=30,
    )
    if applied.returncode != 0:
        raise AssertionError(applied.stdout + applied.stderr)

    patched = source_path.read_text()
    return (
        _extract_function(patched, "Camera_8002A0C0"),
        _extract_function(original_path.read_text(), "Camera_8002A0C0"),
        _extract_declaration(
            patched, "HSD_CameraDescPerspective", "cm_803BCB64"),
    )


_FIXTURE_PREFIX = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef float f32;
typedef struct { int16_t xmin, xmax, ymin, ymax; } HSD_RectS16;
typedef struct { void* parent; float pos[3]; int flags; } HSD_WObjDesc;
typedef struct {
    void* class_name;
    uint16_t flags;
    uint16_t projection_type;
    HSD_RectS16 viewport;
    HSD_RectS16 scissor;
    HSD_WObjDesc* eyepos;
    HSD_WObjDesc* interest;
    float roll;
    void* up_vector;
    float nearz, farz, fov, aspect;
} HSD_CameraDescPerspective;
typedef struct { float z_pos; } CameraBounds;
typedef struct {
    float interest[3], target_interest[3], position[3], target_position[3];
    float fov, target_fov;
} CameraTransformState;
typedef struct { float xA4, xA8, xAC, x2BC; } CameraState;
typedef struct { float x54, x58, x5C, x60, xE8; } CameraUnkGlobals;
typedef struct { unsigned char bytes[0x24]; } CameraModeCallbacks;
struct CallbackStorage {
    CameraModeCallbacks callbacks;
    /* Also cover the larger 64-bit host layout used by this fixture. */
    unsigned char guard[0x100];
};

static HSD_WObjDesc cm_803BCB3C = {NULL, {0.0f, 40.241425f, 300.241f}, 0};
static HSD_WObjDesc cm_803BCB50 = {NULL, {0.0f, 10.0f, 0.0f}, 0};
/* This table is deliberately separate from the descriptor.  The guard makes
 * a retail adjacency cast observe invalid descriptor bytes deterministically. */
static struct CallbackStorage cm_803BCB18_storage;
#define cm_803BCB18 cm_803BCB18_storage.callbacks
static CameraState cm_80452C68;
static CameraUnkGlobals cm_803BCCA0;
static float observed_x;
static float observed_y;

static int gm_8016B41C(void) { return 0; }
static float Stage_GetCamZoomRate(void) { return 100.0f; }
static float Stage_GetCamMaxDepth(void) { return 300.0f; }
static void Camera_80030DE4(float x, float y) {
    observed_x = x;
    observed_y = y;
}

'''


def _fixture_source(function: str, descriptor: str, control: bool) -> str:
    main = r'''
int main(void) {
    (void)&cm_803BCB64;
    memset(&cm_803BCB18_storage, 0xA5, sizeof(cm_803BCB18_storage));
    memset(&cm_80452C68, 0, sizeof(cm_80452C68));
    cm_80452C68.xA4 = 0.25f;
    cm_80452C68.xA8 = -0.5f;
    cm_80452C68.xAC = 1.0f;
    cm_80452C68.x2BC = 1.0f;
    cm_803BCCA0.x54 = 0.5f;
    cm_803BCCA0.x58 = 0.5f;
    cm_803BCCA0.x5C = 1.5f;
    cm_803BCCA0.x60 = 1.5f;
    cm_803BCCA0.xE8 = 1.0f;
    CameraBounds bounds = {300.0f};
    CameraTransformState state = {{0}, {0}, {0}, {0}, 30.0f, 30.0f};
    Camera_8002A0C0(&bounds, &state);
'''
    if control:
        main += r'''
    /* The old body reads its fake descriptor from the A5 guard. */
    if (isfinite(observed_x) || isfinite(observed_y)) {
        fprintf(stderr, "old adjacency body unexpectedly produced finite output\n");
        return 2;
    }
    puts("old-control-rejected");
    return 0;
'''
    else:
        main += r'''
    union { float f; uint32_t u; } x = {observed_x}, y = {observed_y};
    if (x.u != 0x3f92c856u || y.u != 0xc020c4ffu ||
        cm_80452C68.xA4 != 0.0f || cm_80452C68.xA8 != 0.0f ||
        !isfinite(observed_x) || !isfinite(observed_y)) {
        fprintf(stderr, "unexpected translation x=%08x y=%08x reset=%g,%g\n",
                x.u, y.u, cm_80452C68.xA4, cm_80452C68.xA8);
        return 2;
    }
    puts("patched-direct-descriptor");
    return 0;
'''
    return _FIXTURE_PREFIX + descriptor + "\n" + function + "\n" + main + "}\n"


class CameraTranslationTests(unittest.TestCase):
    def test_patched_camera_uses_separate_descriptor_and_resets_inputs(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            self.skipTest("A C compiler is required")

        with tempfile.TemporaryDirectory(prefix="camera-translation-") as directory:
            patched, original, descriptor = _apply_camera_patch(Path(directory))
            self.assertIn(
                "const HSD_CameraDescPerspective* desc = &cm_803BCB64;", patched)
            self.assertIn("desc->aspect", patched)
            self.assertIn("desc->viewport", patched)
            self.assertNotIn("struct CameraStaticData", patched)
            self.assertIn("struct CameraStaticData", original)
            self.assertIn("data->desc.aspect", original)
            self.assertIn("1.2173333f", descriptor)
            self.assertIn("0x280", descriptor)
            self.assertIn("0x1E0", descriptor)

            positive_source = Path(directory) / "camera_positive.c"
            positive_binary = Path(directory) / "camera_positive"
            positive_source.write_text(_fixture_source(patched, descriptor, False))
            compiled = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-O0", "-ffp-contract=off", str(positive_source), "-lm",
                 "-o", str(positive_binary)],
                cwd=directory, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            result = subprocess.run(
                [str(positive_binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), "patched-direct-descriptor")

            control_source = Path(directory) / "camera_control.c"
            control_binary = Path(directory) / "camera_control"
            control_source.write_text(_fixture_source(original, descriptor, True))
            compiled = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-O0", "-ffp-contract=off", str(control_source), "-lm",
                 "-o", str(control_binary)],
                cwd=directory, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            result = subprocess.run(
                [str(control_binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), "old-control-rejected")


if __name__ == "__main__":
    unittest.main()
