"""Exercise the real network diagnostic arena-fill lifecycle wrapper."""
from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "gameplay_net_input.c"
SCRATCH_PARENT = ROOT / "work" / "net-arena-fill-tests"


def _function(source: str, name: str) -> str:
    import re

    match = re.search(r"(?:static\s+)?(?:void|int)\s+" + re.escape(name) + r"\b", source)
    if not match:
        raise AssertionError(f"source function is absent: {name}")
    brace = source.find("{", match.end())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1]
    raise AssertionError(f"unterminated source function: {name}")


def _harness() -> str:
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index("typedef struct NetSession {")
    end = source.index("} NetSession;", start) + len("} NetSession;")
    state = source[start:end]
    reset = _function(source, "melee_web_net_reset")
    begin = _function(source, "melee_web_net_session_begin")
    return f'''#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef struct {{ unsigned value; }} PADStatus;
typedef struct {{ uint32_t words[16]; }} MeleeWebNetChecksumRecord;
typedef struct {{ uint32_t tick, scene, base, bytes; uint64_t hash; }} MeleeWebNetArenaRecord;
typedef struct MeleeWebMenuHost MeleeWebMenuHost;
#define MELEE_WEB_NET_CHECKSUM_RING 8192
#define MELEE_WEB_NET_ARENA_RECORDS 64
static unsigned normal_calls, pattern_calls;
static int last_pattern = -2;
static int normal_ok = 1, pattern_ok = 1;
int melee_web_gameplay_session_begin(size_t bytes, char* error, size_t size)
{{ (void)bytes; (void)error; (void)size; ++normal_calls; return normal_ok; }}
int melee_web_gameplay_session_begin_with_pattern(size_t bytes, int pattern,
                                                  char* error, size_t size)
{{ (void)bytes; (void)error; (void)size; ++pattern_calls; last_pattern=pattern; return pattern_ok; }}
{state}
static NetSession net = {{.arena_fill = -1}};
{reset}
{begin}
int main(void)
{{
    char error[32] = {{0}};
    net.arena_fill = 0;
    net.arena_fill_pending = 1;
    melee_web_net_reset();
    if (!net.arena_fill_pending || net.arena_fill != 0) return 1;
    if (!melee_web_net_session_begin(32U * 1024U * 1024U, error, sizeof(error))) return 2;
    if (pattern_calls != 1 || last_pattern != 0 || net.arena_fill_pending || net.arena_fill != 0) return 3;
    melee_web_net_reset();
    if (!melee_web_net_session_begin(32U * 1024U * 1024U, error, sizeof(error))) return 4;
    if (normal_calls != 1 || pattern_calls != 1 || net.arena_fill_pending || net.arena_fill != -1) return 5;
    melee_web_net_reset();
    if (!melee_web_net_session_begin(32U * 1024U * 1024U, error, sizeof(error))) return 6;
    if (normal_calls != 2 || net.arena_fill != -1) return 7;
    net.arena_fill = 165;
    net.arena_fill_pending = 1;
    pattern_ok = 0;
    if (melee_web_net_session_begin(32U * 1024U * 1024U, error, sizeof(error))) return 8;
    if (!net.arena_fill_pending || net.arena_fill != 165 || pattern_calls != 2) return 9;
    return 0;
}}
'''


class NetArenaFillLifetimeTests(unittest.TestCase):
    def test_pattern_is_consumed_once_and_not_reported_for_later_normal_arenas(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.fail("cc is required for the network arena-fill lifecycle control")
        SCRATCH_PARENT.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="run-", dir=SCRATCH_PARENT))
        try:
            harness = directory / "net_arena_fill_lifetime.c"
            binary = directory / "net_arena_fill_lifetime"
            harness.write_text(_harness(), encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(harness), "-o", str(binary)],
                capture_output=True, text=True, timeout=30, check=False,
            )
            if result.returncode:
                self.fail(f"harness compile failed; retained {directory}: {result.stderr}")
            result = subprocess.run([str(binary)], capture_output=True, text=True,
                                    timeout=10, check=False)
            if result.returncode:
                self.fail(f"lifecycle control failed ({result.returncode}); retained {directory}")
        except Exception:
            raise
        else:
            shutil.rmtree(directory)


if __name__ == "__main__":
    unittest.main()
