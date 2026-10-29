"""Native later-SSS identity controls; endpoint leaf is modeled only."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import unittest

SOURCE_ROOT = Path(os.environ.get(
    "MELEE_STADIUM_GATE1_SOURCE_ROOT", Path(__file__).resolve().parents[1]
)).resolve()
SUPPORT_ROOT = Path(os.environ.get(
    "MELEE_STADIUM_GATE1_SUPPORT_ROOT", SOURCE_ROOT
)).resolve()
SHA_HEADER = Path(os.environ.get(
    "MELEE_STADIUM_GATE1_SHA_HEADER", SUPPORT_ROOT / "src/private_sha256.hpp"
)).resolve()

# The focused compiler helper and owned scratch helper live in the checkout when
# this candidate is run from its external review directory.
for _path in (SUPPORT_ROOT / "tests", SUPPORT_ROOT / "scripts",
              SUPPORT_ROOT / "tools", SUPPORT_ROOT / "reference-capture/dolphin"):
    sys.path.insert(0, str(_path))

from owned_test_workspace import OwnedWorkspaceTests
from test_stadium_current_sss_exit import run as run_cpp_fixture


def cut(text: str, start: str, end: str) -> str:
    if text.count(start) != 1 or text.count(end) != 1:
        raise AssertionError(f"expected one source boundary for {start!r} and {end!r}")
    return text[text.index(start):text.index(end)]


class LaterReferenceIdentityControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        keys = ("MELEE_LATER_B_MODULE", "MELEE_LATER_B_BINDING")
        present = [key in os.environ for key in keys]
        if not any(present):
            raise unittest.SkipTest("optional retained original-B binding is not configured")
        if not all(present) or not all(os.environ[key] for key in keys):
            raise ValueError("partial retained original-B fixture configuration refuses")

        module_path = Path(os.environ["MELEE_LATER_B_MODULE"]).resolve()
        spec = importlib.util.spec_from_file_location("later_b_reference_identity_operand", module_path)
        if spec is None or spec.loader is None:
            raise ValueError("retained original-B operand module cannot be loaded")
        cls.operand = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.operand)

        cls.binding = json.loads(Path(os.environ["MELEE_LATER_B_BINDING"]).read_text())
        cls.paths = cls.operand.verified_files(cls.binding)
        cls.rows = list(cls.operand.context.replay.iter_records(cls.paths["primary"]))
        cls.launch = json.loads(cls.paths["launch"].read_text())
        cls.report = json.loads(cls.paths["report"].read_text())

    def test_actual_native_descriptor_seal_and_profile_arm_controls(self):
        source = SOURCE_ROOT / "src/gameplay_menu_browser.cpp"
        native = source.read_text()
        match_source = (SOURCE_ROOT / "src/gameplay_menu_browser_match.cpp").read_text()
        contexts = json.loads((SOURCE_ROOT / "tools/stadium_later_reference_contexts.json").read_text())
        observed = next(row for row in contexts["contexts"] if row["profile"] == 3)
        legacy = next(row for row in contexts["contexts"] if row["profile"] == 2)

        # Regenerate the strict v8 recipe from the retained raw source records.
        # This produces reference operands only; it does not admit or run a game.
        retained_operands = self.operand.load(self.binding)
        self.assertIs(retained_operands["runtime_admission"], False)
        self.assertIs(retained_operands["same_run_provenance_verified"], False)
        capture = self.operand.context.replay.capture_from_records(self.rows)
        recipe_bytes, _ = self.operand.context.replay.encode_v8(capture)
        self.assertEqual(hashlib.sha256(recipe_bytes).hexdigest(), observed["recipe_sha256"])
        self.assertEqual(len(recipe_bytes), observed["recipe_bytes"])
        expected_spans = [
            {"scene": scene, "first_frame": first, "last_frame": last}
            for scene, first, last in zip(contexts["scenes"],
                                           observed["first"], observed["last"])
        ]
        self.assertEqual(capture["spans"], expected_spans)

        legacy_spans = [
            {"scene": scene, "first_frame": first, "last_frame": last}
            for scene, first, last in zip(contexts["scenes"], legacy["first"], legacy["last"])
        ]
        workspace = self.new_workspace(SUPPORT_ROOT, "stadium-later-reference-identity-")
        exact_path = workspace / "retained-new-reference.mwrc"
        changed_path = workspace / "one-pad-byte-changed.mwrc"
        observed_topology_path = workspace / "observed-topology.txt"
        legacy_topology_path = workspace / "legacy-topology.txt"
        exact_path.write_bytes(recipe_bytes)
        input_offset = (
            self.operand.context.replay.HEADER.size
            + self.operand.context.replay.CONTEXT_HEADER.size
            + self.operand.context.replay.CONTEXT_BYTES
            + self.operand.context.replay.GAME_INFO_SIZE
            + self.operand.context.replay.PAD_STATE_BYTES
        )
        changed = bytearray(recipe_bytes)
        self.assertLess(input_offset, len(changed))
        changed[input_offset] ^= 1
        self.assertNotEqual(changed, recipe_bytes)
        changed_path.write_bytes(changed)

        def write_topology(path: Path, frame_count: int, spans: list[dict]) -> None:
            rows = [f"{frame_count} {len(spans)}"]
            rows.extend(f"{row['scene']} {row['first_frame']} {row['last_frame']}" for row in spans)
            path.write_text("\n".join(rows) + "\n")

        write_topology(observed_topology_path, len(capture["frames"]), capture["spans"])
        write_topology(legacy_topology_path, legacy["frames"], legacy_spans)

        cpp = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>
#include "@SHA_HEADER@"
#include "@DESCRIPTOR_HEADER@"

enum class CurrentSssExitProfile : unsigned { Time120 = 0, Stock4None = 1 };
struct Span { unsigned scene=0, first_frame=0, last_frame=0; };
struct Recipe {
 unsigned version=8;
 std::vector<uint8_t> frames;
 std::vector<Span> spans;
 bool diagnostic_entity_prefix=false;
 bool whole_session() const { return true; }
};
namespace melee_web { using RetailReplayRecipe = ::Recipe; }
std::unique_ptr<melee_web::RetailReplayRecipe> replay;
struct LaterSssContinuation {
 const melee_web::stadium_later_reference::Context* reference=
     &melee_web::stadium_later_reference::legacy;
 bool armed=false;
 size_t pause_next_cursor=450;
} later_sss;
unsigned completed_matches=0;
std::string message;
int leaf_calls=0;
unsigned leaf_profile=99;
bool leaf_observed_reference=false;
int current_sss_exit_arm(CurrentSssExitProfile profile, bool observed_reference=false) {
 ++leaf_calls;
 leaf_profile=static_cast<unsigned>(profile);
 leaf_observed_reference=observed_reference;
 return 1; // Endpoint model records invocation only; no world/source success.
}
void check(bool value, const char* error) {
 if (!value) throw std::runtime_error(error);
}

@SEAL_AND_BIND@
@LEGACY_RECIPE_GUARD@
@PROFILE_ARM@

std::vector<uint8_t> read_bytes(const std::string& path) {
 std::ifstream in(path, std::ios::binary);
 assert(in.good());
 return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}
std::unique_ptr<Recipe> read_recipe(const std::string& path) {
 std::ifstream in(path);
 assert(in.good());
 unsigned frame_count=0, span_count=0;
 in >> frame_count >> span_count;
 auto result=std::make_unique<Recipe>();
 result->frames.resize(frame_count);
 for(unsigned i=0;i<span_count;++i) {
  Span span;
  in >> span.scene >> span.first_frame >> span.last_frame;
  result->spans.push_back(span);
 }
 assert(in.good() || in.eof());
 return result;
}
void reset(const std::string& topology) {
 replay=read_recipe(topology);
 later_sss=LaterSssContinuation{};
 later_sss_reference_seal={};
 completed_matches=0;
 message.clear();
 leaf_calls=0;
 leaf_profile=99;
 leaf_observed_reference=false;
}
void expect_profile3_refusal() {
 const int before=leaf_calls;
 assert(melee_web_native_menu_stadium_current_sss_exit_arm_profile(3)==0);
 assert(leaf_calls==before && !later_sss.armed);
}
int main() {
 const std::string exact_topology=@EXACT_TOPOLOGY@;
 const std::string legacy_topology=@LEGACY_TOPOLOGY@;
 const std::vector<uint8_t> exact=read_bytes(@EXACT_BYTES@);
 const std::vector<uint8_t> changed=read_bytes(@CHANGED_BYTES@);
 assert(exact.size()==melee_web::stadium_later_reference::observed_b.recipe_bytes);
 assert(changed.size()==exact.size());

 // Exact regenerated bytes bind to the actual parsed owner and reach the
 // profile-3 leaf only with the observed stock4/None profile and flag.
 reset(exact_topology);
 later_sss_bind_parsed_reference(exact.data(), static_cast<unsigned>(exact.size()));
 assert(later_sss_reference_seal.owner==replay.get());
 assert(later_sss_reference_seal.bytes==melee_web::stadium_later_reference::observed_b.recipe_bytes);
 assert(later_sss_reference_seal.sha256==melee_web::stadium_later_reference::observed_b.recipe_sha256);
 assert(melee_web_native_menu_stadium_current_sss_exit_arm_profile(3)==1);
 assert(leaf_calls==1 && leaf_profile==static_cast<unsigned>(CurrentSssExitProfile::Stock4None));
 assert(leaf_observed_reference && later_sss.armed);
 assert(later_sss.reference==&melee_web::stadium_later_reference::observed_b);
 assert(later_sss.pause_next_cursor==melee_web::stadium_later_reference::observed_b.pause_first);
 assert(later_sss_reference_bound());

 // One actual source-consumed PAD byte changed: the exact-size parse may bind,
 // but its digest cannot pass the immutable descriptor before the leaf.
 reset(exact_topology);
 later_sss_bind_parsed_reference(changed.data(), static_cast<unsigned>(changed.size()));
 assert(later_sss_reference_seal.owner==replay.get());
 assert(later_sss_reference_seal.bytes==exact.size());
 expect_profile3_refusal();

 // Wrong-sized parse clears the seal and refuses before the modeled leaf.
 reset(exact_topology);
 later_sss_bind_parsed_reference(exact.data(), static_cast<unsigned>(exact.size()-1));
 assert(later_sss_reference_seal.owner==nullptr && later_sss_reference_seal.bytes==0);
 expect_profile3_refusal();

 // A replacement parsed owner is foreign until the actual bind entry is called
 // again for that new owner. The bind entry also clears an old seal before a
 // fresh parse can bind the new bytes.
 reset(exact_topology);
 later_sss_bind_parsed_reference(exact.data(), static_cast<unsigned>(exact.size()));
 replay=read_recipe(exact_topology);
 expect_profile3_refusal();
 later_sss_bind_parsed_reference(nullptr, 0);
 assert(later_sss_reference_seal.owner==nullptr && later_sss_reference_seal.bytes==0);
 expect_profile3_refusal();
 later_sss_bind_parsed_reference(exact.data(), static_cast<unsigned>(exact.size()));
 assert(melee_web_native_menu_stadium_current_sss_exit_arm_profile(3)==1);
 assert(leaf_calls==1 && leaf_observed_reference);

 // Mixed new topology refuses before the modeled source-arm leaf.
 reset(exact_topology);
 later_sss_bind_parsed_reference(exact.data(), static_cast<unsigned>(exact.size()));
 assert(replay->spans.size()>6);
 --replay->spans[6].first_frame;
 expect_profile3_refusal();

 // Stale seal size/digest identities fail profile-3 admission. A copied
 // Context pointer fails the internal canonical-descriptor predicate; profile 3
 // itself selects immutable observed_b and accepts no caller descriptor.
 reset(exact_topology);
 later_sss_bind_parsed_reference(exact.data(), static_cast<unsigned>(exact.size()));
 later_sss_reference_seal.bytes=melee_web::stadium_later_reference::legacy.recipe_bytes;
 expect_profile3_refusal();
 reset(exact_topology);
 later_sss_bind_parsed_reference(exact.data(), static_cast<unsigned>(exact.size()));
 later_sss_reference_seal.sha256=melee_web::stadium_later_reference::legacy.recipe_sha256;
 expect_profile3_refusal();
 reset(exact_topology);
 later_sss_bind_parsed_reference(exact.data(), static_cast<unsigned>(exact.size()));
 melee_web::stadium_later_reference::Context copied=melee_web::stadium_later_reference::observed_b;
 later_sss.reference=&copied;
 assert(!later_sss_reference_bound());
 assert(leaf_calls==0);

 // Legacy profile 2 keeps its original descriptor and does not claim an
 // observed reference. Public/profile 1 still reaches the same modeled leaf
 // with the default observed_reference=false argument.
 reset(legacy_topology);
 assert(melee_web_native_menu_stadium_current_sss_exit_arm_profile(2)==1);
 assert(leaf_calls==1 && leaf_profile==static_cast<unsigned>(CurrentSssExitProfile::Stock4None));
 assert(!leaf_observed_reference && later_sss.armed);
 assert(later_sss.reference==&melee_web::stadium_later_reference::legacy);
 reset(legacy_topology);
 assert(melee_web_native_menu_stadium_current_sss_exit_arm_profile(1)==1);
 assert(leaf_calls==1 && leaf_profile==static_cast<unsigned>(CurrentSssExitProfile::Stock4None));
 assert(!leaf_observed_reference && !later_sss.armed);
}
'''
        cpp = (cpp.replace("@SHA_HEADER@", str(SHA_HEADER))
                  .replace("@DESCRIPTOR_HEADER@", str(SOURCE_ROOT / "src/stadium_later_reference.hpp"))
                  .replace("@SEAL_AND_BIND@", cut(
                      native, "struct LaterSssReferenceSeal {",
                      "bool later_sss_notification_order_owned("))
                  .replace("@LEGACY_RECIPE_GUARD@", cut(
                      native,
                      "bool later_sss_recipe_valid(const melee_web::RetailReplayRecipe& recipe){",
                      "bool later_sss_pause_pads("))
                  .replace("@PROFILE_ARM@", cut(
                      native,
                      "int melee_web_native_menu_stadium_current_sss_exit_arm_profile(unsigned profile){",
                      "const char* melee_web_native_menu_stadium_current_sss_exit_observe(")))

        # The parser commit must call the actual seal binder only after the new
        # parsed recipe becomes the current owner.
        parse_commit = cut(
            match_source, "int melee_web_native_menu_replay(",
            "#if defined(MELEE_WEB_NET_SESSION)",
        )
        self.assertLess(parse_commit.index("replay=std::move(candidate);"),
                        parse_commit.index("later_sss_bind_parsed_reference(data,size);"))

        cpp = (cpp.replace("@EXACT_TOPOLOGY@", json.dumps(str(observed_topology_path)))
                  .replace("@LEGACY_TOPOLOGY@", json.dumps(str(legacy_topology_path)))
                  .replace("@EXACT_BYTES@", json.dumps(str(exact_path)))
                  .replace("@CHANGED_BYTES@", json.dumps(str(changed_path))))
        run_cpp_fixture(cpp, workspace)
