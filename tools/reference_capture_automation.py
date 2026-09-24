"""Controller-only automation for exercising the same retail app capture path.

The observer never calls this module. It writes ordinary Dolphin Pipe input,
uses host time, and records intentions separately from actual observed PAD polls.
It cannot write retail memory or supply CPU-generated controller decisions.
"""
import configparser
import json
import os
from pathlib import Path
import struct
import sys
import time

from retail_input_plan import BUTTONS, NEUTRAL_PAD, load_plan, pipe_commands


def wait_for_match_initial(raw_path, stop_event, *, timeout=600):
    """Tail validated observer frames to schedule the human input plan.

    Only the ordinary match-construction boundary controls this scheduler;
    fighter values and CPU decisions never influence the supplied input.
    The capture's independent finalizer still validates the complete stream.
    """
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "reference-capture/dolphin"))
    from reference_observer_stream import HEADER, MAX_PAYLOAD, _decode_record
    deadline, expected, offset = time.monotonic() + timeout, 0, 0
    while not Path(raw_path).exists():
        if time.monotonic() >= deadline or stop_event.wait(0.02):
            raise TimeoutError("No observer stream before automation deadline")
    with Path(raw_path).open("rb") as stream:
        while time.monotonic() < deadline:
            stream.seek(offset)
            header_bytes = stream.read(HEADER.size)
            if len(header_bytes) == HEADER.size:
                header = HEADER.unpack(header_bytes)
                if header[8] > MAX_PAYLOAD:
                    raise ValueError("Observer payload exceeds automation bound")
                payload = stream.read(header[8])
                if len(payload) == header[8]:
                    row = _decode_record(header, payload, f"automation record {expected}")
                    if row["seq"] != expected:
                        raise ValueError("Observer sequence gap before automated input")
                    expected += 1
                    offset = stream.tell()
                    if row["event"] in ("error", "end"):
                        raise ValueError("Observer ended before ordinary match construction")
                    if row["payload"].get("boundary") == "setup":
                        return {"observer_seq": row["seq"], "timestamp_ns": row["timestamp_ns"]}
                    continue
            if stop_event.wait(0.02):
                raise TimeoutError("Automated input canceled")
    raise TimeoutError("No ordinary match-construction boundary before automation deadline")


def prepare_pipe(user):
    pipes = Path(user) / "Pipes"
    pipes.mkdir()
    os.mkfifo(pipes / "pad1", 0o600)
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config["GCPad1"] = {"Device": "Pipe/0/pad1"}
    pad = config["GCPad1"]
    for _, name in BUTTONS:
        key = "D-Pad/" + name[2:].title() if name.startswith("D_") else (
            "Triggers/" + name if name in ("L", "R") else "Buttons/" + ("Start" if name == "START" else name))
        pad[key] = "`Button " + name + "`"
    for stick, axis in (("Main Stick", "MAIN"), ("C-Stick", "C")):
        for direction, component in (("Up", "Y +"), ("Down", "Y -"), ("Left", "X -"), ("Right", "X +")):
            pad[stick + "/" + direction] = "`Axis " + axis + " " + component + "`"
        for setting in ("Calibration", "Center", "Modifier"):
            pad[stick + "/" + setting] = ""
        pad[stick + "/Dead Zone"] = "0"
        pad[stick + "/Virtual Notches"] = "0"
    for trigger in ("L", "R"):
        pad["Triggers/" + trigger + "-Analog"] = "`Axis " + trigger + " +`"
    pad["Triggers/Dead Zone"] = "0"
    pad["Triggers/Threshold"] = "90"
    target = Path(user) / "Config/GCPadNew.ini"
    target.chmod(0o600)
    with target.open("w") as stream: config.write(stream)
    target.chmod(0o400)
    target = Path(user) / "Config/Dolphin.ini"
    core = configparser.ConfigParser(interpolation=None)
    core.optionxform = str
    core.read(target)
    core.set("Core", "SIDevice0", "6")
    target.chmod(0o600)
    with target.open("w") as stream: core.write(stream)
    target.chmod(0o400)
    return pipes / "pad1"


class PipeController:
    def __init__(self, fifo, log):
        self.fifo, self.log = Path(fifo), Path(log)

    def write(self, pad):
        payload = pipe_commands(pad)
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        try:
            # One Pipe packet stays below PIPE_BUF, so a short write or full
            # pipe is a failed test attempt, never silently retried/reordered.
            if os.write(fd, payload) != len(payload):
                raise OSError("Incomplete controller pipe packet")
        finally:
            os.close(fd)
        with self.log.open("a") as stream:
            stream.write(json.dumps({"host_monotonic_ns": time.monotonic_ns(),
                                     "human_port": 0, "intended_pad": pad}) + "\n")

    def pulse(self, *, buttons=0, x=0, y=0, seconds=0.12):
        self.write(struct.pack(">HbbbbBBBBb", buttons, x, y, 0, 0, 0, 0, 0, 0, 0).hex())
        time.sleep(seconds)
        self.write(NEUTRAL_PAD)

    def play_plan(self, path, stop_event):
        plan, _plan_sha256 = load_plan(path)
        if plan.get("controlled_ports") != [1]:
            raise ValueError("Automation only supplies human controller port 1")
        started = time.monotonic()
        for index, vector in enumerate(plan["frames"]):
            if stop_event.wait(max(0, started + index / 60 - time.monotonic())):
                # Release every held control before abandoning a scheduled
                # workload.  A cancelled host-side plan must not leave a
                # button or axis latched in Dolphin's ordinary Pipe device.
                self.write(NEUTRAL_PAD)
                return
            self.write(vector[0])
        self.write(NEUTRAL_PAD)
