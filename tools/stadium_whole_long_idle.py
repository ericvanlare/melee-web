"""Explicit original whole3 B window; no native or per-owner RNG parity claim."""
from stadium_go_prefix import (DRAW_RETURN_PC, StadiumGoPrefixError, _validate_idle_3500_boundary_minimum,
                               _validate_stadium_idle_state_record)
from whole_session_replay import _consumed_ports

PROFILE = {"profile": "selected_b_3500_v1", "selected_matches": [1],
           "comparison_post_go_ticks": 3500, "natural_tail_max": 4,
           "raw_omission_kinds": ["draw_enter", "draw_return"]}


def require(condition, reason):
    if not condition:
        raise StadiumGoPrefixError("whole long idle: " + reason)


class LongIdleBObservations:
    """Consumes every decoded MWRO row, never a reindexed/filtered projection.

    The transport decoder retains CRC/sequence/GPR validation. The ordinary
    whole3 lifecycle/setup/allocation validator remains independently required.
    """
    def __init__(self):
        self.previous = None
        self.markers = []
        self.go = None
        self.last_tick = None
        self.target = False
        self.closed = False
        self.pending = None
        self.last_consume = None
        self.draw_open = None
        self.expected_draw = None
        self.samples = 0
        self.owner = None
        self.close = None

    def accept(self, row):
        seq = row.get("seq")
        require(type(seq) is int and (self.previous is None and seq == 0 or
                self.previous is not None and seq == self.previous + 1), "record sequence gap")
        self.previous = seq
        p = row.get("payload", {})
        event = row.get("event")
        if event in ("handshake", "start"):
            require(p.get("stadium_whole_long_idle") == PROFILE and
                    p.get("whole_session") is True and p.get("match_count") == 3 and
                    p.get("stadium_whole_b_state") is True and p.get("stadium_observed_match") == 1,
                    "profile identity or whole3 owner differs")
            require(event not in self.markers, "duplicate profile announcement")
            self.markers.append(event)
        kind = p.get("boundary")
        if event == "progress" and p.get("phase") == "go_after" and p.get("match_index") == 1:
            require(self.markers == ["handshake", "start"] and self.go is None,
                    "GO lacks unique admitted profile")
            require(type(row.get("source_tick")) is int and row["source_tick"] <= 0xffffffff - 3504,
                    "GO tick exceeds finite range")
            _validate_stadium_idle_state_record(row, "GO", row["source_tick"])
            require(self.last_consume is not None and
                    self.last_consume["source_tick"] == row["source_tick"] and
                    self.last_consume["draw_ordinal"] == row["draw_ordinal"],
                    "GO lacks its containing consumed PAD")
            require(_consumed_ports(self.last_consume, self.last_consume["seq"]) ==
                    ["00"*11, "00"*11, "00"*10+"ff", "00"*10+"ff"],
                    "GO-containing consumed PAD is not literal neutral")
            self.pending = self.last_consume
            self.go = row
            self.expected_draw = row["draw_ordinal"]
        if event != "boundary":
            return
        if kind == "pad_consume":
            self.last_consume = row
        active = self.go is not None and not self.closed
        omission = active and kind in PROFILE["raw_omission_kinds"]
        if kind in PROFILE["raw_omission_kinds"]:
            require(bool(p.get("slices")) != omission, "draw raw omission escaped its selected window")
        if not active:
            return
        require(p.get("whole_session") is True and p.get("match_index") == 1 and
                p.get("audio_owner_epoch") is not None, "foreign whole-session boundary owner")
        require(kind in ("source_tick", "pad_consume", "pad_poll", "draw_enter", "draw_return"),
                "lifecycle exit before target DrawReturn")
        require(row.get("draw_ordinal") == self.expected_draw, "draw ordinal gap")
        if self.draw_open is not None:
            require(kind in ("pad_poll", "draw_return") and row["source_tick"] == self.draw_open,
                    "source work inside draw or changed draw tick")
        if kind in ("source_tick", "pad_poll", "pad_consume"):
            _validate_idle_3500_boundary_minimum(row)
        else:
            pcs = {"draw_enter": 0x80390fc0, "draw_return": DRAW_RETURN_PC}
            require(p.get("pc") == pcs[kind] and isinstance(p.get("gprs"), list) and
                    len(p["gprs"]) == 32 and all(type(v) is int and 0 <= v <= 0xffffffff
                    for v in p["gprs"]), "draw PC/GPR inventory differs")
        if kind == "pad_consume":
            require(self.pending is None and self.draw_open is None, "unpaired consume")
            require(_consumed_ports(row, seq) == ["00"*11, "00"*11, "00"*10+"ff", "00"*10+"ff"],
                    "idle consumed PAD differs from literal neutral")
            self.pending = row
        elif kind == "source_tick":
            require(self.pending is not None and self.pending["source_tick"] == row["source_tick"] and
                    self.pending["draw_ordinal"] == row["draw_ordinal"], "tick lacks exact consumed PAD")
            self.pending = None
            tick = row["source_tick"]
            require(tick == (self.go["source_tick"] if self.last_tick is None else self.last_tick + 1) and
                    0 <= tick - self.go["source_tick"] <= 3504, "tick range/continuity differs")
            state = _validate_stadium_idle_state_record(row, "IDLE", tick)
            roots = tuple((item["tag"], item["flags"], item["hex"]) for item in p["slices"]
                          if item["tag"] in (67, 68))
            if self.owner is None:
                self.owner = roots
                self.countdown = state["map2"]["countdown_s32"]
                self.map2_history = {k:v for k,v in state["map2"].items() if k != "countdown_s32"}
            require(roots == self.owner and
                    {k:v for k,v in state["map2"].items() if k != "countdown_s32"} == self.map2_history and
                    state["map2"]["phase_s16"] == 0 and
                    not state["map2"]["gate_mask_80"] and
                    state["map2"]["countdown_s32"] == self.countdown - self.samples and
                    state["map2"]["countdown_s32"] > 0, "stage owner/countdown differs")
            self.samples += 1
            self.last_tick = tick
            self.target |= tick - self.go["source_tick"] == 3500
        elif kind in ("draw_enter", "draw_return"):
            require(self.pending is None and self.last_tick is not None and
                    row["source_tick"] == self.last_tick + 1, "draw splits consume or tick coordinate differs")
            if kind == "draw_enter":
                require(self.draw_open is None, "duplicate DrawEnter")
                self.draw_open = row["source_tick"]
            else:
                require(self.draw_open is not None, "DrawReturn lacks DrawEnter")
                self.draw_open = None
                self.expected_draw += 1
                if self.target:
                    self.closed = True
                    self.close = {"sequence": seq, "source_tick": row["source_tick"],
                                  "draw_ordinal": row["draw_ordinal"]}

    def finish(self):
        require(self.markers == ["handshake", "start"] and self.closed and self.target and
                self.pending is None and self.draw_open is None and 3501 <= self.samples <= 3505,
                "missing complete target/natural DrawReturn")
        return {"scope": "original-whole3-selected-b-long-idle-source-observation-only",
                "profile": PROFILE, "go_sequence": self.go["seq"],
                "go_tick": self.go["source_tick"], "state_samples": self.samples,
                "natural_tail_ticks": self.samples - 3501, "close": self.close,
                "whole3_lifecycle_required_separately": True, "native_parity": False,
                "per_owner_rng_parity": False, "c3_complete": False}
