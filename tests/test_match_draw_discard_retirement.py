"""Focused controls for the private match draw/discard retirement handoff."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class MatchDrawDiscardRetirementTests(unittest.TestCase):
    def test_actual_runtime_notification_and_export_controls(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test",
             str(ROOT / "tests/match_draw_discard_retirement.test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("tests 9", result.stdout)
        self.assertIn("fail 0", result.stdout)

    def test_extracted_native_status_and_retirement_controls(self):
        source = (ROOT / "src/gameplay_menu_browser.cpp").read_text()
        first = "const char* melee_web_native_menu_match_draw_discard_status(){"
        start = source.find(first)
        self.assertNotEqual(start, -1, "private status function is missing from native source")
        self.assertEqual(source.find(first, start + len(first)), -1,
                         "native source must contain one private status function")
        end = source.find("\n#endif", start)
        self.assertNotEqual(end, -1, "private status/retirement block must end at its compile guard")
        functions = source[start:end]
        self.assertEqual(functions.count("int melee_web_native_menu_match_draw_discard_retire(){"), 1,
                         "private retirement function is missing or duplicated")
        self.assertEqual(functions.count("const char* melee_web_native_menu_match_draw_discard_status(){"), 1)

        compiler_words = shlex.split(os.environ.get("CXX", "clang++"))
        if not compiler_words:
            self.fail("CXX is empty; a C++ compiler is required for the extracted native control")
        compiler = shutil.which(compiler_words[0])
        if compiler is None:
            self.skipTest(f"C++ compiler {compiler_words[0]!r} is unavailable")

        harness = r'''#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

std::string match_draw_discard_receipt, message;
bool match_draw_discard_terminal = true;
bool match_draw_discard_native_retired = false;
bool source_file_busy = false, throw_from_close = false;
std::vector<int> answers;
std::size_t answer_index = 0;
std::vector<std::string> calls;

int next_answer() {
    assert(answer_index < answers.size());
    return answers[answer_index++];
}
#define EM_ASM_INT(...) next_answer()
#define EM_ASM(...) calls.push_back("observer-retired")
void check(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}
int melee_web_source_file_drive_busy() { return source_file_busy; }
void close() {
    calls.push_back("close");
    if (throw_from_close) throw std::runtime_error("original close failed");
}
void melee_web_input_shutdown() { calls.push_back("input-shutdown"); }
void aurora_shutdown() { calls.push_back("aurora-shutdown"); }
void emscripten_cancel_main_loop() { calls.push_back("cancel-loop"); }

__ACTUAL_NATIVE_FUNCTIONS__

void reset(std::vector<int> native_answers) {
    match_draw_discard_receipt.clear();
    match_draw_discard_native_retired = false;
    answers = std::move(native_answers);
    answer_index = 0;
    calls.clear();
    source_file_busy = false;
    throw_from_close = false;
    message.clear();
}

int main() {
    reset({});
    assert(std::string(melee_web_native_menu_match_draw_discard_status()) ==
           "{\"native_retired\":false}");

    reset({0});
    assert(melee_web_native_menu_match_draw_discard_retire() == 0);
    assert(calls.empty() && !match_draw_discard_native_retired);

    reset({1, 0});
    assert(melee_web_native_menu_match_draw_discard_retire() == 0);
    assert(calls.empty() && !match_draw_discard_native_retired);

    reset({1, 1});
    source_file_busy = true;
    assert(melee_web_native_menu_match_draw_discard_retire() == 0);
    assert(calls.empty() && !match_draw_discard_native_retired);

    reset({1, 1});
    throw_from_close = true;
    assert(melee_web_native_menu_match_draw_discard_retire() == 0);
    assert((calls == std::vector<std::string>{"close"}));
    assert(!match_draw_discard_native_retired);
    assert(message == "original close failed");

    reset({1, 1, 1});
    match_draw_discard_receipt = "{\"source_frame_after\":202}";
    assert(std::string(melee_web_native_menu_match_draw_discard_status()) ==
           "{\"source_frame_after\":202,\"native_retired\":false}");
    assert(melee_web_native_menu_match_draw_discard_retire() == 1);
    assert(match_draw_discard_native_retired && answer_index == 3);
    assert((calls == std::vector<std::string>{"close", "input-shutdown",
           "aurora-shutdown", "cancel-loop", "observer-retired"}));
    assert(std::string(melee_web_native_menu_match_draw_discard_status()) ==
           "{\"source_frame_after\":202,\"native_retired\":true}");
    const auto successful_calls = calls.size();
    assert(melee_web_native_menu_match_draw_discard_retire() == 0);
    assert(calls.size() == successful_calls);

    reset({1, 1, 0});
    assert(melee_web_native_menu_match_draw_discard_retire() == 0);
    assert(match_draw_discard_native_retired);
    assert((calls == std::vector<std::string>{"close", "input-shutdown",
           "aurora-shutdown", "cancel-loop", "observer-retired"}));
    const auto refused_notification_calls = calls.size();
    assert(melee_web_native_menu_match_draw_discard_retire() == 0);
    assert(calls.size() == refused_notification_calls);
    assert(std::string(melee_web_native_menu_match_draw_discard_status()) ==
           "{\"native_retired\":true}");
}
'''.replace("__ACTUAL_NATIVE_FUNCTIONS__", functions)

        scratch = Path(tempfile.mkdtemp(prefix="melee-web-native-retirement-test-"))
        source_path = scratch / "native_retirement_control.cpp"
        binary_path = scratch / "native_retirement_control"
        source_path.write_text(harness)
        command = [compiler, *compiler_words[1:], "-std=c++20", "-Wall", "-Wextra",
                   str(source_path), "-o", str(binary_path)]
        try:
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0,
                             f"native control compile failed; reproducer retained at {scratch}\n"
                             f"{compiled.stdout}{compiled.stderr}")
            ran = subprocess.run([str(binary_path)], capture_output=True, text=True, timeout=20)
            self.assertEqual(ran.returncode, 0,
                             f"native control failed; reproducer retained at {scratch}\n"
                             f"{ran.stdout}{ran.stderr}")
        except BaseException:
            raise
        else:
            shutil.rmtree(scratch)


if __name__ == "__main__":
    unittest.main()
