"""Reduced original-profile Items controls; no GCI, native launch or game writes."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "scripts"), str(ROOT / "reference-capture/dolphin")]
from authored_sd_reference_plan import make_input_plan
from capture_sd_reference_prefix import SheikTransformPrefixReceiver
from reference_versus_sequence_capture import raw_pad
from sd_original_menu_plan import sheik_transform_prefix_packet
from sheik_transform_prefix import (ITEMS_COMPLETION_BOUNDARY, TransformPrefixError,
                                    validate_transform_prefix)
from test_sheik_transform_capture_driver import _serialized_items_rows
from test_sheik_transform_prefix import _capture, _json, _frame, _boundary_payload
import reference_observer_stream as stream


def reduced_capture(path, specs, *, end_change=None, status_change=None, extra_boundary=None):
    # Retain the existing pinned handshake fixture; all boundary bytes go through
    # the actual MWRO writer fixture and production transport decoder.
    _capture(path)
    headers = list(stream.iter_records(path))[:2]
    rows = _serialized_items_rows(path, specs)
    encoded = []
    for i, row in enumerate(headers):
        payload = dict(row["payload"], completion_boundary=ITEMS_COMPLETION_BOUNDARY)
        encoded.append(_json(i + 1, i, payload))
    for i, row in enumerate(rows, 2):
        p = row["payload"]
        if p["boundary"] == "pad_consume":
            slot = bytes.fromhex(next(x["hex"] for x in p["slices"] if x["tag"] == 3))
            payload = _boundary_payload(2, [], pad_slot=slot)
        else:
            payload = _boundary_payload(1, [(x["tag"], x["flags"], x["address"],
                                             bytes.fromhex(x["hex"])) for x in p["slices"]])
        encoded.append(_frame(3, i, payload, pc=p["pc"], source_tick=0))
    if extra_boundary:
        encoded.append(_frame(3, len(encoded), _boundary_payload(5, []), pc=0x8016E9C4, source_tick=0))
    end = {"status": "interrupted", "natural": False,
           "completion_boundary": ITEMS_COMPLETION_BOUNDARY, "items_boundary_complete": True,
           "transform_complete": False, "match_complete": False}
    end.update(end_change or {})
    encoded.append(_json(6, len(encoded), end))
    path.write_bytes(b"".join(encoded))
    status = {"state": "interrupted", "event_count": len(encoded), "last_seq": len(encoded)-1,
              "source_tick": 0, "draw_ordinal": 0, "completed": False, "invalid": False, "error": None}
    status.update(status_change or {})
    Path(str(path)+'.status.json').write_text(json.dumps(status))
    return list(stream.iter_records(path))


def specs():
    up = raw_pad(buttons=["D_UP"])
    return [
        # Rules uses Up before opening Items; it is not the Items episode.
        {"kind": 13, "row": 6, "missing_lock": True},
        {"boundary": "pad_consume", "p1": up}, {"boundary": "pad_consume"},
        {"locked": 1}, {"locked": 0},
        {"boundary": "pad_consume", "p1": up},
        {"boundary": "pad_consume", "p1": up},
        # The first row transition can precede consumed neutral.
        {"locked": 0, "row": 31, "value": 3},
        {"boundary": "pad_consume"},
        {"locked": 0, "row": 31, "value": 3}]


class SheikTransformItemsTests(unittest.TestCase):
    def test_live_and_offline_share_exact_items_sequence(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'items.mwro'
            rows = reduced_capture(path, specs())
            receiver = SheikTransformPrefixReceiver(make_input_plan(8), sheik_transform_prefix_packet(), items_only=True)
            for row in rows:
                receiver.accept(row)
            live = receiver.finish()['items']
            offline = validate_transform_prefix(path, items_only=True)
            self.assertEqual(live, offline['items'])
            self.assertLess(live['up_sequence'], live['first_row31_sequence'])
            self.assertLess(live['first_row31_sequence'], live['neutral_sequence'])
            self.assertLess(live['neutral_sequence'], live['terminal_poll_sequence'])
            self.assertFalse(offline['transform_complete'])
            with self.assertRaises(TransformPrefixError):
                validate_transform_prefix(path)

    def test_strict_input_owner_and_terminal_refusals(self):
        cases = []
        base = specs()
        missing = [x for i,x in enumerate(base) if i != 8]
        cases.append((missing, {}, {}, None))
        second = base[:9] + [base[5]] + base[9:]
        cases.append((second, {}, {}, None))
        locked = specs(); locked[4] = {'locked': 1}; cases.append((locked, {}, {}, None))
        for spec in [{'missing_lock': True}, {'lock_address': 0x804D6BED},
                     {'lock_raw': bytes((0,0))}, {'lock_raw': bytes((2,))},
                     {'lock_flags': 1}, {'pc': 0x80377584}]:
            bad = specs();bad[4] = spec;cases.append((bad, {}, {}, None))
        cases += [(specs(), {'status':'completed','natural':True}, {}, None),
                  (specs(), {'transform_complete':True}, {}, None),
                  (specs(), {}, {'completed':True}, None),
                  (specs(), {}, {'event_count':1}, None),
                  (specs()[:-1], {}, {}, 'setup')]
        for n,(samples,end,status,extra) in enumerate(cases):
            with self.subTest(n=n), tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'bad.mwro'; reduced_capture(path,samples,end_change=end,status_change=status,extra_boundary=extra)
                with self.assertRaises(ValueError): validate_transform_prefix(path, items_only=True)

    def test_reduced_terminal_requires_both_independent_writers(self):
        from unittest.mock import patch
        import capture_sd_reference_prefix as driver
        with tempfile.TemporaryDirectory() as tmp:
            observer, native = Path(tmp)/"observer", Path(tmp)/"native"
            observer.touch(); native.touch()
            primary = {"invalid": False, "error": None, "state": "interrupted"}
            inputs = {"invalid": False, "error": None, "complete": True}
            with patch.object(driver, "read_status", return_value=primary), patch.object(driver, "validate_status", return_value=inputs):
                self.assertEqual(driver.wait_transform_terminal_statuses(observer, native,
                    driver.time.monotonic()+1, items_only=True), (primary, inputs))
            for side, bad in [("read_status", dict(primary, invalid=True)),
                              ("validate_status", dict(inputs, invalid=True))]:
                with patch.object(driver, "read_status", return_value=primary), patch.object(driver, "validate_status", return_value=inputs), patch.object(driver, side, return_value=bad):
                    with self.assertRaises(ValueError):
                        driver.wait_transform_terminal_statuses(observer, native,
                            driver.time.monotonic()+1, items_only=True)
            # A completed transform terminal is not the declared reduced terminal.
            with patch.object(driver, "read_status", return_value=dict(primary, state="completed")), patch.object(driver, "validate_status", return_value=inputs), patch.object(driver.time, "monotonic", side_effect=[0, 2]), patch.object(driver.time, "sleep"):
                with self.assertRaisesRegex(ValueError, "deadline expired"):
                    driver.wait_transform_terminal_statuses(observer, native, 1, items_only=True)

    def test_reduced_action_selection_is_exact_original_prefix(self):
        from capture_sd_reference_prefix import menu_actions_for_capture
        menus = sheik_transform_prefix_packet()
        before = json.dumps(menus, sort_keys=True)
        selected = menu_actions_for_capture(menus, True)
        self.assertEqual(selected[-1]["label"], "items-frequency-row")
        self.assertEqual(selected, menus["actions"][:len(selected)])
        self.assertTrue(all(action["max_polls"] == 600 for action in selected))
        self.assertEqual(json.dumps(menus, sort_keys=True), before)
        self.assertIs(menu_actions_for_capture(menus), menus["actions"])
        with self.assertRaises(ValueError):
            menu_actions_for_capture(dict(menus, scope="items_row_gci"), True)

    def test_actual_native_stop_and_literal_pad_guards_compile(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        if not compiler: self.skipTest('A native C++ compiler is not installed')
        source=(ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        guards=source[source.index('int TransformItemsPadKind('):source.index('enum class TransformPrefixEntryDisposition')]
        main=r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
using u8=uint8_t;
''' + guards + r'''
int main(){
 u8 pad[48]{};pad[34]=pad[46]=255;
 assert(TransformItemsPadKind(pad)==0);pad[1]=8;
 assert(TransformItemsPadKind(pad)==1);pad[11]=77;
 assert(TransformItemsPadKind(pad)==1);pad[12]=1;
 assert(TransformItemsPadKind(pad)==-1);pad[12]=0;pad[34]=0;
 assert(TransformItemsPadKind(pad)==-1);
 u8 flow[24]{};u8 input[8]{};flow[0]=16;flow[3]=31;flow[4]=3;flow[17]=1;
 assert(TransformItemsStopReady(1,flow,input,0,true,true));
 assert(!TransformItemsStopReady(1,flow,input,1,true,true));
 assert(!TransformItemsStopReady(1,flow,input,0,false,true));
 assert(!TransformItemsStopReady(1,flow,input,0,true,false));
 assert(!TransformItemsStopReady(8,flow,input,0,true,true));
 for(int byte: {0,3,4,17}){u8 old=flow[byte];flow[byte]=0;assert(!TransformItemsStopReady(1,flow,input,0,true,true));flow[byte]=old;}
 input[0]=1;assert(!TransformItemsStopReady(1,flow,input,0,true,true));
}
'''
        main=main.replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>')
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'guard.cpp';exe=Path(tmp)/'guard';cpp.write_text(main)
            subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],check=True,capture_output=True)
            subprocess.run([str(exe)],check=True,capture_output=True)
