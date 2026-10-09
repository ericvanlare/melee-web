"""Synthetic portable setup/receiver controls; no original-run validation."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from test_sd_reference_diagnostic import events, ROOT
from authored_sd_reference_plan import make_input_plan
from reference_capture_save import encode_block
from retail_input_plan import NEUTRAL_PAD, DISCONNECTED_PAD
from sd_gci_profile import load_profile, prepare_gci_folder, SAVE_BYTES, BANK_BYTES
from sd_original_menu_plan import gci_rules_ready_packet, validate_packet
from sd_reference_diagnostic import GciRulesMenuReceiver, RulesMenuReceiver, SdDiagnosticError, SCOPE, PCS
from capture_sd_reference_prefix import run, prepare_rules_profile


def fixture():
    raw = bytearray(90176)
    raw[:6] = b"GALE01"
    raw[8:32] = b"SuperSmashBros0110290334"
    raw[56:58] = (11).to_bytes(2, "big")
    save = bytearray(SAVE_BYTES)
    save[:5] = bytes.fromhex("07ff07ff0f")
    save[0x448] = 2
    save[0x458:0x45c] = b"\1" * 4
    for slot in range(1, 11):
        identity = slot if slot <= 8 else (0xffff if slot == 9 else 1)
        decoded = bytearray(8192)
        decoded[16:18] = identity.to_bytes(2, "big")
        if identity == 1:
            decoded[32:32 + SAVE_BYTES] = save
        elif identity != 0xffff:
            decoded[32 + 0x198:32 + 0x1a0] = bytes.fromhex("8260826082618261")
        raw[64 + slot * 8192:64 + (slot + 1) * 8192] = encode_block(decoded)
    return bytes(raw)


def ready_rows(profile):
    rows = events(4)[:2]
    rows[0]["payload"].update(menu_probe="rules_ready", profile_gci_sha256=profile["sha256"])
    root = 0x80400000
    def add(name, count, fields):
        rows.append({"seq": len(rows), "event": "progress", "source_tick": 0,
                     "payload": {"diagnostic": SCOPE, "name": name, "pc": PCS[name],
                                 "consumed": 0, "menu_consumed": count,
                                 "slices": [{"tag": t, "flags": 0, "address": a, "hex": bytes(b).hex()}
                                            for t, a, b in fields]}})
    add("menu", 0, [(40, 0x803dfde4, b"\0")])
    pad = b"".join(bytes.fromhex(p) + b"\0" for p in
                   (NEUTRAL_PAD, NEUTRAL_PAD, DISCONNECTED_PAD, DISCONNECTED_PAD))
    add("menu_input", 1, [(3, 0x8046b908, pad)])
    flow = bytearray(24); flow[0] = 13
    menu = [(40, 0x803dfde4, b"\1"), (45, 0x804a04f0, flow), (46, 0x804d6bc8, b"\0" * 8)]
    add("menu", 1, menu)
    save = bytearray(profile["save"] + b"".join(profile["banks"][:2]))
    save[0x1eb] = 77  # Original mutable PowerCount retained, never equality-masked.
    add("rules_ready", 1, menu + [(54, root + 0x1cc0, b"\1" * 4),
        (36, root + 0x1868, bytes.fromhex("07ff")), (37, root + 0x186a, bytes.fromhex("07ff")),
        (38, root + 0x1850, b"\0" * 24), (39, root + 0x1868, save)])
    rows.append({"seq": len(rows), "event": "end", "source_tick": 0,
                 "payload": {"status": "interrupted", "natural": False}})
    return rows


class GciProfileTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.raw = fixture(); self.sha = hashlib.sha256(self.raw).hexdigest()
        self.patch = mock.patch("sd_gci_profile.GCI_SHA256", self.sha)
        self.patch.start(); self.addCleanup(self.patch.stop)
        self.source = self.base / "source.gci"; self.source.write_bytes(self.raw)

    def test_exact_owned_copy_and_original_immutable(self):
        profile, target = prepare_gci_folder(self.source, self.base / "card")
        self.assertEqual(target.read_bytes(), self.raw)
        self.assertEqual(self.source.read_bytes(), self.raw)
        self.assertEqual(target.stat().st_mode & 0o777, 0o400)
        self.assertEqual(len(profile["save"]), SAVE_BYTES)
        self.assertEqual([len(b) for b in profile["banks"]], [BANK_BYTES] * 7)
        with self.assertRaises(FileExistsError): prepare_gci_folder(self.source, self.base / "card")
        self.source.write_bytes(self.raw[:-1] + b"X")
        with self.assertRaises(ValueError): load_profile(self.source)
        link = self.base / "redirect.gci"; link.symlink_to(target)
        with self.assertRaises(ValueError): load_profile(link)

    def test_loaded_context_and_mutable_progress_have_distinct_scope(self):
        profile = load_profile(self.source)
        receiver = GciRulesMenuReceiver(make_input_plan(4), profile)
        for row in ready_rows(profile): receiver.accept(row)
        self.assertTrue(receiver.ended)
        self.assertEqual(bytes.fromhex(receiver.loaded_context["save_hex"])[0x1eb], 77)
        self.assertEqual(len(receiver.loaded_context["name_bank_sha256"]), 2)
        with self.assertRaises(SdDiagnosticError): RulesMenuReceiver(make_input_plan(4))

    def test_missing_unlock_preferences_name_and_root_fail(self):
        profile = load_profile(self.source)
        for tag, offset in ((39, 4), (39, 0x45b), (39, SAVE_BYTES + BANK_BYTES - 1), (37, 0)):
            rows = ready_rows(profile)
            item = next(s for s in rows[5]["payload"]["slices"] if s["tag"] == tag)
            raw = bytearray.fromhex(item["hex"]); raw[offset] ^= 1; item["hex"] = raw.hex()
            with self.assertRaises(SdDiagnosticError):
                receiver = GciRulesMenuReceiver(make_input_plan(4), profile)
                for row in rows: receiver.accept(row)
        rows = ready_rows(profile)
        next(s for s in rows[5]["payload"]["slices"] if s["tag"] == 38)["address"] += 4
        with self.assertRaises(SdDiagnosticError):
            receiver = GciRulesMenuReceiver(make_input_plan(4), profile)
            for row in rows: receiver.accept(row)

    def test_exact_new_packet_and_stale_producer_decline(self):
        packet = gci_rules_ready_packet(); validate_packet(packet)
        with self.assertRaises(ValueError): validate_packet(dict(packet, profile_gci_sha256="0" * 64))
        self.assertNotIn(42, [a["scene"] for a in packet["boot"]])
        plan, menu, manifest = (self.base / n for n in ("plan.json", "menu.json", "build.json"))
        plan.write_text(json.dumps(make_input_plan(4))); menu.write_text(json.dumps(packet))
        manifest.write_text('{"observer_source_overlay_sha256":{}}')
        build = {"sha256": hashlib.sha256(manifest.read_bytes()).hexdigest()}
        with mock.patch("capture_sd_reference_prefix.validate_reference_build_manifest", return_value=build), \
             mock.patch("capture_sd_reference_prefix.subprocess.Popen") as launch:
            with self.assertRaisesRegex(SdDiagnosticError, "stale"):
                run(dolphin="unused", disc="unused", profile="unused", input_plan=plan,
                    menu_recipe=menu, output=self.base / "failed", build_manifest=manifest, gci=self.source)
            launch.assert_not_called()
        self.assertEqual(json.loads((self.base / "failed/failure.json").read_text())["scope"], "rules_ready_gci")
