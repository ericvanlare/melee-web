"""Exercise generated source ownership against actual local Git checkouts."""
from pathlib import Path
import subprocess
import sys
import tempfile
import shutil
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_sources import prepare_sources
from gameplay_bool import replace_bool_tokens, transform_source


def git(path, *args):
    return subprocess.check_output(["git", *args], cwd=path, text=True, stderr=subprocess.PIPE).strip()


class GameplaySourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="melee generated source ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.original = self.root / ".deps/melee"
        (self.original / "src").mkdir(parents=True)
        (self.original / "src/fixture.c").write_text("int value = 1;\n")
        git(self.original, "init", "--quiet")
        git(self.original, "add", ".")
        git(self.original, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
            "commit", "--quiet", "-m", "Authored source")
        self.lock = {"repositories": {"melee": {"commit": git(self.original, "rev-parse", "HEAD")}}}
        (self.root / "patches").mkdir()
        # Dependency verification itself has real-repository tests already.
        # This fixture isolates source preparation without downloading an SDK.
        self.verification = patch("gameplay_sources.verify_sources")
        self.verification.start()
        self.addCleanup(self.verification.stop)
        self.write_patch(2)

    def write_patch(self, value):
        file = self.original / "src/fixture.c"
        file.write_text(f"int value = {value};\n")
        (self.root / "patches/melee-gameplay.patch").write_text(git(self.original, "diff", "--binary") + "\n")
        file.write_text("int value = 1;\n")

    def add_boolean_fixture(self):
        files = {
            "src/Runtime/platform.h": "#ifndef RUNTIME_PLATFORM_H\n#define RUNTIME_PLATFORM_H\n#include <stdbool.h>\ntypedef bool (*Predicate)(void);\n#endif\n",
            "src/melee/flags.h": "#include <Runtime/platform.h>\nstruct MarioLike { int a,b; bool tornado,cape; void* object; };\nstruct Capsule { bool a,b; };\nbool source_seven(void);\n",
            "src/melee/flags.c": "#include <melee/flags.h>\nbool source_seven(void) { bool value = 7; return value; }\n",
            "src/sysdolphin/native.h": "#include <Runtime/platform.h>\ntypedef void (*SourceCallback)(bool);\n",
            "src/MSL/stdbool.h": "typedef int bool;\n",
            "extern/dolphin/sdk.h": "#include <stdbool.h>\nstruct SDKFlags { bool one,two; };\n",
        }
        for relative, text in files.items():
            path = self.original / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        git(self.original, "add", ".")
        git(self.original, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "--quiet", "-m", "Boolean source fixture")
        self.lock["repositories"]["melee"]["commit"] = git(self.original, "rev-parse", "HEAD")
        return files

    def test_token_migration_preserves_comments_literals_and_sdk(self):
        sample = r'bool bool_name; /* bool */ const char* s = "bool \" bool"; // bool' + "\n" + "char c = 'b'; bool x; 0bool; " + r'R"tag(bool " bool)tag";' + "\n"
        expected = sample.replace('bool bool_name', 'melee_source_bool bool_name').replace('bool x;', 'melee_source_bool x;')
        self.assertEqual(replace_bool_tokens(sample), expected)
        self.assertEqual(replace_bool_tokens(expected), expected)
        continued = '// bool\\\n bool\n/\\\n* bool */ bo\\\nol value;\n'
        self.assertEqual(replace_bool_tokens(continued), '// bool\\\n bool\n/\\\n* bool */ melee_source_bool\\\n value;\n')
        self.assertEqual(transform_source("src/MSL/stdbool.h", "typedef int bool;"), "typedef int bool;")
        self.assertEqual(transform_source("extern/dolphin/sdk.h", "bool value;"), "bool value;")

    def test_generated_c_and_cpp_share_source_integer_bool_without_sdk_pollution(self):
        originals = self.add_boolean_fixture()
        source = prepare_sources(self.root, self.lock)
        for relative, text in originals.items():
            self.assertEqual((self.original/relative).read_text(), text)
        runtime = (source/"Runtime/platform.h").read_text()
        self.assertIn("typedef int melee_source_bool;", runtime)
        self.assertIn("typedef melee_source_bool (*Predicate)(void);", runtime)
        self.assertEqual((source/"MSL/stdbool.h").read_text(), originals["src/MSL/stdbool.h"])
        self.assertEqual((source.parent/"extern/dolphin/sdk.h").read_text(), originals["extern/dolphin/sdk.h"])
        for name in ["Runtime/platform.h", "melee/flags.h", "melee/flags.c", "sysdolphin/native.h"]:
            before = (source/name).stat().st_mtime_ns
            prepare_sources(self.root,self.lock)
            self.assertEqual((source/name).stat().st_mtime_ns,before)
        c = shutil.which("clang") or shutil.which("cc")
        cpp = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(c);self.assertIsNotNone(cpp)
        output = self.root/"bool.o"
        subprocess.run([c,"-std=c11","-Wall","-Wextra","-Werror","-I",str(source),"-c",str(source/"melee/flags.c"),"-o",str(output)],check=True)
        test = self.root/"bool.cpp"
        test.write_text('#include <vector>\n#include <type_traits>\n#include <cstddef>\nextern "C" {\n#include <melee/flags.h>\n#include <sysdolphin/native.h>\n#include "sdk.h"\n}\nstatic_assert(sizeof(bool)==1);\nstatic_assert(sizeof(melee_source_bool)==4);\nstatic_assert(sizeof(SDKFlags)==2);\nstatic_assert(sizeof(Capsule)==8);\nstatic_assert(offsetof(MarioLike,cape)==12);\nstatic_assert(std::is_same_v<Predicate,int(*)(void)>);\nstatic_assert(std::is_same_v<SourceCallback,void(*)(int)>);\nint main(){std::vector<bool> v{true}; Predicate p=source_seven; return v[0] && p()==7 ? 0:1;}\n')
        binary=self.root/"bool_check"
        subprocess.run([cpp,"-std=c++20","-Wall","-Wextra","-Werror","-I",str(source),"-I",str(source.parent/"extern/dolphin"),str(test),str(output),"-o",str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

    def test_cached_reviewed_patch_transitions_to_composed_patch(self):
        self.add_boolean_fixture()
        with patch("gameplay_sources.composed_patch", side_effect=lambda repo, reviewed: reviewed):
            source=prepare_sources(self.root,self.lock)
        self.assertIn("bool tornado",(source/"melee/flags.h").read_text())
        prepare_sources(self.root,self.lock)
        self.assertIn("melee_source_bool tornado",(source/"melee/flags.h").read_text())
        self.assertEqual(git(self.original,"status","--porcelain"),"")

    def test_unknown_changes_and_staged_files_reject_after_migration(self):
        self.add_boolean_fixture();source=prepare_sources(self.root,self.lock)
        path=source/"melee/flags.h";expected=path.read_text();path.write_text(expected+"// user edit\n")
        with self.assertRaisesRegex(ValueError,"changes differ"):
            prepare_sources(self.root,self.lock)
        self.assertTrue(path.read_text().endswith("// user edit\n"))
        path.write_text(expected);git(source.parent,"add","src/melee/flags.h")
        with self.assertRaisesRegex(ValueError,"staged changes"):
            prepare_sources(self.root,self.lock)

    def test_pristine_dependency_idempotence_and_reviewed_patch_update(self):
        source = prepare_sources(self.root, self.lock)
        self.assertEqual((self.original / "src/fixture.c").read_text(), "int value = 1;\n")
        self.assertEqual((source / "fixture.c").read_text(), "int value = 2;\n")
        mtime = (source / "fixture.c").stat().st_mtime_ns
        self.assertEqual(prepare_sources(self.root, self.lock), source)
        self.assertEqual((source / "fixture.c").stat().st_mtime_ns, mtime)
        self.write_patch(4)
        prepare_sources(self.root, self.lock)
        self.assertEqual((source / "fixture.c").read_text(), "int value = 4;\n")
        self.assertEqual(git(self.original, "status", "--porcelain"), "")
        self.assertEqual(git(source.parent, "diff", "--cached"), "")

    def test_patch_update_preserves_unmodified_transformed_source_mtimes(self):
        fixture = self.original / "src/melee/steady.c"
        fixture.parent.mkdir(parents=True)
        fixture.write_text("bool stable(bool input) { return input; }\n")
        git(self.original, "add", ".")
        git(self.original, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
            "commit", "--quiet", "-m", "Stable original source")
        self.lock["repositories"]["melee"]["commit"] = git(self.original, "rev-parse", "HEAD")
        source = prepare_sources(self.root, self.lock)
        normalized = source / "melee/steady.c"
        before = normalized.stat().st_mtime_ns
        self.assertIn("melee_source_bool", normalized.read_text())
        self.write_patch(4)
        prepare_sources(self.root, self.lock)
        self.assertEqual(normalized.stat().st_mtime_ns, before)
        self.assertEqual((source / "fixture.c").read_text(), "int value = 4;\n")

    def test_unexplained_generated_edits_are_never_overwritten(self):
        source = prepare_sources(self.root, self.lock)
        (source / "fixture.c").write_text("int value = 99;\n")
        self.write_patch(4)
        with self.assertRaisesRegex(ValueError, "changes differ"):
            prepare_sources(self.root, self.lock)
        self.assertEqual((source / "fixture.c").read_text(), "int value = 99;\n")

    def test_invalid_new_patch_keeps_previous_source(self):
        source = prepare_sources(self.root, self.lock)
        patch_file = self.root / "patches/melee-gameplay.patch"
        patch_file.write_text(patch_file.read_text().replace("-int value = 1;", "-int value = 77;"))
        with self.assertRaises(subprocess.CalledProcessError):
            prepare_sources(self.root, self.lock)
        self.assertEqual((source / "fixture.c").read_text(), "int value = 2;\n")


if __name__ == "__main__":
    unittest.main()
