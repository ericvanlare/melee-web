# Read-only GALE01 revision-2 CSS/SSS/match lifecycle collector.
# Source this file from the same isolated Dolphin GDB session documented in
# docs/ORIGINAL_COMPARISON.md. It uses hardware execution breakpoints and
# bounded reads; it never writes game memory or patches code.

import gdb
import json
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from transition_trace_format import SCHEMA, VERSION, decode_start_melee_data

GAME_REVISION = "GALE01r2"
DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
EMULATOR_VERSION = "2606a"
EMULATOR_COMMIT = "c77bbaa0f372c3f72281602a8b087206706542cb"
ROOT = Path(os.environ.get("MELEE_REFERENCE_WORK", "work")).resolve()
OUTPUT = Path(os.environ.get(
    "MELEE_TRANSITION_TRACE", ROOT / "reference-transition.jsonl")).resolve()

CSS_POINTER = 0x804D6CB0
SSS_POINTER = 0x804D6C90
RNG_POINTER = 0x804D5F94
RNG_DEFAULT = 0x804D5F90
CURRENT_HPS = 0x803BB300
CURRENT_HPS_SIZE = 0x40
HPS_VOICE = 0x804D6038

CSS_ENTER = 0x8026688C
CSS_EXIT = 0x80266D70
SSS_ENTER = 0x8025A998
SSS_EXIT = 0x8025BB5C
MATCH_ENTER = 0x8016E934
STREAM_START = 0x8038E8EC
STREAM_STOP = 0x8038E968
DRIVER_INITIALIZE = 0x8002838C
LANGUAGE_BANK_INITIALIZE = 0x80028690

active = False
run = 0
event_index = 0
current_entry = None
current_return = None
css_pointer = 0
sss_pointer = 0
match_pointer = 0
sss_exits = 0
stream_starts = 0
stream_stops = 0
driver_reinitializations = 0
language_bank_initializations = 0
counter_breakpoints = []


def memory(address, size):
    if not 0x80000000 <= address < 0x81800000 or size > 0x1000:
        raise RuntimeError("Reference memory range rejected")
    if address + size > 0x81800000:
        raise RuntimeError("Reference memory range crosses RAM")
    return bytes(gdb.selected_inferior().read_memory(address, size))


def word(address):
    return struct.unpack(">I", memory(address, 4))[0]


def cstring(address, limit=256):
    if address == 0:
        return ""
    data = memory(address, limit)
    return data.split(b"\0", 1)[0].decode("ascii", errors="strict")


def basename(path):
    return path.replace("\\", "/").rsplit("/", 1)[-1]


def selection(address):
    return decode_start_melee_data(memory(address, 0xF0))


def random_state():
    pointer = word(RNG_POINTER)
    if pointer not in (RNG_DEFAULT,):
        if not 0x80000000 <= pointer <= 0x817FFFFC:
            raise RuntimeError("Retail RNG pointer is outside RAM")
    return word(pointer)


def audio_state():
    current = basename(cstring(CURRENT_HPS, CURRENT_HPS_SIZE))
    voice = word(HPS_VOICE)
    return {
        "active": voice != 0xFFFFFFFF and bool(current),
        "owner_epoch": stream_starts,
        "stream": current,
    }


def emit(row):
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT.open("a") as output:
        output.write(json.dumps(row, separators=(",", ":")) + "\n")


def emit_event(name, route=None, start_address=None):
    global event_index
    row = {
        "record": "event",
        "run": run,
        "index": event_index,
        "event": name,
        "audio": audio_state(),
        "rng": random_state(),
        "retail_audio_diagnostics": {
            "stream_starts": stream_starts,
            "stream_stops": stream_stops,
            "driver_reinitializations": driver_reinitializations,
            "language_bank_initializations": language_bank_initializations,
        },
    }
    if route is not None:
        row["route"] = route
    if start_address:
        row["selection"] = selection(start_address)
    emit(row)
    event_index += 1


def start_address_for(kind):
    if kind.startswith("css_"):
        pointer = css_pointer or word(CSS_POINTER)
        return pointer + 0x10 if pointer else 0
    if kind.startswith("sss_"):
        pointer = sss_pointer or word(SSS_POINTER)
        return pointer + 0x10 if pointer else 0
    if kind == "match_enter_complete":
        return match_pointer
    return 0


def arm(address, kind):
    global current_entry
    if current_entry is not None:
        current_entry.delete()
    current_entry = LifecycleEntry(address, kind)


def next_after(kind, route=None):
    if kind == "css_exit_complete":
        arm(SSS_ENTER, "sss_enter_complete")
    elif kind == "sss_enter_complete":
        arm(SSS_EXIT, "sss_exit_complete")
    elif kind == "sss_exit_complete" and route == "css":
        arm(CSS_ENTER, "css_enter_complete")
    elif kind == "sss_exit_complete" and route == "match":
        arm(MATCH_ENTER, "match_enter_complete")
    elif kind == "css_enter_complete":
        arm(CSS_EXIT, "css_exit_complete")


def install_audio_counters():
    global counter_breakpoints
    if counter_breakpoints:
        raise RuntimeError("Retail audio counters are already installed")
    counter_breakpoints = [
        CounterBreakpoint(STREAM_START, "start"),
        CounterBreakpoint(STREAM_STOP, "stop"),
        CounterBreakpoint(DRIVER_INITIALIZE, "driver_init"),
        CounterBreakpoint(LANGUAGE_BANK_INITIALIZE, "language_bank_init"),
    ]


def remove_audio_counters():
    global counter_breakpoints
    for breakpoint in counter_breakpoints:
        if breakpoint.is_valid():
            breakpoint.delete()
    counter_breakpoints = []


class LifecycleReturn(gdb.Breakpoint):
    def __init__(self, address, kind):
        super().__init__(f"*0x{address:08x}", gdb.BP_HARDWARE_BREAKPOINT,
                         internal=True)
        self.silent = True
        self.kind = kind

    def stop(self):
        global active, current_return, sss_exits
        try:
            route = None
            start_address = start_address_for(self.kind)
            if self.kind == "sss_exit_complete":
                sss_exits += 1
                route = "match" if memory(sss_pointer + 4, 1)[0] else "css"
                expected_route = "css" if sss_exits == 1 else "match"
                if sss_exits > 2 or route != expected_route:
                    raise RuntimeError(
                        f"Expected SSS route {expected_route}, observed {route}")
            emit_event(self.kind, route, start_address if route == "match" or
                       self.kind == "match_enter_complete" else None)
            self.delete()
            current_return = None
            next_after(self.kind, route)
            if self.kind == "match_enter_complete":
                active = False
                print(f"Transition capture run {run} complete: {OUTPUT}; interrupt and run ref-transition off")
        except Exception as error:
            active = False
            if self.is_valid():
                self.delete()
            current_return = None
            emit({"record": "collector_error", "run": run,
                  "event": self.kind, "error": str(error)})
        return False


class LifecycleEntry(gdb.Breakpoint):
    def __init__(self, address, kind):
        super().__init__(f"*0x{address:08x}", gdb.BP_HARDWARE_BREAKPOINT,
                         internal=True)
        self.silent = True
        self.kind = kind

    def stop(self):
        global active, current_entry, current_return
        global css_pointer, sss_pointer, match_pointer
        try:
            argument = int(gdb.parse_and_eval("$r3"))
            if self.kind == "css_enter_complete":
                css_pointer = argument
            elif self.kind == "sss_enter_complete":
                sss_pointer = argument
            elif self.kind == "match_enter_complete":
                match_pointer = argument
            elif self.kind == "css_exit_complete":
                css_pointer = word(CSS_POINTER)
            elif self.kind == "sss_exit_complete":
                sss_pointer = word(SSS_POINTER)
            return_address = int(gdb.parse_and_eval("$lr"))
            self.delete()
            current_entry = None
            current_return = LifecycleReturn(return_address, self.kind)
        except Exception as error:
            active = False
            if self.is_valid():
                self.delete()
            current_entry = None
            emit({"record": "collector_error", "run": run,
                  "event": self.kind, "error": str(error)})
        return False


class CounterBreakpoint(gdb.Breakpoint):
    def __init__(self, address, counter):
        super().__init__(f"*0x{address:08x}", gdb.BP_HARDWARE_BREAKPOINT,
                         internal=True)
        self.silent = True
        self.counter = counter

    def stop(self):
        global stream_starts, stream_stops, driver_reinitializations
        global language_bank_initializations
        if not active:
            return False
        try:
            if self.counter == "start":
                stream_starts += 1
            elif self.counter == "stop":
                stream_stops += 1
            elif self.counter == "driver_init":
                driver_reinitializations += 1
            else:
                language_bank_initializations += 1
        except Exception as error:
            emit({"record": "collector_error", "run": run,
                  "event": f"audio_{self.counter}", "error": str(error)})
        return False


class TransitionCapture(gdb.Command):
    def __init__(self):
        super().__init__("ref-transition", gdb.COMMAND_USER)

    def invoke(self, arguments, from_tty):
        global active, run, event_index, css_pointer, sss_pointer, match_pointer
        global sss_exits, stream_starts, stream_stops, driver_reinitializations
        global language_bank_initializations, current_entry, current_return
        words = arguments.split()
        command = words[0] if words else "status"
        if command == "begin":
            run = int(words[1]) if len(words) > 1 else 0
            if active:
                raise gdb.GdbError("A transition capture is already active")
            if run == 0:
                OUTPUT.parent.mkdir(parents=True, exist_ok=True)
                OUTPUT.write_text(json.dumps({
                    "record": "header", "schema": SCHEMA, "version": VERSION,
                    "producer": "retail", "game_revision": GAME_REVISION,
                    "dol_sha1": DOL_SHA1,
                    "emulator_version": EMULATOR_VERSION,
                    "emulator_commit": EMULATOR_COMMIT,
                    "cpu_core": "Interpreter64", "cpu_thread": False,
                    "fixed_rtc": 1704067200,
                }, separators=(",", ":")) + "\n")
            elif not OUTPUT.exists():
                raise gdb.GdbError("Run zero must create the transition trace first")
            event_index = 0
            css_pointer = word(CSS_POINTER)
            sss_pointer = 0
            match_pointer = 0
            sss_exits = 0
            stream_starts = 0
            stream_stops = 0
            driver_reinitializations = 0
            language_bank_initializations = 0
            initial_audio = audio_state()
            if not initial_audio["active"] or initial_audio["stream"] != "menu01.hps":
                raise gdb.GdbError("Begin capture from a live CSS with menu01.hps active")
            install_audio_counters()
            active = True
            emit_event("capture_begin")
            arm(CSS_EXIT, "css_exit_complete")
            print(f"Transition capture run {run} active: choose SSS, cancel once, then choose a stage")
        elif command == "off":
            active = False
            if current_entry is not None:
                current_entry.delete()
                current_entry = None
            if current_return is not None:
                current_return.delete()
                current_return = None
            remove_audio_counters()
            print("Transition capture disabled")
        elif command == "status":
            print(f"active={active} run={run} events={event_index} output={OUTPUT}")
        else:
            raise gdb.GdbError("Usage: ref-transition begin [RUN] | off | status")


TransitionCapture()
print("Read-only GALE01r2 transition collector installed; use ref-transition begin 0 from CSS")
