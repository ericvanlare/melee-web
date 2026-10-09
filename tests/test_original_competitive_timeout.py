"""Portable policy controls. All later gameplay/terminal rows are synthetic.

Existing retained Items/CSS profile-entry fixtures remain covered separately;
these controls do not claim a native timeout run or native compilation.
"""
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

from authored_sd_reference_plan import make_input_plan
from original_competitive_timeout import (CAPS, POLICY_SHA256, TimeoutInventory,
    canonical, live_fighters, load_policy, policy, serialized_budget)
from ordinary_timeout_receiver import OrdinaryTimeoutReceiver
from sd_reference_diagnostic import SdDiagnosticError, PCS, slices
from reference_versus_sequence_capture import raw_pad
from capture_sd_reference_prefix import BoundedLog, BoundedIntentController


def bank(direction=False):
    return b"".join(bytes.fromhex(p)+b"\0" for p in
                    [raw_pad(x=-80) if direction else raw_pad(),raw_pad(),"00"*10+"ff","00"*10+"ff"])


def live(stocks=None):
    return dict(stocks=stocks or [4,4],damage_bits=["00000000"]*2,
                x_bits=["c2700000","42700000"])


def clock(frame):
    return frame,max(0,480-(frame+59)//60),(frame+59)%60


def ready():
    s=TimeoutInventory()
    for frame in range(181):
        s.input(bank());s.tick(s.ticks,clock(frame),live())
    return s


def loss():
    s=ready();s.input(bank(True));s.tick(s.ticks,clock(181),live([3,4]))
    return s


def result():
    b=bytearray(0x448);b[4:7]=bytes((1,1,0));b[8:12]=(28800).to_bytes(4,"big")
    b[13]=1;b[16]=1
    for slot,base in enumerate((0x58,0x100)):
        b[base+1]=8;b[base+3]=(1,0)[slot]<<2;b[base+5]=(1,0)[slot];b[base+8]=(3,4)[slot]
    for slot in range(2,6):b[0x58+slot*0xa8]=3
    return bytes(b)


def live_slices(stocks=None):
    fields=[]
    for slot in range(2):
        p=0x81000000+slot*0x10000;g=0x80f00000+slot*0x100
        head=bytearray(256);head[12]=slot
        head[0xb0:0xb4]=bytes.fromhex(live()["x_bits"][slot])
        for tag,address,value in ((52,0x80453080+slot*0xe90+0xb0,g.to_bytes(4,"big")+b"\0"*4),
                (53,g+0x2c,p.to_bytes(4,"big")),(5,p,head),(7,p+0x1830,b"\0"*4),
                (8,0x80453080+slot*0xe90+0x8e,bytes([(stocks or [4,4])[slot]]))):
            fields.append(dict(tag=tag,flags=slot,address=address,hex=value.hex()))
    return fields


class OriginalTimeoutTests(unittest.TestCase):
    def test_separate_hash_entry_only_provenance_and_budget(self):
        self.assertEqual(POLICY_SHA256,hashlib.sha256(canonical(policy())).hexdigest())
        self.assertEqual(len(make_input_plan(6)["frames"]),4323)
        self.assertEqual(policy()["entry_recipe_sha256"],make_input_plan(6)["authored_recipe_sha256"])
        b=serialized_budget()
        self.assertEqual((b["observer_records"],b["observer_bytes"]),(73456,126902592))
        self.assertLess(b["observer_bytes"],CAPS["observer_bytes"])
        self.assertEqual(b["native_mwri_bytes"],67108856)
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"policy.json";p.write_bytes(canonical(policy()))
            self.assertEqual(load_policy(p)[1],POLICY_SHA256)
            p.write_bytes(canonical(dict(policy(),timeout_frames=3600)))
            with self.assertRaises(ValueError):load_policy(p)

    def test_full_declared_synthetic_natural_timeout_and_ordered_retirement(self):
        s=loss()
        # Explicit synthetic held-bank drain, then source-observed neutral.
        s.input(bank(True));s.tick(s.ticks,clock(182),live([3,4]))
        for frame in range(183,28801):
            s.input(bank());s.tick(s.ticks,clock(frame),live([3,4]))
        self.assertEqual(s.directional,2)
        s.exit(s.ticks,clock(28800),live([3,4]),result());s.retire()
        self.assertEqual(s.phase,"retired")
        for action in (lambda:s.input(bank()),lambda:s.tick(s.ticks,clock(28800),live([3,4])),s.retire):
            with self.assertRaises(ValueError):action()

    def test_ready_loss_drain_and_never_restart_negatives(self):
        for name in ("early_direction","early_loss","wrong_position","neutral_before_loss","preloss_restart",
                     "restart","second_loss","p2loss","damage","unknownbank","first_loss_cap"):
            with self.subTest(name=name),self.assertRaises(ValueError):
                s=ready()
                if name=="early_direction":TimeoutInventory().input(bank(True))
                elif name=="early_loss":
                    t=TimeoutInventory();t.input(bank());t.tick(0,clock(0),live([3,4]))
                elif name=="wrong_position":
                    t=TimeoutInventory();t.frame=179;t.input(bank());v=live();v["x_bits"][0]="00000000"
                    t.tick(0,clock(180),v)
                elif name=="neutral_before_loss":
                    s.input(bank(True));s.input(bank());s.tick(s.ticks,clock(181),live([3,4]))
                elif name=="preloss_restart":s.input(bank(True));s.input(bank());s.input(bank(True))
                elif name=="restart":s=loss();s.input(bank());s.input(bank(True))
                elif name in ("second_loss","p2loss","damage"):
                    s=loss();s.input(bank());v=live([3,4])
                    if name=="second_loss":v["stocks"]=[2,4]
                    elif name=="p2loss":v["stocks"]=[3,3]
                    else:v["damage_bits"][0]="3f800000"
                    s.tick(s.ticks,clock(182),v)
                elif name=="unknownbank":s.input(bank(True)[:12]+b"\1"+bank()[13:])
                else:
                    for _ in range(601):s.input(bank(True))

    def test_independent_input_iteration_clock_and_caps(self):
        for name in ("gap","repeat","framegap","timer","subframe","noinput","ticks_cap","pad_cap"):
            with self.subTest(name=name),self.assertRaises(ValueError):
                s=ready();s.input(bank())
                if name=="gap":s.tick(s.ticks+1,clock(181),live())
                elif name=="repeat":s.tick(s.ticks-1,clock(181),live())
                elif name=="framegap":s.tick(s.ticks,clock(182),live())
                elif name=="timer":s.tick(s.ticks,(181,480,0),live())
                elif name=="subframe":s.tick(s.ticks,(181,476,1),live())
                elif name=="noinput":s.last_consumed=s.active_samples;s.tick(s.ticks,clock(181),live())
                elif name=="ticks_cap":s.ticks=CAPS["scheduled_records"];s.tick(s.ticks,clock(181),live())
                else:s.active_samples=CAPS["total_source_samples"];s.input(bank())
        s=TimeoutInventory();s.input(bank())
        with self.assertRaisesRegex(ValueError,"frame zero"):s.tick(0,clock(1),live())

    def test_actual_sd_first_and_terminal_clock_field_binding(self):
        # Exact fields projected from retained original SD V6 MWRO SHA65da3d89.
        # This establishes the source counter/clock lifetime, not480s evidence.
        first=bytes.fromhex("000000000000000000001e0000060000000000000000000000000000803d5630803d5620000000000000003c003b")
        terminal=bytes.fromhex("030000000000000001001e0000060100000000000000000000000000803d5630803d562000000e1000000000003b")
        unpack=lambda raw:(int.from_bytes(raw[36:40],"big"),int.from_bytes(raw[40:44],"big"),int.from_bytes(raw[44:46],"big"))
        self.assertEqual(unpack(first),(0,60,59));self.assertEqual(unpack(terminal),(3600,0,59))
        s=TimeoutInventory()
        # Same exact observed first counters0/1 and frame0 duplicated. Only
        # the declared480s normalization differs from the original60s capture.
        for counter in (0,1):s.input(bank());s.tick(counter,clock(0),live())
        self.assertEqual(s.ticks,2)

    def test_total_source_budget_includes_constructor_samples(self):
        s=ready();s.setup_samples=123;s.active_samples=29523-123
        with self.assertRaisesRegex(ValueError,"total source PAD cap"):s.input(bank())
        s=ready();s.setup_samples=1;s.active_samples=29521;s.input(bank())
        self.assertEqual(s.active_samples+s.setup_samples,29523)
        with self.assertRaises(ValueError):s.input(bank())

    def test_exact_live_pointer_ownership_and_minimal_json_ceiling(self):
        fields=live_slices()+[dict(tag=14,flags=0,address=0x8046b6a0,hex="00"*46),
            dict(tag=17,flags=0,address=0x80479d30,hex="02"+"00"*5),
            dict(tag=40,flags=0,address=0x810f0000,hex="02")]
        payload=dict(diagnostic="sd_initialization_prefix",name="tick",consumed=29523,
                     menu_consumed=7200,pc=PCS["tick"],slices=fields)
        self.assertEqual(live_fighters(slices(payload),payload)["stocks"],[4,4])
        self.assertLessEqual(len(canonical(payload))-1,2048)
        for tag,change in ((52,"secondary"),(53,"pointer"),(5,"identity"),(7,"address"),(8,"size")):
            bad=deepcopy(payload);f=next(v for v in bad["slices"] if v["tag"]==tag and v["flags"]==0)
            if change=="secondary":f["hex"]=f["hex"][:8]+"00000001"
            elif change=="pointer":f["hex"]="81010000"
            elif change=="identity":b=bytearray.fromhex(f["hex"]);b[12]=1;f["hex"]=b.hex()
            elif change=="address":f["address"]+=4
            else:f["hex"]=""
            with self.subTest(change=change),self.assertRaises(ValueError):live_fighters(slices(bad),bad)

    def test_canonical_terminal_and_retirement_negatives(self):
        for name in ("premature","wrongoutcome","wrongwinner","wrongstock","setupstock","wrongdamage","inactivehuman","inactivecpu","rank00","rank01","rank11","retire"):
            with self.subTest(name=name),self.assertRaises(ValueError):
                s=loss();s.input(bank());s.phase="neutral-timeout";s.frame=28800
                b=bytearray(result());v=live([3,4])
                if name=="premature":s.frame=28799
                elif name=="wrongoutcome":b[4]=4
                elif name=="wrongwinner":b[16]=0
                elif name=="wrongstock":b[0x60]=4
                elif name=="setupstock":v["stocks"]=[4,4]
                elif name=="wrongdamage":b[0x64]=1
                elif name=="inactivehuman":b[0x58+2*0xa8]=0
                elif name=="inactivecpu":b[0x58+5*0xa8]=1
                elif name=="rank00":b[0x5d]=0
                elif name=="rank01":b[0x5d]=0;b[0x105]=1
                elif name=="rank11":b[0x105]=1
                else:s.retire()
                s.exit(s.ticks,clock(28800),v,b)

    def test_native_policy_constants_owner_and_record_bound_source(self):
        root=Path(__file__).resolve().parents[1]
        h=(root/"reference-capture/dolphin/source/Core/PowerPC/ReferenceOrdinaryTimeoutState.h").read_text()
        self.assertIn(POLICY_SHA256,h)
        source=(root/"reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_text()
        self.assertIn("!AddPlayerEntitySlices(system, slot)",source)
        self.assertIn("ordinary_timeout.Exit(tick",source)
        self.assertIn("ordinary_timeout.Retire()",source)
        self.assertIn("ordinary_bytes + 44 + json.size()",source)
        # Text guards establish composition only; native compile/runtime unrun.

    def test_scoped_receiver_handshake_active_inventory_and_terminal_order(self):
        import test_sd_items_lock as helpers
        from test_sd_gci_profile import ready_rows
        helper=helpers.ItemsLockTests();helper.setUp();self.addCleanup(helper.doCleanups)
        r=OrdinaryTimeoutReceiver(make_input_plan(6),helper.profile)
        rows=ready_rows(helper.profile)[:-1]
        rows[0]["payload"].update(menu_probe="ordinary_timeout",ordinary_policy_sha256=POLICY_SHA256,
                                  recipe_sha256=r.plan["authored_recipe_sha256"])
        for row in rows:r.accept(row)
        # Synthetic precondition isolates the new active receiver; retained
        # Items/CSS/SSS/setup admission is exercised by the existing controls.
        r.order=2
        def progress(name,fields,count,tick):
            return dict(seq=r.seq,event="progress",source_tick=tick,payload=dict(
                diagnostic="sd_initialization_prefix",name=name,pc=PCS[name],consumed=count,
                menu_consumed=r.menu_consumed,slices=fields))
        r.accept(progress("input",[dict(tag=3,flags=0,address=0x810f0000,hex=bank().hex())],1,0))
        c=bytearray(46);c[40:44]=(480).to_bytes(4,"big");c[44:46]=(59).to_bytes(2,"big")
        fields=live_slices()+[dict(tag=14,flags=0,address=0x8046b6a0,hex=c.hex()),
            dict(tag=17,flags=0,address=0x80479d30,hex="02"+"00"*5),
            dict(tag=40,flags=0,address=0x810f1000,hex="02")]
        before=deepcopy(r)
        for change in ("clock_address","missing","foreignowner","tickgap","SD","premature_end"):
            bad=deepcopy(before);row=progress("tick",deepcopy(fields),1,0)
            if change=="clock_address":row["payload"]["slices"][-3]["address"]+=4
            elif change=="missing":row["payload"]["slices"].pop(0)
            elif change=="foreignowner":row["payload"]["slices"][-1]["hex"]="03"
            elif change=="tickgap":row["source_tick"]=1
            elif change=="SD":row["payload"].update(name="sd_entry",pc=PCS["sd_entry"])
            else:row.update(event="end",payload=dict(status="interrupted",natural=False))
            with self.subTest(change=change),self.assertRaises(ValueError):bad.accept(row)
        r.accept(progress("tick",fields,1,0));self.assertEqual(r.ordinary.ticks,1)
        # Synthetic failed-publication snapshots remain Error events. They
        # cannot replace a successful vs_exit or activate native completion.
        rejected=deepcopy(r)
        row=progress("vs_exit",fields,1,0)
        row.update(event="error")
        row["payload"]["name"]="terminal_rejected"
        with self.assertRaisesRegex(ValueError,"Ordinary event/scope/PC differs"):
            rejected.accept(row)
        self.assertEqual(rejected.order,2)
        self.assertIsNone(rejected.terminal)
        self.assertFalse(rejected.ended)
        for probe in ("competitive_entry","sd_prefix","rules_ready","ordinary_timeout"):
            payload=deepcopy(rows[0]["payload"]);payload["menu_probe"]=probe
            if probe=="ordinary_timeout":payload["ordinary_policy_sha256"]="0"*64
            t=OrdinaryTimeoutReceiver(make_input_plan(6),helper.profile)
            with self.subTest(probe=probe),self.assertRaises(ValueError):t.accept(dict(rows[0],payload=payload))
        # Old receiver declines the new scope even with the old recipe hash.
        legacy=helpers.GciRulesMenuReceiver(make_input_plan(6),helper.profile,full_route=True,
                                           guarded_items=True,competitive_entry=True)
        with self.assertRaises(ValueError):legacy.accept(rows[0])

    def test_bounded_owned_log_retains_overflow_prefix(self):
        target=io.BytesIO();log=BoundedLog(io.BytesIO(b"123456789"),target,5)
        log.thread.join(1)
        self.assertEqual(target.getvalue(),b"12345")
        with self.assertRaisesRegex(SdDiagnosticError,"log byte cap"):log.finish()

    def test_intent_caps_fail_before_fifo_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);c=BoundedIntentController(p/"unused1",p/"unused2",p/"intent")
            c.intent_records=CAPS["intent_records"]
            with self.assertRaisesRegex(SdDiagnosticError,"record cap"):c.write(1,raw_pad(),action="test")
            c.intent_records=0
            with self.assertRaisesRegex(SdDiagnosticError,"identity"):c.write(1,raw_pad(),action="x"*97)
            with self.assertRaisesRegex(SdDiagnosticError,"identity"):c.write(1,raw_pad(),action="\0"*96)
            self.assertFalse(c.log.exists())

    def test_runner_drain_initialization_and_independent_cleanup_errors(self):
        # Mocked lifecycle controls only: no Dolphin/native validation claim.
        from unittest import mock
        import capture_sd_reference_prefix as runner
        import test_sd_items_lock as helpers
        from sd_original_menu_plan import gci_competitive_entry_packet
        helper=helpers.ItemsLockTests();helper.setUp();self.addCleanup(helper.doCleanups)
        root=Path(__file__).resolve().parents[1]
        for mode in ("initialization","cleanup","receipt_write"):
            with self.subTest(mode=mode),tempfile.TemporaryDirectory() as directory:
                out=Path(directory)/"output";out.mkdir()
                manifest=Path(directory)/"manifest.json"
                manifest.write_bytes(canonical(dict(observer_source_overlay_sha256={
                    "Core/PowerPC/ReferenceCaptureObserver.cpp":hashlib.sha256((root/
                        "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_bytes()).hexdigest()})))
                policy_path=Path(directory)/"policy.json";policy_path.write_bytes(canonical(policy()))
                def prepare(_profile,user):
                    config=user/"Config";config.mkdir(parents=True)
                    for name in ("Dolphin.ini","GCPadNew.ini"):(config/name).write_text("owned synthetic control")
                    return user/"P1",user/"P2",{}
                child=mock.Mock(pid=404,stdout=io.BytesIO(),poll=mock.Mock(return_value=None))
                child.wait.side_effect=OSError("PID cleanup control") if mode=="cleanup" else None
                child.wait.return_value=0
                drain=mock.Mock(size=0);drain.thread.is_alive.return_value=False
                drain.check.side_effect=SdDiagnosticError("primary source control")
                drain.finish.side_effect=RuntimeError("drain cleanup control")
                original_write=Path.write_bytes
                def write(path,payload):
                    if mode=="receipt_write" and path.name=="cleanup.json":
                        raise OSError("cleanup receipt control")
                    return original_write(path,payload)
                with mock.patch.object(runner,"validate_reference_build_manifest",return_value={
                        "sha256":hashlib.sha256(manifest.read_bytes()).hexdigest()}), \
                     mock.patch.object(runner,"load_plan",return_value=(make_input_plan(6),"entry-only")), \
                     mock.patch.object(runner,"menu_actions",return_value=(gci_competitive_entry_packet(),"menu8")), \
                     mock.patch("sd_gci_profile.prepare_gci_folder",return_value=(helper.profile,out/"owned.gci")), \
                     mock.patch.object(runner,"prepare_rules_profile",side_effect=prepare), \
                     mock.patch.object(runner.subprocess,"Popen",return_value=child), \
                     mock.patch.object(runner,"BoundedLog",side_effect=RuntimeError("drain init control")
                                       if mode=="initialization" else None,return_value=drain), \
                     mock.patch.object(Path,"write_bytes",new=write):
                    with self.assertRaisesRegex((RuntimeError,SdDiagnosticError),
                        "drain init control" if mode=="initialization" else "primary source control"):
                        runner._run(dolphin=Path("unlaunched"),disc=Path("unread"),profile=Path("unused"),
                            input_plan=Path("unused"),menu_recipe=Path("unused"),output=out,
                            build_manifest=manifest,timeout=600,gci=Path("unread"),ordinary_policy=policy_path)
                child.terminate.assert_called_once();child.wait.assert_called_once()
                if mode=="receipt_write":self.assertFalse((out/"cleanup.json").exists())
                else:self.assertEqual(json.loads((out/"cleanup.json").read_text())["pid"],404)
                log_cleanup=json.loads((out/"owned-log-cleanup.json").read_text())
                if mode in ("cleanup","receipt_write"):
                    drain.finish.assert_called_once()
                    self.assertEqual(log_cleanup["error"],"drain cleanup control")
                    self.assertIn("primary source control",json.loads((out/"failure.json").read_text())["error"])
                    if mode=="receipt_write":self.assertEqual(log_cleanup["native_cleanup_error"],"cleanup receipt control")
                else:
                    self.assertFalse(log_cleanup["initialized"]);self.assertTrue(child.stdout.closed)
