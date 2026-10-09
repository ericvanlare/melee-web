"""Portable route/receiver controls, never native original-route validation."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from test_sd_reference_diagnostic import events, index
from test_sd_gci_profile import fixture, ready_rows
from authored_sd_reference_plan import make_input_plan, recipe
from retail_input_plan import load_plan, NEUTRAL_PAD
from reference_versus_sequence_capture import raw_pad
from sd_original_menu_plan import gci_sd_prefix_packet, validate_packet, route_pads
from sd_gci_profile import load_profile
from sd_reference_diagnostic import (GciRulesMenuReceiver, SdDiagnosticError,
                                     stage_state, css_state, slices, SCOPE, PCS)
from capture_sd_reference_prefix import menu_actions, run, ROOT


def route_rows(profile):
    rows = ready_rows(profile)[:-1]
    rows[0]["payload"].update(recipe_sha256=make_input_plan(5)["authored_recipe_sha256"], menu_probe="sd_prefix")
    def menu(fields):
        rows.append({"seq": len(rows), "event": "progress", "source_tick": 0,
            "payload": {"diagnostic": SCOPE, "name": "menu", "pc": PCS["menu"],
                        "consumed": 0, "menu_consumed": 1, "slices": fields}})
    def field(tag, raw, flags=0, address=0x80001000):
        return {"tag": tag, "flags": flags, "address": address, "hex": bytes(raw).hex()}
    live, doors = bytearray(0x148), bytearray(0x90)
    for slot in range(4):
        live[0x71 + slot*0x24] = 0 if slot < 2 else 3
    for slot, costume in enumerate((1,0)):
        live[0x70 + slot*0x24] = 8
        doors[slot*0x24+13] = costume
        doors[slot*0x24+14] = 1
    fields = [field(40,b"\10"),field(48,live),field(44,doors)]
    fields += [field(tag,bytearray(size),slot) for slot in range(2) for tag,size in ((43,0x14),(47,0x18))]
    menu(fields)
    menu([field(40,b"\11"),field(55,b"\0"*4,address=0x804d6ca4),
          field(41,b"\2",address=0x804d6cae),field(42,b"\40",address=0x803f06d0+2*0x1c+0xb)])
    tail = events()[2:]
    normal_item = next(s for s in tail[0]["payload"]["slices"] if s["tag"] == 4 and s["flags"] == 0)
    normal = bytearray.fromhex(normal_item["hex"])
    normal[2] &= ~8
    for slot, costume in enumerate((1,0)):
        base = 0x60 + slot*0x24
        normal[base+3] = costume
        normal[base+0xc] |= 0x80
    persistent = bytearray(normal); persistent[2] &= ~0x80; persistent[4] &= ~0x40
    sd = bytearray(persistent); sd[0] &= ~2; sd[2] &= ~4
    for slot in range(2):
        base = 0x60+slot*0x24
        sd[base+2] = 1; sd[base+0x12:base+0x14] = (300).to_bytes(2,"big")
    for row in tail:
        for item in row["payload"].get("slices", []):
            if item["tag"] == 54: item["hex"] = "01010101"
            if item["tag"] != 4: continue
            name = row["payload"]["name"]
            if item["flags"] == 1: raw = persistent
            elif name in ("vs_entry","vs_setup"): raw = normal
            else:
                raw = bytearray(sd)
                if name == "sd_setup": raw[6] = 1
            item["hex"] = bytes(raw).hex()
        row["seq"] = len(rows); rows.append(row)
    return rows


class OriginalSdRouteTests(unittest.TestCase):
    def test_extended_deadline_and_stale_producer_fail_before_launch(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp)
            plan, menu, manifest = base/'plan.json', base/'menu.json', base/'manifest.json'
            plan.write_text(json.dumps(make_input_plan(5)))
            menu.write_text(json.dumps(gci_sd_prefix_packet()))
            overlay=ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp'
            valid={'observer_source_overlay_sha256':{'Core/PowerPC/ReferenceCaptureObserver.cpp':
                   hashlib.sha256(overlay.read_bytes()).hexdigest()}}
            for stale in (True, False):
                body={} if stale else valid
                manifest.write_text(json.dumps(body))
                build={'sha256':hashlib.sha256(manifest.read_bytes()).hexdigest()}
                with mock.patch('capture_sd_reference_prefix.validate_reference_build_manifest',return_value=build), \
                     mock.patch('sd_gci_profile.prepare_gci_folder',return_value=({'sha256':'unit-profile'},base/'owned.gci')), \
                     mock.patch('capture_sd_reference_prefix.GciRulesMenuReceiver'), \
                     mock.patch('capture_sd_reference_prefix.subprocess.Popen') as launch:
                    output=base/('stale' if stale else 'deadline')
                    with self.assertRaisesRegex(SdDiagnosticError,'stale' if stale else 'deadline'):
                        run(dolphin='unused',disc='unused',profile='unused',input_plan=plan,menu_recipe=menu,
                            output=output,build_manifest=manifest,timeout=181,gci='unused')
                    launch.assert_not_called()
                    failure=json.loads((output/'failure.json').read_text())
                    self.assertEqual(failure['scope'],'sd_prefix_gci')
                    self.assertFalse(failure['native_launched'])

    def test_declared_recipe_setup_and_finite_source_menu_policy(self):
        plan = make_input_plan(5); packet = gci_sd_prefix_packet()
        validate_packet(packet)
        self.assertEqual(packet["authored_recipe_sha256"], plan["authored_recipe_sha256"])
        self.assertEqual(recipe(4)["expected_setup"]["disable_pausing"], True)
        self.assertEqual(recipe(5)["cold_original_context"]["port_rumble_preferences"], [1]*4)
        frequency = [a for a in packet["actions"] if a["label"].startswith("items-frequency-")]
        self.assertEqual([a["after"]["value"] for a in frequency], [3,2,1,0])
        self.assertEqual(frequency[0]["p1"], raw_pad(buttons=["D_UP"]))
        self.assertTrue(all(a["p1"] == raw_pad(buttons=["D_RIGHT"])
                            for a in frequency[1:]))
        for change in (lambda p:p["sss"].__setitem__("max_scan_polls",601),
                       lambda p:p["css"]["costumes"].reverse(),
                       lambda p:p["actions"][-2].__setitem__("p1",NEUTRAL_PAD)):
            altered = deepcopy(packet); change(altered)
            with self.assertRaises(ValueError): validate_packet(altered)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"packet.json"; path.write_text(json.dumps(packet))
            self.assertEqual(menu_actions(path)[0], packet)
            path.write_text(json.dumps(plan))
            with self.assertRaises(ValueError): load_plan(path)
        self.assertTrue(all(pad[1] == NEUTRAL_PAD for pad in route_pads(packet)
                            if pad[0] != NEUTRAL_PAD))

    def test_constructor_cooldown_and_random_stage_are_exact(self):
        def payload(index=2, cooldown=0):
            fields = [(40,0x80001000,b"\11"),(55,0x804d6ca4,cooldown.to_bytes(4,"big")),
                      (41,0x804d6cae,bytes([index]))]
            if index < 30: fields.append((42,0x803f06d0+index*0x1c+0xb,b"\40"))
            return {"diagnostic":SCOPE,"name":"menu","consumed":0,"pc":PCS['menu'],
                    "slices":[{"tag":t,"flags":0,"address":a,"hex":b.hex()} for t,a,b in fields]}
        for index in (2,30):
            p=payload(index,20)
            self.assertEqual(stage_state(slices(p),p)["cooldown"],20)
        p=payload(); self.assertEqual(stage_state(slices(p),p)["kind"],32)
        for mutate in (lambda p:p['slices'].pop(1),lambda p:p['slices'][1].update(address=0x804d6ca8),
                       lambda p:p['slices'][1].update(hex='00000015'),
                       lambda p:p['slices'][2].update(hex='1f'),
                       lambda p:p['slices'][3].update(address=0x803f06d0)):
            p=payload(); mutate(p)
            with self.assertRaises(SdDiagnosticError): stage_state(slices(p),p)

    def test_full_declared_timeout_and_both_rumble_normalizations(self):
        with tempfile.TemporaryDirectory() as temp:
            raw=fixture(); sha=hashlib.sha256(raw).hexdigest()
            path=Path(temp)/'profile.gci'; path.write_bytes(raw)
            with mock.patch('sd_gci_profile.GCI_SHA256',sha):
                profile=load_profile(path); plan=make_input_plan(5); rows=route_rows(profile)
                receiver=GciRulesMenuReceiver(plan,profile,full_route=True)
                for row in rows: receiver.accept(row)
                self.assertTrue(receiver.ended); self.assertEqual(receiver.tick_count,3600)
                for name in ('vs_entry','sd_entry'):
                    self.assertEqual(receiver.records[name][(4,0)][0x6c] & 0x80,0x80)
                for name,offset in (('vs_entry',0x6c),('sd_entry',0x6c),('sd_entry',0x6d)):
                    bad=deepcopy(rows); row=bad[index(bad,name)]
                    field=next(s for s in row['payload']['slices'] if s['tag']==4 and s['flags']==0)
                    body=bytearray.fromhex(field['hex']); body[offset]^=0x80; field['hex']=body.hex()
                    receiver=GciRulesMenuReceiver(plan,profile,full_route=True)
                    with self.assertRaises(ValueError):
                        for row in bad: receiver.accept(row)
                # Rules-only interrupted completion cannot admit the full prefix.
                bad=ready_rows(profile); bad[0]['payload'].update(menu_probe='sd_prefix',recipe_sha256=plan['authored_recipe_sha256'])
                receiver=GciRulesMenuReceiver(plan,profile,full_route=True)
                with self.assertRaises(SdDiagnosticError):
                    for row in bad: receiver.accept(row)
                # Constructor slices alone cannot invent the missing final owner.
                for missing in (43, 44, 48, 55):
                    bad=deepcopy(rows)
                    for row in bad:
                        if 'slices' in row['payload']:
                            row['payload']['slices'] = [s for s in row['payload']['slices'] if s['tag'] != missing]
                    receiver=GciRulesMenuReceiver(plan,profile,full_route=True)
                    with self.assertRaises(SdDiagnosticError):
                        for row in bad: receiver.accept(row)
                bad=deepcopy(rows)
                owner=next(row for row in bad if row['payload'].get('name')=='menu' and
                           any(s['tag']==55 for s in row['payload']['slices']))
                absent=deepcopy(owner)
                absent['payload']['slices']=[s for s in absent['payload']['slices'] if s['tag']==40]
                bad.insert(owner['seq']+1,absent)
                for seq,row in enumerate(bad): row['seq']=seq
                receiver=GciRulesMenuReceiver(plan,profile,full_route=True)
                with self.assertRaisesRegex(SdDiagnosticError,'disappeared'):
                    for row in bad: receiver.accept(row)
