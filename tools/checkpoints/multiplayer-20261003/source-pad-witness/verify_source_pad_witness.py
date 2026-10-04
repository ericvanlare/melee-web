#!/usr/bin/env python3
"""Static/mock controls for the unapplied source PAD application witness.

This reads the current checkout and the candidate fragment. It does not build,
launch Node/Wasm, start a browser, or run native source code.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

EXPECTED = {
    "src/gameplay_menu_browser.cpp": "2ba24a79b9c6b5693371368f48603a1eb74c600bd26f09f7926ec5d0453f135e",
    "src/gameplay_match_context.c": "9be4d068d8d853b70a86f091610b3df164146fb74c686f0e39dd445648cc7598",
    "src/gameplay_match_context.h": "daddc4378129ae7f9f9a9fd18270d0c1a892fccea8815b8514a60d1ec1b6f52f",
    "src/gameplay_match_session.hpp": "350e656b608e761f0259c95842faad88d1833a988bf5254b9891f51ef10b3365",
    "src/browser_input.h": "e394c96e9b5492d63baf740e6995901bc4354c5cf9c50d789d74e7e5beb89945",
    "src/browser_input.cpp": "8ea946edfcd62429c67dc77a59a8c2ee506e0240bf807aa4557f6e4a113be2c8",
}

class WitnessError(AssertionError):
    pass

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def require(cond: bool, message: str) -> None:
    if not cond:
        raise WitnessError(message)

def validate_row(row: dict, *, ordinary: bool = True) -> None:
    """Schema gate used by the focused mock controls, not runtime evidence."""
    require(row.get("host_sample", 0) > 0, "host sample ID is required")
    before, after = row.get("source_frame_before"), row.get("source_frame_after")
    require(isinstance(before, int) and isinstance(after, int), "source frame pair is required")
    require(after > before, "source frame did not advance")
    if ordinary:
        require(after == before + 1, "ordinary source step is not exactly one frame")
    raw = row.get("raw")
    require(isinstance(raw, list) and len(raw) == 4, "final four-port raw sample is required")
    consumers = row.get("consumers")
    require(isinstance(consumers, list) and consumers, "post-step consumers are required")
    for c in consumers:
        require(c.get("consumer_kind") == "Fighter::input", "consumer identity is not Fighter::input")
        require(isinstance(c.get("player_slot"), int), "source player slot missing")
        require(isinstance(c.get("controller_port"), int), "source controller mapping missing")
        require(len(c.get("source_stick", [])) == 2, "source stick missing")
        for key in ("source_triggers", "held_buttons", "pressed_buttons", "released_buttons"):
            require(key in c, f"source consumer field missing: {key}")
    require(row.get("step_returned") is True, "step did not return successfully")

def controls() -> dict:
    positive = {
        "host_sample": 12,
        "source_frame_before": 125,
        "source_frame_after": 126,
        "raw": [{"button": 0, "stickX": 64, "stickY": 0}, {}, {}, {}],
        "consumers": [{"consumer_kind": "Fighter::input", "player_slot": 0,
                       "controller_port": 0, "source_stick": [0.5, 0.0],
                       "source_triggers": 0.0, "held_buttons": 0,
                       "pressed_buttons": 0, "released_buttons": 0}],
        "step_returned": True,
    }
    validate_row(positive)
    negative = {
        "host_sample_only": {"host_sample": 12, "raw": positive["raw"]},
        "queue_only": {"host_sample": 12, "source_frame_before": 125,
                        "source_frame_after": 126, "raw": positive["raw"],
                        "step_returned": True},
        "missing_frame": {k: v for k, v in positive.items() if k != "source_frame_after"},
        "mutated_sample": dict(positive, raw=[{"button": 1}, {}, {}, {}],
                                recorded_raw=[{"button": 0}, {}, {}, {}]),
        "failed_step": dict(positive, step_returned=False),
    }
    failures = {}
    for name, row in negative.items():
        try:
            if name == "mutated_sample":
                require(row["raw"] == row["recorded_raw"], "recorded sample was mutated")
            validate_row(row)
        except WitnessError:
            failures[name] = True
    require(set(failures) == set(negative), "a queue/host/missing/failure negative control was accepted")
    return {"positive": True, "negative_refusals": sorted(failures)}

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkout", type=Path, required=True)
    parser.add_argument("--packet", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()
    checkout = args.checkout
    source = (checkout / "src/gameplay_menu_browser.cpp").read_text()
    context = (checkout / "src/gameplay_match_context.c").read_text()
    header = (checkout / "src/gameplay_match_context.h").read_text()
    session = (checkout / "src/gameplay_match_session.hpp").read_text()
    browser_input = (checkout / "src/browser_input.cpp").read_text()
    fragment = (args.packet / "observer_fragment.inc").read_text()
    for relative, expected in EXPECTED.items():
        actual = sha(checkout / relative)
        require(actual == expected, f"source changed: {relative} {actual}")
    p_input = source.index('const auto* input=melee_web_input_poll();')
    p_before = source.index('source_frames.before_step(present_source);')
    p_checked = source.index('PADStatus checked_input[4];', p_before)
    p_match = source.index('if(match){', p_checked)
    p_tick = source.index('match->tick(sample);', p_match)
    positions = [p_input, p_before, p_checked, p_match, p_tick]
    require(positions == sorted(positions), f"source input/tick order changed: {positions}")
    tick = source.index('match->tick(sample);')
    require(source.find('match->player_stats(0)', tick) > tick, "no post-tick source consumer read")
    require(source.count('match->tick(sample);') == 1, "candidate assumes a unique match tick call")
    for token in ('uint64_t samples;', 'state.samples', '++state.samples'):
        require(token in ((checkout / 'src/browser_input.h').read_text() + browser_input),
                f"host sample contract missing: {token}")
    context_order = [
        'memset(&h->input_queue,0,sizeof(h->input_queue));',
        'h->input_queue.stat[h->slots[i]]=raw[h->controllers[i]];',
        'HSD_PadLibData.qcount=1;',
        'HSD_PadRumbleInterpret();',
        'HSD_PadRenewMasterStatus();',
        'if(HSD_PadLibData.qcount)return fail',
        'if(!melee_web_gameplay_step(e,n))return 0;',
        'h->ticks++;',
    ]
    cpositions = [context.index(token) for token in context_order]
    require(cpositions == sorted(cpositions), f"raw source PAD order changed: {cpositions}")
    for token in ('source_stick[0]', 'source_stick[1]', 'source_triggers',
                  'held_buttons', 'pressed_buttons', 'released_buttons'):
        require(token in context, f"source Fighter consumer field missing: {token}")
    for token in ('input->samples', 'source_frame_before', 'final_sample',
                  'match.player_stats', 'step_returned', 'Fighter::input'):
        require(token in fragment, f"observer fragment missing {token}")
    p_host = fragment.index('input->samples')
    p_fragment_tick = fragment.index('match->tick(sample);', p_host)
    p_fragment_consumer = fragment.index('source_pad_application_trace::finish(', p_fragment_tick)
    require(p_host < p_fragment_tick, "host sample is not captured before source tick")
    require(p_fragment_tick < p_fragment_consumer, "source consumer is not read after source tick")
    require('match->tick(sample);' in fragment and '#else' in fragment,
            "normal target fallback is missing")
    result = {"source_hashes": EXPECTED, "static_controls": controls(),
              "normal_runtime": "unrun", "build": "unrun", "browser": "unrun"}
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
