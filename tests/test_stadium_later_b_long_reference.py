"""Retained actual long-B projection controls, no live/native observations."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(os.environ.get("MELEE_LONG_B_SUPPORT_ROOT",Path(__file__).resolve().parents[1]))
sys.path[:0]=[str(ROOT/"tools"),str(ROOT/"scripts"),str(ROOT/"reference-capture/dolphin")]


class LongBReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binding=os.environ.get("MELEE_LONG_B_BINDING")
        module=os.environ.get("MELEE_LONG_B_MODULE")
        if binding is None and module is None:
            raise unittest.SkipTest("optional retained long-B operands absent")
        if binding is None or module is None:
            raise ValueError("partial retained long-B fixture configuration")
        spec=importlib.util.spec_from_file_location("long_b_reference",module)
        cls.tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.tool)
        cls.binding=Path(binding)
        cls.actual=cls.tool.build_long_b_projection(cls.binding)
        cls.paths=cls.tool.verified_files(json.loads(cls.binding.read_text()))
        cls.rows=list(cls.tool.context.replay.iter_records(cls.paths["primary"]))

    def test_actual_reference_dimensions_and_scopes(self):
        value=self.actual
        self.assertEqual((value["recipe_frames"],value["recipe_bytes"],value["setup_global_cursor"]),(5422,262226,951))
        self.assertEqual(value["menu_prefix"]["pads_sha256"],"a10939bd0050f10af929b0d28cea52539aa248131ca66aa0778b5b0ceeded790")
        self.assertEqual((value["entry"]["rng"]["value_hex"],value["setup"]["rng"]["value_hex"],
                          value["go"]["rng"]["value_hex"]),("7fc17cf1","25d557a7","7fc9b4d0"))
        self.assertEqual(len(value["states"]),3501)
        self.assertIs(value["compact_rng"]["validated"]["valid"],True)
        report=json.loads(self.paths["report"].read_text())
        self.assertNotIn("valid",report["stadium_whole_long_idle_compact_rng"])
        actual={k:v for k,v in value["compact_rng"]["validated"].items() if k!="valid"}
        actual["output_path"]=str(self.paths["compact_rng"])
        self.assertEqual(actual,report["stadium_whole_long_idle_compact_rng"])
        self.assertEqual(value["compact_rng"]["validated"]["record_count"],91383)
        self.assertEqual(value["compact_rng"]["validated"]["unresolved_owner_events"],0)
        self.assertEqual(sum(x["events"] for x in value["compact_rng"]["semantic_owner_inventory"]),91383)
        for key in ("runtime_admission","browser_parity","per_owner_rng_parity","C3"):
            self.assertIs(value[key],False)

    def test_actual_per_action_rng_assignment_and_gap_refusal(self):
        value=self.actual["action_rng"]
        self.assertEqual((len(value["rows"]),value["rows"][0]["action_index"],value["rows"][-1]["action_index"]),(7000,168,7167))
        self.assertEqual(sum(row["event_count"] for row in value["rows"]),91383)
        self.assertEqual(value["rows"][0]["selected_event_count"],0)
        self.assertEqual(value["rows"][-1]["selected_event_count"],0)
        compact=self.tool.decode_live_compact(self.paths["compact_rng"].read_bytes(),
            capture_id=self.actual["whole_session_context"]["identity"]["capture_id"],
            sequence_id=self.actual["whole_session_context"]["identity"]["sequence_id"],
            dol_sha256=self.actual["whole_session_context"]["identity"]["dol_sha256"],
            primary_status=json.loads(self.paths["primary_status"].read_text()))
        bad=copy.deepcopy(compact)
        bad["records"][0]["observer_next_sequence"]=self.actual["actions"][168]["consume"]["sequence"]
        with self.assertRaisesRegex(ValueError,"outside actual action boundary"):
            self.tool.action_rng_projection(self.actual["actions"],bad)
        bad=copy.deepcopy(compact);bad["records"][0]["draw_ordinal"]+=1
        with self.assertRaisesRegex(ValueError,"action source coordinates"):
            self.tool.action_rng_projection(self.actual["actions"],bad)

    def test_actual_serialized_projection_admission_and_refusals(self):
        with tempfile.TemporaryDirectory(prefix="long-b-projection-admission-") as tmp:
            path=Path(tmp)/"projection.json"
            raw=self.tool.serialize_projection(self.actual);path.write_bytes(raw)
            receipt=self.tool.admit_serialized_projection(self.binding,path)
            import hashlib
            self.assertEqual(receipt,{"schema":"melee-web-later-b-serialized-projection-admission-v1",
                "source_binding":{"bytes":self.binding.stat().st_size,"sha256":hashlib.sha256(self.binding.read_bytes()).hexdigest()},
                "projection":{"bytes":len(raw),"sha256":hashlib.sha256(raw).hexdigest()},"runtime_admission":False})
            path.write_bytes(raw+b" ")
            with self.assertRaisesRegex(ValueError,"serialized projection differs"):
                self.tool.admit_serialized_projection(self.binding,path)
            path.write_bytes(raw)
            link=Path(tmp)/"link.json";link.symlink_to(path)
            with self.assertRaisesRegex(ValueError,"regular non-symlink"):
                self.tool.admit_serialized_projection(self.binding,link)

    def test_long_gate1_preserves_v3_projection_bytes(self):
        import hashlib
        self.assertEqual(hashlib.sha256(self.tool.serialize_projection(self.actual)).hexdigest(),"dead7b678716df87432febba4c8dfedf43acdb47f8d1254bec7b6970f00d3ba1")

    def test_long_gate1_expected_matches_actual_phase_and_prefix(self):
        value,recipe=self.tool.build_gate1_transport(self.binding)
        import hashlib
        self.assertEqual((len(recipe),hashlib.sha256(recipe).hexdigest()),(262226,self.tool.RECIPE_SHA256))
        self.assertEqual((value["schema"],value["observation_profile"],value["selected_occurrence"]),
            ("melee-web-stock4-none-later-sss-reference-expectation-v1",4,1))
        self.assertEqual((value["notification_cursor"],value["menu_session_ticks"],len(value["literal_inputs"])),(951,544,951))
        self.assertEqual(value["source"]["sequence"],4266)
        self.assertEqual(value["prepared_profile"]["entry"]["sequence"],4267)
        self.assertEqual(value["prepared_profile"]["setup"]["sequence"],4353)
        table=Path(os.environ.get("MELEE_LONG_B_CANONICAL_CONTEXTS",ROOT/"tools/stadium_later_reference_contexts.json"))
        canonical=[c for c in json.loads(table.read_text())["contexts"] if c["id"]==self.tool.REFERENCE_ID]
        self.assertEqual(len(canonical),1)
        self.assertEqual(value["reference_context"],canonical[0])
        self.assertNotIn("start_sha256",value["reference_context"])
        self.assertIn("start_sha256",self.actual["reference_descriptor"])
        self.assertEqual(value["reference_context"]["handoff_origin"],"current_sss_prepared_later_951")
        self.assertEqual(value["preceding_pause_post_tick_clock"],self.actual["preceding_pause_post_tick_clock"])
        self.assertIs(value["runtime_admission"],False)
        self.assertEqual(value["literal_inputs"],[{"index":x["index"],"scene":x["scene_code"],"pads":x["pads"]}
            for x in self.actual["menu_prefix"]["inputs"]])

    def test_long_gate1_transport_exports_exact_recipe_and_refuses_existing_output(self):
        with tempfile.TemporaryDirectory(prefix="long-b-gate1-transport-") as tmp:
            directory=Path(tmp)/"packet"
            receipt=self.tool.export_gate1_packet(self.binding,directory)
            import hashlib
            self.assertEqual(receipt["recipe"]["sha256"],self.tool.RECIPE_SHA256)
            self.assertEqual(hashlib.sha256((directory/"recipe.mwrc").read_bytes()).hexdigest(),self.tool.RECIPE_SHA256)
            expected=(directory/"expected.json").read_bytes()
            self.assertEqual((len(expected),hashlib.sha256(expected).hexdigest()),
                             (receipt["expected"]["bytes"],receipt["expected"]["sha256"]))
            self.assertEqual(json.loads((directory/"transport.json").read_text()),receipt)
            self.assertIs(receipt["runtime_admission"],False)
            before={q.name:q.read_bytes() for q in directory.iterdir()}
            with self.assertRaisesRegex(ValueError,"transport output must be absent"):
                self.tool.export_gate1_packet(self.binding,directory)
            self.assertEqual(before,{q.name:q.read_bytes() for q in directory.iterdir()})

    def test_actual_variable_action_order_and_terminal(self):
        actions=self.actual["actions"]
        self.assertEqual((len(actions),sum(x["kind"]=="tick" for x in actions),sum(x["kind"]=="draw" for x in actions)),(7168,3585,3583))
        self.assertEqual([i for i in range(1,len(actions)) if actions[i-1]["kind"]==actions[i]["kind"]=="tick"],[4929,6930])
        self.assertEqual(self.actual["natural_terminal"]["first_later_natural_draw_return"],
                         {"sequence":22274,"source_tick":3585,"draw_ordinal":3582})
        self.assertNotIn("stop_before_consume",self.actual)

    def test_wrong_four_port_operand_refuses(self):
        frames=copy.deepcopy(self.actual["whole_session_context"]["inputs"])
        frames[951]["pads"][0]="01"+frames[951]["pads"][0][2:]
        with self.assertRaisesRegex(ValueError,"action input escaped"):
            self.tool.source_actions(self.rows,self.actual["setup"]["sequence"],22274,frames)

    def test_missing_draw_and_reordered_consume_refuse(self):
        draw_seq=self.actual["actions"][1]["draw_enter"]["sequence"]
        rows=[r for r in self.rows if r["seq"]!=draw_seq]
        frames=self.actual["whole_session_context"]["inputs"]
        with self.assertRaisesRegex(ValueError,"unpaired DrawReturn"):
            self.tool.source_actions(rows,self.actual["setup"]["sequence"],22274,frames)
        rows=list(self.rows)
        consume=self.actual["actions"][0]["consume"]["sequence"]
        tick=self.actual["actions"][0]["source"]["sequence"]
        rows[consume],rows[tick]=rows[tick],rows[consume]
        with self.assertRaisesRegex(ValueError,"source tick/consume"):
            self.tool.source_actions(rows,self.actual["setup"]["sequence"],22274,frames)

    def test_wrong_schema_role_and_changed_compact_pin_refuse(self):
        binding=json.loads(self.binding.read_text())
        wrong=copy.deepcopy(binding);wrong["schema"]=self.tool.reference.SCHEMA
        with self.assertRaisesRegex(ValueError,"unknown long-reference"):
            self.tool.verified_files(wrong)
        wrong=copy.deepcopy(binding);wrong["files"]["extra"]=wrong["files"]["primary"]
        with self.assertRaisesRegex(ValueError,"unknown long-reference"):
            self.tool.verified_files(wrong)
        wrong=copy.deepcopy(binding);wrong["files"]["compact_rng"]["sha256"]="0"*64
        with self.assertRaisesRegex(ValueError,"changed compact"):
            self.tool.verified_files(wrong)

    def test_mixed_actual_compact_role_refuses_before_projection(self):
        binding=json.loads(self.binding.read_text())
        binding["files"]["compact_rng"]=copy.deepcopy(binding["files"]["primary"])
        with tempfile.TemporaryDirectory(prefix="long-b-binding-negative-") as tmp:
            path=Path(tmp)/"binding.json";path.write_text(json.dumps(binding))
            with self.assertRaisesRegex(ValueError,"long profile is not a fresh selected-B record"):
                self.tool.build_long_b_projection(path)
