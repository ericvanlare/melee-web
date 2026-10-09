"""Explicit adaptive original timeout policy; fixed v4 remains entry provenance.

No missing PAD samples are authored by this module. The policy accepts actual
copied PAD and independently observed scheduled iterations, not host time.
"""
import hashlib
import json
import struct
from authored_sd_reference_plan import make_input_plan
from sd_original_menu_plan import gci_competitive_entry_packet

SCOPE = "ordinary_timeout_gci"
CAPS = dict(wall_seconds=600, setup_samples=123, total_source_samples=29523,
            menu_samples=7200, menu_polls=7200, scheduled_records=29523,
            directional_samples=600, observer_records=73500,
            observer_bytes=128*1024*1024, input_bytes=64*1024*1024,
            intent_bytes=16*1024*1024, intent_records=16000,
            log_bytes=16*1024*1024)


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def policy():
    return dict(schema="original-competitive-natural-timeout-policy", version=1,
                provenance="authored-development-workload", scope=SCOPE,
                entry_recipe_sha256=make_input_plan(6)["authored_recipe_sha256"],
                menu_sha256=hashlib.sha256(canonical(gci_competitive_entry_packet())).hexdigest(),
                caps=CAPS, timeout_frames=28800, timeout_seconds=480,
                ready_frame=180, ready_x_bits=["c2700000", "42700000"],
                direction_x=-80, terminal_stocks=[3,4], winner=1,
                phases=["neutral-ready", "first-loss", "held-bank-drain", "neutral-timeout", "exit", "retired"],
                exclusions=["Results-confirmations", "PAD-schedule-equality", "RNG", "pixels", "PCM", "timing"])


POLICY_SHA256 = hashlib.sha256(canonical(policy())).hexdigest()


def serialized_budget():
    """Native-enforced JSON ceilings plus every 44-byte MWRO header.

    Menu includes boot/Rules/Items/CSS/SSS. Rules-ready and VS entry carry
    full profile slices once each. No one-PAD-per-frame assumption is used.
    The two writer-owned error/end records are included even on failure.
    """
    inventory = {"menu":(7200,6144), "menu_input":(7200,512),
                 "input":(29523,512), "tick":(29523,2048),
                 "rules_ready":(1,65536), "vs_entry":(1,65536),
                 "vs_setup":(1,4096), "vs_exit":(1,8192), "vs_retired":(1,512),
                 "handshake_start_error_end":(4,4096)}
    records=sum(count for count,_ in inventory.values())
    size=sum(count*(44+payload) for count,payload in inventory.values())
    assert records<=CAPS["observer_records"] and size<=CAPS["observer_bytes"]
    # MWRI: 40-byte record = existing native header plus exact 11-byte PAD.
    # Maximum four-port records for each actual menu/setup/active bank, plus
    # the independent scheduled bound, header/footer. Keep native64MiB too.
    return dict(inventory=inventory, observer_records=records,observer_bytes=size,
                intent_bytes=CAPS["intent_records"]*512,
                native_mwri_records=1677721,native_mwri_bytes=16+1677721*40,
                native_mwri_scope="independent PADRead+scheduled-snapshot producer cap, including footer; no bank/frame ratio assumption")


def load_policy(path):
    raw = path.read_bytes()
    if raw != canonical(policy()):
        raise ValueError("Ordinary timeout policy differs from its canonical scoped identity")
    return policy(), POLICY_SHA256


class TimeoutInventory:
    """Independent source/PAD state machine, shared by receiver controls."""
    def __init__(self):
        self.phase = "neutral-ready"
        self.frame = 0
        self.ticks = 0
        self.last_consumed = 0
        self.active_samples = 0
        self.setup_samples = 0
        self.directional = 0
        self.first_loss = None
        self.neutral_after_loss = False
        self.last_live = None
        self.previous_direction = False

    @staticmethod
    def check(ok, message):
        if not ok:
            raise ValueError("Ordinary timeout: " + message)

    def input(self, raw):
        self.check(self.phase not in ("exit", "retired"), "input after terminal publication")
        self.check(len(raw) == 48, "copied PAD size")
        banks = [raw[p:p+11] for p in range(0,48,12)]
        neutral = [b"\0"*11]*2 + [b"\0"*10+b"\xff"]*2
        direction = list(neutral)
        direction[0] = b"\0\0\xb0" + b"\0"*8
        self.check(banks in (neutral, direction), "undeclared PAD bank")
        self.check(not (banks == neutral and self.phase == "first-loss" and self.directional > 0),
                   "neutral/restart before observed first loss")
        self.active_samples += 1
        self.check(0 <= self.setup_samples <= CAPS["setup_samples"] and
                   self.active_samples + self.setup_samples <= CAPS["total_source_samples"],
                   "total source PAD cap (setup plus active)")
        if banks == direction:
            self.check(self.phase in ("first-loss", "held-bank-drain") and
                       not self.neutral_after_loss, "direction before ready or after release")
            self.directional += 1
            self.check(self.directional <= CAPS["directional_samples"], "first-loss/drain cap")
        elif self.phase == "held-bank-drain":
            self.neutral_after_loss = True
            self.phase = "neutral-timeout"
        self.previous_direction = banks == direction

    def tick(self, counter, clock, live):
        self.check(self.phase not in ("exit", "retired"), "tick after exit")
        self.check(type(counter) is int and counter == self.ticks and
                   self.ticks < CAPS["scheduled_records"], "scheduled counter/cap")
        self.check(self.active_samples > self.last_consumed, "iteration lacks actual PAD consumption")
        frame, seconds, sub = clock
        self.check(self.ticks != 0 or frame == 0, "first scheduled frame must be original frame zero")
        self.check(frame in (self.frame,self.frame+1) and frame <= 28800 and
                   seconds == max(0,480-(frame+59)//60) and sub == (frame+59)%60,
                   "clock advancement/countdown")
        self.check(live["stocks"] in ([4,4],[3,4]) and live["damage_bits"] == ["00000000"]*2,
                   "unexpected live stock/damage")
        if self.phase == "neutral-ready":
            self.check(live["stocks"] == [4,4] and frame <= 180, "loss or skipped ready boundary")
            if frame == 180:
                self.check(live["x_bits"] == ["c2700000","42700000"], "ready source positions")
                self.phase = "first-loss"
        elif live["stocks"] == [3,4] and self.first_loss is None:
            self.check(self.phase == "first-loss" and self.directional > 0 and self.previous_direction,
                       "first loss lacks prior held directional bank")
            self.first_loss = dict(counter=counter, frame=frame, directional_samples=self.directional)
            self.phase = "held-bank-drain"
        elif self.first_loss is not None:
            self.check(live["stocks"] == [3,4], "stock resurrection/second loss")
        self.frame = frame
        self.last_live = live
        self.ticks += 1
        self.last_consumed = self.active_samples

    def exit(self, counter, clock, live, result):
        self.check(self.phase == "neutral-timeout" and self.first_loss is not None and
                   counter == self.ticks and self.frame == 28800 and clock == (28800,0,59),
                   "premature/missing natural timeout")
        self.check(live["stocks"] == [3,4] and live["damage_bits"] == ["00000000"]*2,
                   "live terminal stocks/damage")
        self.check(len(result)==0x448 and result[4:7]==bytes((1,1,0)) and
                   int.from_bytes(result[8:12],"big")==28800 and result[13]==1 and
                   result[16]==1, "canonical timeout/winner")
        for slot, base in enumerate((0x58,0x100)):
            self.check(result[base]==0 and result[base+1]==8 and
                       result[base+3]>>2 == (1,0)[slot] and result[base+5]==0 and
                       result[base+8] == (3,4)[slot] and result[base+12:base+14]==b"\0\0",
                       "canonical participant/stock/damage")
        self.check(all(result[0x58+slot*0xa8]==3 for slot in range(2,6)),
                   "canonical inactive roster differs")
        self.phase = "exit"
        self.last_live = live

    def retire(self):
        self.check(self.phase == "exit", "retirement before canonical exit")
        self.phase = "retired"


def live_fighters(data, payload):
    """Verify exact StaticPlayer/GObj ownership before decoding minimal heads."""
    addresses = {(s["tag"],s["flags"]):s["address"] for s in payload["slices"]}
    stocks, damage, x = [], [], []
    for slot in range(2):
        head, pair, link = data.get((5,slot),b""), data.get((52,slot),b""), data.get((53,slot),b"")
        # Tags are checked against the canonical enum by focused controls.
        TimeoutInventory.check(len(head)==256 and head[12]==slot and head[4:8]==b"\0"*4,
                               "fighter source identity")
        TimeoutInventory.check(len(pair)==8 and pair[4:]==b"\0"*4 and int.from_bytes(pair[:4],"big")!=0 and
            len(link)==4 and addresses.get((52,slot))==0x80453080+slot*0xe90+0xb0 and
            addresses.get((53,slot))==int.from_bytes(pair[:4],"big")+0x2c and
            int.from_bytes(link,"big")==addresses.get((5,slot)), "fighter entity ownership")
        pointer=addresses[(5,slot)]
        TimeoutInventory.check(addresses.get((7,slot))==pointer+0x1830 and len(data.get((7,slot),b""))==4 and
            addresses.get((8,slot))==0x80453080+slot*0xe90+0x8e and len(data.get((8,slot),b""))==1,
            "live stock/damage source addresses")
        stocks.append(data[(8,slot)][0]); damage.append(data[(7,slot)].hex()); x.append(head[0xb0:0xb4].hex())
    return dict(stocks=stocks,damage_bits=damage,x_bits=x)
