"""Separate ordinary terminal admission beside unchanged fixed-plan receivers."""
from original_competitive_timeout import CAPS, POLICY_SHA256, TimeoutInventory, live_fighters
from sd_reference_diagnostic import GciRulesMenuReceiver, Receiver, PCS, slices, require


class OrdinaryTimeoutReceiver(GciRulesMenuReceiver):
    def __init__(self, plan, profile):
        super().__init__(plan,profile,full_route=True,guarded_items=True,competitive_entry=True)
        self.phase_order = ("vs_entry","vs_setup","vs_exit","vs_retired")
        self.ordinary = TimeoutInventory()
        self.setup_samples = 0
        self.serialized_bytes = 0
        self.terminal = None

    def accept(self, row):
        # Count the exact original length-delimited record, not a re-encoding.
        # The runner supplies this from ObserverTail.offset before admission.
        require(self.seq < CAPS["observer_records"], "Ordinary observer record cap")
        payload = row["payload"]
        if row["event"] == "handshake":
            require(payload.get("menu_probe") == "ordinary_timeout" and
                    payload.get("ordinary_policy_sha256") == POLICY_SHA256,
                    "Ordinary policy/scope handshake differs")
            forwarded = dict(row,payload=dict(payload,menu_probe="competitive_entry"))
            return super().accept(forwarded)
        name = payload.get("name")
        if self.order < 2 or name == "vs_setup":
            result = super().accept(row)
            if name == "input":
                self.setup_samples += 1
                require(self.setup_samples <= CAPS["setup_samples"], "Ordinary setup PAD cap")
            if name == "vs_setup":
                self.ordinary.setup_samples=self.setup_samples
            return result
        require(not self.ended and row["seq"]==self.seq, "Ordinary sequence/trailing event")
        self.seq += 1
        if row["event"] == "end":
            require(self.order==4 and self.ordinary.phase=="retired" and
                    payload=={"status":"interrupted","natural":False}, "Ordinary ending before retirement")
            self.ended=True
            return
        require(row["event"]=="progress" and payload.get("diagnostic")=="sd_initialization_prefix" and
                name in ("input","tick","vs_exit","vs_retired") and payload["pc"]==PCS[name],
                "Ordinary event/scope/PC differs")
        data=slices(payload)
        wanted={(3,0)} if name=="input" else set() if name=="vs_retired" else {
            (14,0),(40,0),(17,0),*[(tag,slot) for slot in range(2) for tag in (5,7,8,52,53)]}
        if name=="vs_exit":wanted.add((15,0))
        require(set(data)==wanted,"Ordinary typed slice inventory differs")
        require(payload.get("menu_consumed")==self.menu_consumed and
                type(payload.get("consumed")) is int, "Ordinary counters differ")
        if name == "input":
            require(self.order==2 and payload["consumed"]==self.consumed+1 and
                    self.consumed < CAPS["total_source_samples"], "Ordinary PAD order/total cap")
            self.ordinary.input(data.get((3,0),b"")); self.consumed+=1
            return
        require(payload["consumed"]==self.consumed, "Ordinary skipped actual input")
        if name in ("tick","vs_exit"):
            require(self.order==2, "Ordinary live observation after exit")
            addresses={(v["tag"],v["flags"]):v["address"] for v in payload["slices"]}
            require(addresses[(14,0)]==0x8046b6a0 and addresses[(17,0)]==0x80479d30 and
                    (name!="vs_exit" or addresses[(15,0)]==0x80479da4),
                    "Ordinary clock/routing/publication source address differs")
            require(data.get((40,0))==b"\2" and len(data.get((17,0),b""))==6 and
                    data[(17,0)][0]==2, "Ordinary live source scene owner differs")
            live=live_fighters(data,payload)
            clock=self.clock(data)
            if name=="tick":
                self.ordinary.tick(row["source_tick"],clock,live)
            else:
                self.ordinary.exit(row["source_tick"],clock,live,data.get((15,0),b""))
                self.order=3; self.records[name]=data
                self.terminal=dict(source_tick=row["source_tick"],clock=list(clock),live=live,
                                   match_end_hex=data[(15,0)].hex(),seq=row["seq"])
        else:
            require(self.order==3 and type(row["source_tick"]) is int and
                    row["source_tick"]==self.ordinary.ticks and not data,
                    "Ordinary retirement order/counter/slices")
            self.retirement_counter=row["source_tick"]  # arena lifecycle output, not active advancement
            self.ordinary.retire(); self.order=4; self.records[name]=data

    def finish(self, observer_status, input_path, input_status):
        report=Receiver.finish(self,observer_status,input_path,input_status)
        require(input_path.stat().st_size<=CAPS["input_bytes"], "Ordinary native input byte cap")
        report.update(schema="melee-web-original-competitive-natural-timeout",scope="ordinary_timeout_gci",
                      policy_sha256=POLICY_SHA256, loaded_context=self.loaded_context,
                      menu_source_samples=self.menu_consumed,menu_polls=self.menu_polls,
                      setup_samples=self.setup_samples,active_samples=self.ordinary.active_samples,
                      total_source_samples=self.consumed,total_source_sample_cap=CAPS["total_source_samples"],
                      scheduled_records=self.ordinary.ticks,directional_samples=self.ordinary.directional,
                      first_loss=self.ordinary.first_loss,terminal=self.terminal,
                      retirement_counter=self.retirement_counter,
                      native_input_completion="complete-declared-terminal-prefix",
                      natural_timeout_admission=True,results_css_admission=False,
                      source_inventory={"vs":{"count":self.ordinary.ticks,"scope":"natural-terminal-retirement"}})
        return report
