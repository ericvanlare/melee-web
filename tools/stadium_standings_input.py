"""Explicit host standings input; same native structural long-window layout. No parity claim."""
from retail_input_plan import NEUTRAL_PAD, DISCONNECTED_PAD
from sd_reference_diagnostic import require

class StadiumStandingsInputPolicy:
    """Host ordinary-input policy; source snapshots are read-only operands.

    No idle ledger is admitted. An issued stick may remain in the existing
    consumed queue until its neutral release is observed; this does not grant
    permission to issue stick input for another/unobserved focus.
    """
    PROFILE = 'selected_b_mode8_horizontal_standings_v1'

    @staticmethod
    def validate_setup(expected):
        players = expected.get('players')
        require(isinstance(players, list) and len(players) == 2 and
                [p.get('port') for p in players] == [1, 2] and
                all(type(p.get(k)) is int and p[k] == value
                    for p in players for k, value in
                    (('player_type', 0), ('character_kind', 8), ('stocks', 4))),
                'Standings input requires exact contiguous two-human Mario stock4 controllers')

    def __init__(self):
        self.last = None
        self.go_tick = None
        self.initial_form = None
        self.stop = None
        self.transitions = []
        self.desired = [NEUTRAL_PAD, NEUTRAL_PAD]
        self.inflight = None
        self.releasing = False
        self.consumed_ticks = 0

    def allowed(self, ports):
        return (ports == [NEUTRAL_PAD, NEUTRAL_PAD] or
                self.inflight is not None and ports == self.inflight)

    def issue(self, ports):
        from reference_versus_sequence_capture import raw_pad
        neutral = [NEUTRAL_PAD, NEUTRAL_PAD]
        require(ports == self.desired, 'Standings input differs from actual observed policy')
        if ports == neutral:
            self.releasing = self.inflight is not None
            return
        require(self.stop is None and self.last is not None and
                self.last['mode'] == 8 and self.last['focus'] in (0, 1) and
                self.inflight is None and not self.releasing,
                'Standings stick lacks settled source-focused mode8 owner')
        expected = neutral.copy()
        expected[self.last['focus']] = raw_pad(x=80)
        require(ports == expected, 'Standings stick controller differs from actual focus')
        self.inflight = list(ports)

    def observe(self, row, snapshot, sample, go):
        from reference_versus_sequence_capture import raw_pad
        require(sample is not None and sample['sequence'] < row['seq'] and
                sample['match_index'] == row['payload']['match_index'] == 1 and
                sample['source_tick'] == row['source_tick'] and
                sample['draw_ordinal'] == row['draw_ordinal'] and
                sample['errors'] == [0, 0, -1, -1] and
                sample['ports'][2:] == [DISCONNECTED_PAD]*2 and
                self.allowed(sample['ports'][:2]),
                'Standings tick lacks exact four-port consumed input owner')
        if self.releasing and sample['ports'][:2] == [NEUTRAL_PAD]*2:
            self.inflight, self.releasing = None, False
        if self.stop is not None:
            return
        tick = row['source_tick']
        require(type(go.get('source_tick')) is int and
                (self.last is None and tick == go['source_tick'] or
                 self.last is not None and tick == self.last['source_tick'] + 1),
                'Standings source tick is not contiguous from actual GO')
        require(snapshot['sequence'] == row['seq'] and snapshot['source_tick'] == tick and
                snapshot['draw_ordinal'] == row['draw_ordinal'],
                'Standings typed snapshot coordinates differ')
        if self.go_tick is None:
            self.go_tick = go['source_tick']
            self.initial_form = snapshot['map2']['form_s16']
        display = snapshot['display']
        mode, focus = display['mode_s16'], display['focused_player_s16']
        if mode == 8:
            require(type(focus) is int and focus in (0, 1),
                    'Standings mode8 focus is not a declared human source slot')
            # Actual AddMatchSlices/AddPlayerEntitySlices already checks the
            # live StaticPlayer->GObj->Fighter links. Recheck the captured typed
            # FighterHead identity against the declared contiguous human slot.
            heads = [x for x in row['payload']['slices']
                     if x.get('name') == 'fighter_head' and x.get('flags') == focus]
            require(len(heads) == 1 and heads[0].get('size') == 0x100,
                    'Standings focus lacks one actual FighterHead')
            head = bytes.fromhex(heads[0]['hex'])
            require(len(head) == 0x100 and head[12] == focus and
                    int.from_bytes(head[4:8], 'big') == 0,
                    'Standings focus FighterHead slot/Mario kind differs')
        point = dict(sequence=row['seq'], source_tick=tick, draw_ordinal=row['draw_ordinal'],
                     mode=mode, focus=focus)
        if self.last is None or (mode, focus) != (self.last['mode'], self.last['focus']):
            self.transitions.append(point.copy())
        self.last = point
        self.consumed_ticks += 1
        transformed = (snapshot['map2']['phase_s16'] != 0 or
                       snapshot['map2']['gate_mask_80'] or
                       snapshot['map2']['form_s16'] != self.initial_form)
        reason = ('first_transform' if transformed else 'observed_mode14' if mode == 14 else
                  'go_plus_3500_cap' if tick-self.go_tick >= 3500 else None)
        self.desired = [NEUTRAL_PAD, NEUTRAL_PAD]
        if reason is not None:
            self.stop = dict(point, reason=reason,
                result='OBSERVED_PRETRANSFORM_MODE14' if reason == 'observed_mode14' else
                       'INCONCLUSIVE_PRETRANSFORM', C3=False, per_owner_rng_comparison=False)
        elif mode == 8 and not self.releasing:
            desired = [NEUTRAL_PAD, NEUTRAL_PAD]
            desired[focus] = raw_pad(x=80)
            # A focus change first releases the previous source-owned command.
            if self.inflight is None or self.inflight == desired:
                self.desired = desired

    def summary(self):
        require(self.stop is not None and self.inflight is None and not self.releasing,
                'Standings evidence stop/release has not settled')
        return dict(profile=self.PROFILE, stop=dict(self.stop),
                    transitions=list(self.transitions), consumed_ticks=self.consumed_ticks,
                    neutral_release_observed=True, allocation_acceptance=False,
                    per_owner_rng_comparison=False, C3=False)


from pathlib import Path
import json
from stadium_whole_long_idle import LongIdleBObservations, PROFILE
from whole_session_replay import _consumed_ports


def read_standings_intents(output):
    """Bounded owned metadata plus the existing successful Pipe write journal."""
    root = Path(output)
    values = []
    for name, cap, lines in (('standings-input-issues.json', 1024*1024, False),
                             ('input-intentions.jsonl', 1024*1024, True)):
        path = root/name
        require(path.is_file() and not path.is_symlink() and path.stat().st_size <= cap,
                'Standings intent metadata missing/nonregular/over cap')
        raw = path.read_text()
        value = [json.loads(line) for line in raw.splitlines()] if lines else json.loads(raw)
        require(isinstance(value, list) and len(value) <= 4096,
                'Standings intent record inventory differs')
        values.append(value)
    return tuple(values)


class StandingsBObservations(LongIdleBObservations):
    """Explicit host input policy over the unchanged native structural layout.

    The default LongIdleBObservations still requires literal neutral and an
    untransformed map. This subclass grants no idle/C4/owner-parity acceptance.
    """
    def __init__(self, issues, pipe_rows):
        super().__init__(PROFILE)
        require(isinstance(issues, list) and len(issues) <= 4096 and
                isinstance(pipe_rows, list) and len(pipe_rows) <= 4096,
                'Standings issue/Pipe inventory differs')
        self.policy = StadiumStandingsInputPolicy()
        self.issues = issues
        self.issue_cursor = 0
        covered = set()
        previous_sequence = previous_pipe = -1
        keys = {'schema', 'index', 'source', 'after_sequence', 'ports', 'action', 'pipe_row_indices'}
        for index, issue in enumerate(issues):
            require(isinstance(issue, dict) and set(issue) == keys and
                    issue['schema'] == 'melee-web-stadium-standings-input-issue-v1' and
                    issue['index'] == index and type(issue['after_sequence']) is int and
                    issue['after_sequence'] > previous_sequence and
                    isinstance(issue['source'], dict) and
                    set(issue['source']) == {'sequence', 'source_tick', 'draw_ordinal', 'mode', 'focus'} and
                    issue['source']['sequence'] == issue['after_sequence'] and
                    isinstance(issue['ports'], list) and len(issue['ports']) == 2,
                    'Standings issue schema/source order differs')
            positions = issue['pipe_row_indices']
            require(isinstance(positions, list) and len(positions) == 2 and
                    all(type(i) is int for i in positions) and
                    positions[0] > previous_pipe and positions[1] == positions[0]+1 and
                    positions[1] < len(pipe_rows), 'Standings issue lacks ordered adjacent Pipe rows')
            expected_action = ('standings-mode8:release' if issue['ports'] == [NEUTRAL_PAD]*2
                               else 'standings-mode8-focused-horizontal')
            require(issue['action'] == expected_action, 'Standings issue action differs')
            for port, pos in enumerate(positions, 1):
                row = pipe_rows[pos]
                require(isinstance(row, dict) and
                        set(row) == {'host_monotonic_ns', 'port', 'action', 'intended_pad'} and
                        type(row['host_monotonic_ns']) is int and
                        row['port'] == port and row['action'] == issue['action'] and
                        row['intended_pad'] == issue['ports'][port-1],
                        'Standings source issue does not join actual successful Pipe write')
                covered.add(pos)
            previous_sequence, previous_pipe = issue['after_sequence'], positions[1]
        for index, row in enumerate(pipe_rows):
            if row.get('action', '').startswith('standings-mode8'):
                require(index in covered, 'Standings Pipe command lacks explicit source issue event')
            if row.get('action') == 'standings-evidence-stop:neutral':
                require(row.get('port') in (1, 2) and row.get('intended_pad') == NEUTRAL_PAD,
                        'Standings final neutral Pipe write differs')

    def validate_consumed_pad(self, row):
        ports = _consumed_ports(row, row['seq'])
        require(ports[2:] == [DISCONNECTED_PAD]*2 and self.policy.allowed(ports[:2]),
                'Standings consumed PAD lacks previously issued source-owned input')

    def validate_map_state(self, row, state, roots, consumed):
        require(roots == self.owner, 'Standings map root owner changed')
        ports = _consumed_ports(consumed, consumed['seq'])
        sample = dict(sequence=consumed['seq'], match_index=consumed['payload']['match_index'],
                      source_tick=consumed['source_tick'], draw_ordinal=consumed['draw_ordinal'],
                      ports=ports, errors=[int.from_bytes(bytes.fromhex(pad)[-1:], 'big', signed=True)
                                           for pad in ports])
        self.policy.observe(row, state, sample, self.go)
        # Until the selected stop, retain all old map/countdown checks. A real
        # transformation is an explicit inconclusive stop; its suffix remains
        # raw observed material and is not accepted as an idle/C4 comparison.
        if self.policy.stop is None:
            super().validate_map_state(row, state, roots, consumed)

    def accept(self, row):
        super().accept(row)
        if self.issue_cursor < len(self.issues):
            issue = self.issues[self.issue_cursor]
            require(issue['after_sequence'] >= row['seq'],
                    'Standings source issue event was missed/reordered')
            if issue['after_sequence'] == row['seq']:
                require(row['event'] == 'boundary' and
                        row['payload'].get('boundary') == 'source_tick' and
                        row['payload'].get('match_index') == 1 and
                        self.policy.last == issue['source'],
                        'Standings issue does not match actual focused source snapshot')
                self.policy.issue(issue['ports'])
                self.issue_cursor += 1

    def finish(self):
        result = super().finish()
        require(self.issue_cursor == len(self.issues), 'Standings source issue events incomplete')
        self.policy.summary()  # exact selected stop and consumed neutral release
        result.update(scope='original-whole3-b-standings-input-structural-window-only',
                      host_input_profile=StadiumStandingsInputPolicy.PROFILE,
                      idle_acceptance=False, transformation_acceptance=False, C3=False)
        return result
