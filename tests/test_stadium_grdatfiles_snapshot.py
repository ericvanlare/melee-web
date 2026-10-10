"""Compile the exact generated grDatFiles raw snapshot copier in isolation."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
GAMEPLAY_SOURCE = ROOT / "build" / "gameplay-source"
GRDATFILES = GAMEPLAY_SOURCE / "src" / "melee" / "gr" / "grdatfiles.c"
GR_TYPES = GAMEPLAY_SOURCE / "src" / "melee" / "gr" / "types.h"
GR_FORWARD = GAMEPLAY_SOURCE / "src" / "melee" / "gr" / "forward.h"
HSD_FORWARD = GAMEPLAY_SOURCE / "src" / "sysdolphin" / "baselib" / "forward.h"
DOLPHIN_TYPES = GAMEPLAY_SOURCE / "extern" / "dolphin" / "include" / "dolphin" / "types.h"
GETTER = "melee_web_stadium_c1_grdatfiles_copy"


def _balanced_declaration(source: str, declaration_start: int, brace: int) -> str:
    """Return one C declaration through its matching closing brace."""
    depth = 0
    state = "code"
    escaped = False
    index = brace
    while index < len(source):
        char = source[index]
        nxt = source[index + 1] if index + 1 < len(source) else ""
        if state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and nxt == "/":
                state = "code"
                index += 1
        elif state in ("string", "char"):
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif (state == "string" and char == '"') or (state == "char" and char == "'"):
                state = "code"
        else:
            if char == "/" and nxt == "/":
                state = "line_comment"
                index += 1
            elif char == "/" and nxt == "*":
                state = "block_comment"
                index += 1
            elif char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[declaration_start:index + 1]
                if depth < 0:
                    break
        index += 1
    raise AssertionError("C declaration has no balanced closing brace")


def _extract_definition(source: str, name: str) -> str:
    pattern = re.compile(r"\bint\s+" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", re.S)
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise AssertionError(f"expected exactly one definition of {name}, found {len(matches)}")
    match = matches[0]
    return _balanced_declaration(source, match.start(), match.end() - 1)


def _extract_struct(source: str, name: str) -> str:
    pattern = re.compile(r"\bstruct\s+" + re.escape(name) + r"\s*\{")
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise AssertionError(f"expected exactly one struct {name}, found {len(matches)}")
    match = matches[0]
    return _balanced_declaration(source, match.start(), match.end() - 1) + ";"


def _extract_typedef_line(source: str, target: str) -> str:
    matches = [line.strip() for line in source.splitlines()
               if re.fullmatch(r"typedef\s+[^;]+\s+" + re.escape(target) + r";", line.strip())]
    if len(matches) != 1:
        raise AssertionError(f"expected exactly one actual typedef for {target}, found {len(matches)}")
    return matches[0]


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


FIXTURE = r'''#include <stdint.h>
#include <stdio.h>
#include <string.h>

@HSD_ARCHIVE_FORWARD@
@GR_FORWARD@
@U32_ALIAS@
@ARCHIVE_STRUCT@

static UnkArchiveStruct grDatFiles_8049EE10[4];
static UnkArchiveStruct native_archive_rows[4];
static unsigned char archive_tokens[4];
static unsigned char stage_tokens[4];
static unsigned char native_archive_tokens[4];
static unsigned char native_stage_tokens[4];
static unsigned char output_tokens[16];

UnkArchiveStruct* melee_web_stage_map_archives(void)
{
    return native_archive_rows;
}

UnkArchiveStruct* grDatFiles_GetArchive(void)
{
    return melee_web_stage_map_archives();
}

@EXACT_GETTER@

static int same_row(const UnkArchiveStruct* left, const UnkArchiveStruct* right)
{
    return left->unk0 == right->unk0 && left->unk4 == right->unk4 &&
           left->unk8 == right->unk8;
}

static int same_table(const UnkArchiveStruct left[4],
                      const UnkArchiveStruct right[4])
{
    for (unsigned i = 0; i < 4; ++i)
        if (!same_row(&left[i], &right[i])) return 0;
    return 1;
}

static int all_zero(const UnkArchiveStruct rows[4])
{
    for (unsigned i = 0; i < 4; ++i)
        if (rows[i].unk0 != NULL || rows[i].unk4 != NULL || rows[i].unk8 != 0)
            return 0;
    return 1;
}

static void seed_tables(void)
{
    memset(grDatFiles_8049EE10, 0, sizeof(grDatFiles_8049EE10));
    memset(native_archive_rows, 0, sizeof(native_archive_rows));

    grDatFiles_8049EE10[1].unk0 = (HSD_Archive*) (intptr_t) -1;
    grDatFiles_8049EE10[1].unk4 = (UnkStageDat*) (intptr_t) -1;
    grDatFiles_8049EE10[1].unk8 = 0x80000001UL;
    grDatFiles_8049EE10[2].unk0 = (HSD_Archive*) &archive_tokens[2];
    grDatFiles_8049EE10[2].unk4 = (UnkStageDat*) &stage_tokens[2];
    grDatFiles_8049EE10[2].unk8 = 0x12345678UL;
    grDatFiles_8049EE10[3].unk4 = (UnkStageDat*) &stage_tokens[3];
    grDatFiles_8049EE10[3].unk8 = 0xFFFFFFFFUL;

    for (unsigned i = 0; i < 4; ++i) {
        native_archive_rows[i].unk0 =
            (HSD_Archive*) &native_archive_tokens[i];
        native_archive_rows[i].unk4 = (UnkStageDat*) &native_stage_tokens[i];
        native_archive_rows[i].unk8 = 0xA0000000UL + i;
    }
}

static void seed_output(UnkArchiveStruct rows[4])
{
    memset(rows, 0, sizeof(UnkArchiveStruct) * 4);
    for (unsigned i = 0; i < 4; ++i) {
        rows[i].unk0 = (HSD_Archive*) &output_tokens[0];
        rows[i].unk4 = (UnkStageDat*) &output_tokens[1];
        rows[i].unk8 = 0x55000000UL + i;
    }
}

static int check_null_argument(unsigned which)
{
    UnkArchiveStruct rows[4];
    UnkArchiveStruct rows_before[4];
    seed_output(rows);
    memcpy(rows_before, rows, sizeof(rows));
    UnkArchiveStruct* ordinary =
        (UnkArchiveStruct*) &output_tokens[2];
    UnkArchiveStruct* ordinary_before = ordinary;
    void* native = &output_tokens[3];
    void* native_before = native;
    UnkArchiveStruct* effective =
        (UnkArchiveStruct*) &output_tokens[4];
    UnkArchiveStruct* effective_before = effective;
    int result;

    if (which == 0)
        result = melee_web_stadium_c1_grdatfiles_copy(
            NULL, &ordinary, &native, &effective);
    else if (which == 1)
        result = melee_web_stadium_c1_grdatfiles_copy(
            rows, NULL, &native, &effective);
    else if (which == 2)
        result = melee_web_stadium_c1_grdatfiles_copy(
            rows, &ordinary, NULL, &effective);
    else
        result = melee_web_stadium_c1_grdatfiles_copy(
            rows, &ordinary, &native, NULL);

    return result == 0 && same_table(rows, rows_before) &&
           ordinary == ordinary_before && native == native_before &&
           effective == effective_before;
}

int main(void)
{
    UnkArchiveStruct copied[4];
    UnkArchiveStruct expected[4];
    UnkArchiveStruct ordinary_before[4];
    UnkArchiveStruct native_before[4];
    UnkArchiveStruct* ordinary = NULL;
    void* native = NULL;
    UnkArchiveStruct* effective = NULL;

    memset(grDatFiles_8049EE10, 0, sizeof(grDatFiles_8049EE10));
    memset(native_archive_rows, 0, sizeof(native_archive_rows));
    seed_output(copied);
    if (!melee_web_stadium_c1_grdatfiles_copy(
            copied, &ordinary, &native, &effective) ||
        !all_zero(copied) || ordinary != grDatFiles_8049EE10 ||
        native != native_archive_rows || effective != native_archive_rows ||
        !all_zero(grDatFiles_8049EE10) || !all_zero(native_archive_rows)) {
        fputs("zero-table or identity check failed\n", stderr);
        return 1;
    }

    seed_tables();
    memcpy(expected, grDatFiles_8049EE10, sizeof(expected));
    memcpy(ordinary_before, grDatFiles_8049EE10, sizeof(ordinary_before));
    memcpy(native_before, native_archive_rows, sizeof(native_before));
    seed_output(copied);
    ordinary = NULL;
    native = NULL;
    effective = NULL;
    if (!melee_web_stadium_c1_grdatfiles_copy(
            copied, &ordinary, &native, &effective) ||
        !same_table(copied, expected) ||
        copied[1].unk0 != (HSD_Archive*) (intptr_t) -1 ||
        copied[1].unk4 != (UnkStageDat*) (intptr_t) -1 ||
        copied[1].unk8 != 0x80000001UL ||
        ordinary != grDatFiles_8049EE10 ||
        native != native_archive_rows || effective != native_archive_rows ||
        !same_table(grDatFiles_8049EE10, ordinary_before) ||
        !same_table(native_archive_rows, native_before)) {
        fputs("raw rows, sentinel, high-bit field, identity or read-only check failed\n",
              stderr);
        return 1;
    }

    for (unsigned i = 0; i < 4; ++i) {
        if (!check_null_argument(i)) {
            fprintf(stderr, "null-argument no-write check failed at %u\n", i);
            return 1;
        }
    }
    puts("grDatFiles generated-source copier fixture: PASS");
    return 0;
}
'''


class StadiumGrDatFilesSnapshotTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(ROOT, "stadium-grdatfiles-snapshot-")

    def test_exact_generated_copier_and_read_only_controls(self):
        generated = (GRDATFILES, GR_TYPES, GR_FORWARD, HSD_FORWARD, DOLPHIN_TYPES)
        if not all(path.is_file() for path in generated):
            self.skipTest("Canonical generated gameplay source is unavailable")
        compiler = (os.environ.get("CC") or shutil.which("clang") or
                    shutil.which("cc"))
        if compiler is None:
            self.skipTest("C compiler unavailable")

        source_text = GRDATFILES.read_text(encoding="utf-8")
        types_text = GR_TYPES.read_text(encoding="utf-8")
        gr_forward_text = GR_FORWARD.read_text(encoding="utf-8")
        hsd_forward_text = HSD_FORWARD.read_text(encoding="utf-8")
        dolphin_types_text = DOLPHIN_TYPES.read_text(encoding="utf-8")
        getter = _extract_definition(source_text, GETTER)
        struct = _extract_struct(types_text, "UnkArchiveStruct")
        u32_alias = _extract_typedef_line(dolphin_types_text, "u32")
        gr_aliases = "\n".join((
            _extract_typedef_line(gr_forward_text, "UnkArchiveStruct"),
            _extract_typedef_line(gr_forward_text, "UnkStageDat"),
        ))
        hsd_alias = _extract_typedef_line(hsd_forward_text, "HSD_Archive")

        fixture = FIXTURE
        fixture = fixture.replace("@HSD_ARCHIVE_FORWARD@", hsd_alias)
        fixture = fixture.replace("@GR_FORWARD@", gr_aliases)
        fixture = fixture.replace("@U32_ALIAS@", u32_alias)
        fixture = fixture.replace("@ARCHIVE_STRUCT@", struct)
        fixture = fixture.replace("@EXACT_GETTER@", getter)
        self.assertIsNone(re.search(r"@[A-Z][A-Z_]*@", fixture),
                          "fixture retained an unresolved source marker")

        fixture_path = self.workspace / "grdatfiles_snapshot_fixture.c"
        binary = self.workspace / "grdatfiles_snapshot_fixture"
        fixture_path.write_text(fixture, encoding="utf-8")
        identity = {
            "host_c_compiler": compiler,
            "getter_name": GETTER,
            "getter_source_line": source_text[:source_text.index(getter)].count("\n") + 1,
            "generated_grdatfiles_path": "build/gameplay-source/src/melee/gr/grdatfiles.c",
            "generated_grdatfiles_sha256": _sha256(GRDATFILES.read_bytes()),
            "exact_getter_sha256": _sha256(getter.encode("utf-8")),
            "generated_gr_types_sha256": _sha256(GR_TYPES.read_bytes()),
            "exact_struct_sha256": _sha256(struct.encode("utf-8")),
            "generated_gr_forward_sha256": _sha256(GR_FORWARD.read_bytes()),
            "generated_hsd_forward_sha256": _sha256(HSD_FORWARD.read_bytes()),
            "generated_dolphin_types_sha256": _sha256(DOLPHIN_TYPES.read_bytes()),
            "actual_u32_alias": u32_alias,
        }
        (self.workspace / "source_identity.json").write_text(
            json.dumps(identity, indent=2) + "\n", encoding="utf-8")

        compiled = subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             str(fixture_path), "-o", str(binary)],
            cwd=ROOT, capture_output=True, text=True, timeout=30, check=False)
        (self.workspace / "compile.log").write_text(
            compiled.stdout + compiled.stderr, encoding="utf-8")
        self.assertEqual(compiled.returncode, 0,
                         compiled.stdout + compiled.stderr)
        observed = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                                  text=True, timeout=10, check=False)
        (self.workspace / "run.log").write_text(
            observed.stdout + observed.stderr, encoding="utf-8")
        self.assertEqual(observed.returncode, 0,
                         observed.stdout + observed.stderr)
        self.assertEqual(observed.stdout.strip(),
                         "grDatFiles generated-source copier fixture: PASS")
        print("STADIUM_GRDATFILES_COPIER_SOURCE " +
              json.dumps(identity, sort_keys=True))


if __name__ == "__main__":
    unittest.main()
