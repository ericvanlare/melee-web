"""Reconstructed Items controls; these do not validate a native capture."""
from copy import deepcopy
import unittest
import hashlib
import tempfile
from pathlib import Path
from unittest import mock

from test_sd_gci_profile import fixture, ready_rows
from authored_sd_reference_plan import make_input_plan
from retail_input_plan import NEUTRAL_PAD, DISCONNECTED_PAD
from reference_versus_sequence_capture import raw_pad
from sd_original_menu_plan import gci_items_row_packet, validate_packet
from sd_reference_diagnostic import (GciRulesMenuReceiver, SdDiagnosticError,
                                    items_lock_state, slices, SCOPE, PCS)
from sd_gci_profile import load_profile


def menu_row(seq, *, lock=0, row=0, value=1, name="menu", count=1):
    flow=bytearray(24);flow[0]=16;flow[2:4]=row.to_bytes(2,"big");flow[4]=value;flow[17]=1
    fields=[(40,0x80001000,b"\1"),(45,0x804a04f0,flow),
            (46,0x804d6bc8,bytes(8)),(56,0x804d6bec,bytes([lock]))]
    return {"seq":seq,"event":"progress","source_tick":0,"payload":{
        "diagnostic":SCOPE,"name":name,"pc":PCS[name],"consumed":0,"menu_consumed":count,
        "slices":[{"tag":t,"flags":0,"address":a,"hex":bytes(b).hex()} for t,a,b in fields]}}


def receiver(profile):
    plan=make_input_plan(5)
    result=GciRulesMenuReceiver(plan,profile,full_route=True,items_probe=True)
    rows=ready_rows(profile)[:-1]
    rows[0]["payload"].update(recipe_sha256=plan["authored_recipe_sha256"],menu_probe="items_row")
    for row in rows: result.accept(row)
    return result


class ItemsLockTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
        raw=fixture();patch=mock.patch('sd_gci_profile.GCI_SHA256',hashlib.sha256(raw).hexdigest())
        patch.start();self.addCleanup(patch.stop)
        path=Path(temp.name)/'fixture.gci';path.write_bytes(raw);self.profile=load_profile(path)

    def test_reduced_packet_has_one_up_and_no_continuation(self):
        packet=gci_items_row_packet();validate_packet(packet)
        self.assertNotIn("css",packet);self.assertNotIn("sss",packet)
        self.assertEqual(packet["actions"][-1]["p1"],raw_pad(buttons=["D_UP"]))
        self.assertEqual(packet["actions"][-1]["before"]["items_locked"],0)
        self.assertEqual(packet["stop"]["row"],31)
        bad=deepcopy(packet);bad["actions"][-1]["p1"]=raw_pad(buttons=["D_LEFT"])
        with self.assertRaises(ValueError):validate_packet(bad)

    def test_missing_wrong_owner_size_address_value_rejected(self):
        for change in (lambda p:p["slices"].pop(),
                       lambda p:p["slices"][-1].update(address=0x804d6bed),
                       lambda p:p["slices"][-1].update(hex="0000"),
                       lambda p:p["slices"][-1].update(hex="02"),
                       lambda p:p["slices"][1].update(hex="0d"+p["slices"][1]["hex"][2:])):
            r=receiver(self.profile);row=menu_row(r.seq);change(row["payload"])
            with self.assertRaises(SdDiagnosticError):r.accept(row)

    def test_locked_input_rejected_and_reconstructed_unlocked_stop(self):
        for lock in (1,0):
            r=receiver(self.profile);r.accept(menu_row(r.seq,lock=lock))
            pads=[raw_pad(buttons=["D_UP"]),NEUTRAL_PAD,DISCONNECTED_PAD,DISCONNECTED_PAD]
            raw=b"".join(bytes.fromhex(p)+b"\0" for p in pads)
            row={"seq":r.seq,"event":"progress","source_tick":0,"payload":{
                "diagnostic":SCOPE,"name":"menu_input","pc":PCS["menu_input"],
                "consumed":0,"menu_consumed":2,"slices":[{
                    "tag":3,"flags":0,"address":0x80001000,"hex":raw.hex()}]}}
            if lock:
                with self.assertRaisesRegex(SdDiagnosticError,"while locked"):r.accept(row)
                continue
            r.accept(row)
            row=deepcopy(row);row["seq"]=r.seq;row["payload"]["menu_consumed"]=3
            row["payload"]["slices"][0]["hex"]=b"".join(bytes.fromhex(p)+b"\0" for p in
                [NEUTRAL_PAD]*2+[DISCONNECTED_PAD]*2).hex()
            r.accept(row)
            r.accept(menu_row(r.seq,row=31,value=3,count=3,name="items_ready"))
            self.assertTrue(r.items_ready)
            r.accept({"seq":r.seq,"event":"end","source_tick":0,
                      "payload":{"status":"interrupted","natural":False}})
            self.assertTrue(r.ended)

    def test_premature_ready_and_gameplay_rejected(self):
        r=receiver(self.profile)
        with self.assertRaises(SdDiagnosticError):r.accept(menu_row(r.seq,name="items_ready"))
        r=receiver(self.profile);row=menu_row(r.seq);row["payload"].update(name="vs_entry",pc=PCS["vs_entry"])
        with self.assertRaises(SdDiagnosticError):r.accept(row)
