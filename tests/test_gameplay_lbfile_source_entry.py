"""Compile the real patched lbFile call chain against the source-file registry."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".deps/melee/src/melee/lb/lbfile.c"
PATCH = ROOT / "patches/melee-gameplay.patch"
REGISTRY = ROOT / "src/gameplay_source_files.c"


def _extract_function(source: str, name: str) -> str:
    match = re.search(
        r"\b(?:size_t|void|char\s*\*)\s+" + re.escape(name) + r"\s*\(", source
    )
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


def _extract_filename_limits(source: str) -> str:
    # Actual lbFileGetFullName closure, not guessed fixture limits.
    match = re.search(
        r"^#define MAX_FILENAME_LENGTH .*\n"
        r"const int FILE_EXTENSION_LENGTH = .*\n"
        r"const int MAX_BASENAME_LENGTH = .*;",
        source, re.MULTILINE,
    )
    if match is None:
        raise AssertionError("lbFile source does not define its filename limits")
    return match.group(0)


def _extract_round_up_macro(source: str) -> str:
    match = re.search(r"^#define ROUND_UP_32\(x\).*$", source, re.MULTILINE)
    if match is None:
        raise AssertionError("lbFile source does not define ROUND_UP_32")
    return match.group(0)


FIXTURE_PREFIX = r'''#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gameplay_source_files.h"

typedef struct DVDFileInfo { size_t length; } DVDFileInfo;
typedef MeleeWebSourceFileCallback HSD_DevComCallback;
static bool interrupts_enabled = true;
static unsigned interrupt_disables;
static unsigned interrupt_restores;
static unsigned dvd_fast_open_calls;
static unsigned dvd_close_calls;
static unsigned panic_calls;
static unsigned request_calls;
static unsigned callback_calls;
static unsigned last_callback_cancelled;
static int last_callback_request;
static int last_request_entry;
static uintptr_t last_request_src;
static uintptr_t last_request_destination;
static size_t last_request_size;
static int last_request_type;
static int last_request_priority;
static HSD_DevComCallback last_request_callback;
static void* last_request_args;
static int source_request_id;
static char source_error[160];
static unsigned char* owned_output;
static const uintptr_t WASM_DESTINATION = 0x20000u;
static jmp_buf panic_target;
static int panic_armed;

static void require_fixture(int value, const char* message)
{
    if (!value) {
        fprintf(stderr, "fixture assertion failed: %s\n", message);
        abort();
    }
}

bool OSDisableInterrupts(void)
{
    const bool previous = interrupts_enabled;
    interrupts_enabled = false;
    ++interrupt_disables;
    return previous;
}

void OSRestoreInterrupts(bool previous)
{
    ++interrupt_restores;
    interrupts_enabled = previous;
}

bool DVDFastOpen(int file, DVDFileInfo* info)
{
    ++dvd_fast_open_calls;
    if (file != 7) return false;
    info->length = 0x1234;
    return true;
}

void DVDClose(DVDFileInfo* info)
{
    (void)info;
    ++dvd_close_calls;
}

void OSReport(const char* format, ...)
{
    (void)format;
}

_Noreturn void HSD_Panic(const char* file, int line, const char* message)
{
    (void)file;
    (void)line;
    (void)message;
    ++panic_calls;
    if (!panic_armed) abort();
    longjmp(panic_target, 1);
}

int lbLang_IsSettingUS(void) { return 1; }
int lbLang_IsSavedLanguageUS(void) { return 1; }

#define HSD_ASSERT(line, condition) do { if (!(condition)) HSD_Panic(__FILE__, (line), "fixture HSD_ASSERT"); } while (0)
#define HSD_ASSERTREPORT(line, condition, ...) do { if (!(condition)) HSD_Panic(__FILE__, (line), "fixture HSD_ASSERTREPORT"); } while (0)
#define PAD_STACK(size) ((void)(size))

bool DVDFastOpen(int file, DVDFileInfo* info);
void DVDClose(DVDFileInfo* info);
int DVDConvertPathToEntrynum(const char* path);
int HSD_DevComRequest(int file, uintptr_t src, uintptr_t destination,
                      size_t size, int type, int priority,
                      HSD_DevComCallback callback, void* args);

'''

FIXTURE_SUFFIX = r'''
int DVDConvertPathToEntrynum(const char* path)
{
    return melee_web_source_file_entry(path);
}

int HSD_DevComRequest(int file, uintptr_t src, uintptr_t destination,
                      size_t size, int type, int priority,
                      HSD_DevComCallback callback, void* args)
{
    require_fixture(interrupts_enabled,
                    "lbFile restored interrupts before DevCom request");
    ++request_calls;
    last_request_entry = file;
    last_request_src = src;
    last_request_destination = destination;
    last_request_size = size;
    last_request_type = type;
    last_request_priority = priority;
    last_request_callback = callback;
    last_request_args = args;
    require_fixture(destination == WASM_DESTINATION,
                    "fixture maps the actual 32-bit Wasm pointer");
    int request = 0;
    require_fixture(melee_web_source_file_request(
                        file, src, (uintptr_t)owned_output, size, type,
                        priority, callback, args, &request,
                        source_error, sizeof(source_error)),
                    source_error[0] ? source_error : "source request refused");
    source_request_id = request;
    return request;
}

static void source_complete(int request, int args, void* buffer, int cancelled)
{
    require_fixture(args == 0 && buffer == NULL,
                    "deferred source callback ABI remains unchanged");
    ++callback_calls;
    last_callback_request = request;
    last_callback_cancelled = (unsigned)cancelled;
}

static void expect_invalid_source_size(int entry)
{
    const unsigned opens_before = dvd_fast_open_calls;
    const unsigned restores_before = interrupt_restores;
    panic_armed = 1;
    if (setjmp(panic_target) == 0) {
        (void)lbFile_8001634C(entry);
        abort();
    }
    panic_armed = 0;
    require_fixture(interrupts_enabled &&
                        interrupt_restores == restores_before + 1,
                    "invalid source metadata restores interrupt state");
    require_fixture(dvd_fast_open_calls == opens_before,
                    "invalid source entry never falls through to DVDFastOpen");
}

int main(void)
{
    static const uint8_t logical_bytes[35] = {
        0x47, 0x72, 0x50, 0x73, 0x33, 0x2e, 0x75, 0x73, 0x64,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21,
        0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28
    };
    static const uint8_t tail_bytes[29] = {
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
        0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
        0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc
    };
    static const MeleeWebSourceFileInput inputs[] = {
        {"GrPs3.usd", logical_bytes, sizeof(logical_bytes),
         tail_bytes, sizeof(tail_bytes)}
    };
    char error[160] = {0};
    size_t length = 0xfeedu;
    const int synthetic = MELEE_WEB_SOURCE_FILE_ENTRY_BASE;
    require_fixture(!melee_web_source_file_entry_size(synthetic, &length) &&
                        length == 0xfeedu,
                    "inactive registry refuses synthetic entry without output mutation");
    require_fixture(!melee_web_source_file_entry_size(-1, &length) &&
                        length == 0xfeedu,
                    "missing entry is refused");

    MeleeWebSourceFileScope* scope = melee_web_source_files_begin(
        inputs, sizeof(inputs) / sizeof(inputs[0]), error, sizeof(error));
    require_fixture(scope != NULL, error[0] ? error : "source registry start failed");
    const int entry = melee_web_source_file_entry("GrPs3.usd");
    require_fixture(entry == synthetic && melee_web_source_file_entry_owned(entry),
                    "entry comes from the active exact-name registry");
    require_fixture(melee_web_source_file_entry_size(entry, &length) &&
                        length == sizeof(logical_bytes),
                    "metadata returns logical bytes, excluding transfer tail");
    length = 0xbeefu;
    require_fixture(!melee_web_source_file_entry_size(entry + 1, &length) &&
                        length == 0xbeefu,
                    "out-of-range source entry is refused without output mutation");
    require_fixture(!melee_web_source_file_entry_size(entry, NULL),
                    "null size destination is refused");
    expect_invalid_source_size(entry + 1);

    const unsigned opens_before = dvd_fast_open_calls;
    const unsigned disables_before = interrupt_disables;
    const unsigned restores_before = interrupt_restores;
    require_fixture(lbFile_8001634C(entry) == sizeof(logical_bytes),
                    "patched 8001634C resolves active source metadata");
    require_fixture(dvd_fast_open_calls == opens_before &&
                        interrupt_disables == disables_before + 1 &&
                        interrupt_restores == restores_before + 1 &&
                        interrupts_enabled,
                    "source metadata lookup preserves the interrupt bracket and avoids DVD open");

    _Alignas(32) static uint8_t output[64];
    memset(output, 0xcc, sizeof(output));
    owned_output = output;
    require_fixture(melee_web_source_file_region_register(
                        output, sizeof(output), error, sizeof(error)),
                    error[0] ? error : "output ownership registration failed");
    size_t returned_size = 0;
    lbFile_80016580("GrPs3", (void*)WASM_DESTINATION, &returned_size,
                    source_complete, NULL);
    require_fixture(returned_size == sizeof(logical_bytes),
                    "original lbFile path reports logical file size");
    require_fixture(request_calls == 1 && last_request_entry == entry &&
                        last_request_src == 0 &&
                        last_request_destination == WASM_DESTINATION &&
                        last_request_size == 64 && last_request_type == 0x23 &&
                        last_request_priority == 1 &&
                        last_request_callback == source_complete &&
                        last_request_args == NULL,
                    "80016580→800164A4 preserves rounded DevCom arguments");
    require_fixture(callback_calls == 0 &&
                        !melee_web_source_files_quiescent_for(
                            scope, error, sizeof(error)) &&
                        output[0] == 0xcc,
                    "request remains deferred and destination untouched before pump");
    require_fixture(melee_web_source_files_pump_for(
                        scope, error, sizeof(error)),
                    error[0] ? error : "owned source pump failed");
    require_fixture(callback_calls == 1 &&
                        last_callback_request == source_request_id &&
                        last_callback_cancelled == 0,
                    "original callback runs only after the owning source pump");
    require_fixture(memcmp(output, logical_bytes, sizeof(logical_bytes)) == 0 &&
                        memcmp(output + sizeof(logical_bytes), tail_bytes,
                               sizeof(tail_bytes)) == 0,
                    "rounded transfer copies exact logical bytes and authenticated tail");
    const char* copied_name = melee_web_source_file_take_name(output);
    require_fixture(copied_name && strcmp(copied_name, "GrPs3.usd") == 0,
                    "completed full transfer retains exact source identity");
    require_fixture(melee_web_source_file_region_unregister(
                        output, sizeof(output), error, sizeof(error)),
                    error[0] ? error : "output ownership release failed");

    const unsigned requests_before_missing = request_calls;
    panic_armed = 1;
    if (setjmp(panic_target) == 0) {
        lbFile_80016580("Missing", (void*)WASM_DESTINATION, &returned_size,
                        source_complete, NULL);
        abort();
    }
    panic_armed = 0;
    require_fixture(request_calls == requests_before_missing,
                    "missing source filename refuses before DevCom request");
    require_fixture(melee_web_source_files_end(scope, error, sizeof(error)),
                    error[0] ? error : "source scope did not end cleanly");
    length = 0xcafeu;
    require_fixture(!melee_web_source_file_entry_size(entry, &length) &&
                        length == 0xcafeu,
                    "closed source scope refuses stale entry");
    expect_invalid_source_size(entry);
    expect_invalid_source_size(MELEE_WEB_SOURCE_FILE_ENTRY_BASE + 99);

    const unsigned legacy_opens = dvd_fast_open_calls;
    const unsigned legacy_closes = dvd_close_calls;
    require_fixture(lbFile_8001634C(7) == 0x1234 &&
                        dvd_fast_open_calls == legacy_opens + 1 &&
                        dvd_close_calls == legacy_closes + 1 &&
                        interrupts_enabled,
                    "ordinary FST entries retain the original DVDFastOpen path");
    puts("source lbFile entry metadata, rounded request and deferred callback: passed");
    return 0;
}
'''


class GameplayLbFileSourceEntryTests(unittest.TestCase):
    def test_actual_lbfile_chain_uses_checked_registry_metadata_and_deferred_read(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            self.skipTest("A native C compiler is required")
        if not UPSTREAM.is_file() or not PATCH.is_file() or not REGISTRY.is_file():
            self.skipTest("Pinned Melee source, patch, and registry are required")

        # The caller supplies an owned TMPDIR. Stay outside the checkout so
        # git apply cannot silently filter this patch through a parent worktree.
        directory = Path(tempfile.mkdtemp(prefix="lbfile-source-entry-"))
        succeeded = False
        try:
            source_path = directory / "src/melee/lb/lbfile.c"
            source_path.parent.mkdir(parents=True)
            shutil.copy2(UPSTREAM, source_path)
            applied = subprocess.run(
                ["git", "apply", "--no-index", "--include=src/melee/lb/lbfile.c", str(PATCH)],
                cwd=directory, capture_output=True, text=True, timeout=30,
            )
            (directory / "apply.stdout").write_text(applied.stdout)
            (directory / "apply.stderr").write_text(applied.stderr)
            self.assertEqual(applied.returncode, 0, applied.stdout + applied.stderr)
            patched = source_path.read_text()
            self.assertIn("melee_web_source_file_entry_size(fileno, &length)", patched,
                          "The authoritative lbFile metadata hunk must be applied")
            actual_functions = "\n\n".join(
                _extract_function(patched, name)
                for name in (
                    "lbFileGetFullName",
                    "lbFile_8001634C",
                    "lbFile_800164A4",
                    "lbFile_80016580",
                )
            )
            macro = _extract_round_up_macro(patched)
            generated = directory / "lbfile_source_entry_trace.c"
            generated.write_text(
                FIXTURE_PREFIX + _extract_filename_limits(patched) + "\n\n" +
                macro + "\n\n" + actual_functions +
                FIXTURE_SUFFIX
            )
            binary = directory / "lbfile_source_entry_trace"
            built = subprocess.run(
                [compiler, "-std=c11", "-DMELEE_WEB_GAMEPLAY=1", "-O0",
                 "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"),
                 str(generated), str(REGISTRY), "-o", str(binary)],
                cwd=directory, capture_output=True, text=True, timeout=60,
            )
            (directory / "compile.stdout").write_text(built.stdout)
            (directory / "compile.stderr").write_text(built.stderr)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], cwd=directory,
                                 capture_output=True, text=True, timeout=30)
            (directory / "run.stdout").write_text(run.stdout)
            (directory / "run.stderr").write_text(run.stderr)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn(
                "source lbFile entry metadata, rounded request and deferred callback: passed",
                run.stdout,
            )
            succeeded = True
        except BaseException as error:
            (directory / "failure.txt").write_text(repr(error) + "\n")
            print("Retained lbFile source-entry reproducer: " + str(directory), file=sys.stderr)
            raise
        finally:
            if succeeded:
                shutil.rmtree(directory)


if __name__ == "__main__":
    unittest.main()
