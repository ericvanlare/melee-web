"""Source-shaped LBMgr bookkeeping for diagnostic allocation replay.

Inputs are independently derived roots and allocator model transitions. Captured
manager words are comparison targets only. Payload bytes are outside this model.
"""

FIELDS = ('src', 'dst', 'size', 'offset', 'callback_arg', 'callback',
          'x6E0', 'x6E4', 'x6E8')


class CompactionManagerState:
    def __init__(self, initial):
        if (not isinstance(initial, dict) or set(initial) != set(FIELDS) or
                any(type(value) is not int or not 0 <= value <= 0xFFFFFFFF
                    for value in initial.values())):
            raise ValueError('independent compaction manager roots are missing or invalid')
        self.words = dict(initial)
        self.ram_generation = None

    def begin(self, state):
        # lbMemory_8001529C always publishes these, including a no-op request.
        # It does not clear the RAM copy manager from an earlier transfer.
        self.words.update(x6E0=state['callback_arg'], x6E4=state['cursor'],
                          x6E8=state['callback'])

    def callback(self, state, callback_address):
        self.words['x6E4'] = state['cursor']
        move = state['move']
        if (state['phase'] == 'waiting_ram_alarm' and
                move['generation'] != self.ram_generation):
            if self.words['size']:
                raise ValueError('source RAM copy started while another copy is active')
            self.words.update(src=move['source'], dst=move['destination'],
                              size=move['size'], offset=0,
                              callback_arg=move['next'], callback=callback_address)
            self.ram_generation = move['generation']

    def alarm(self, state):
        move = state['move']
        if move['generation'] != self.ram_generation or not self.words['size']:
            raise ValueError('source RAM alarm lacks its active manager generation')
        self.words['offset'] = move['offset']
        if move['offset'] == move['size']:
            # Source clears only size before invoking the next callback.
            self.words['size'] = 0

    def snapshot(self):
        size, offset = self.words['size'], self.words['offset']
        remaining = size - offset if size else 0
        return dict(self.words, remaining=remaining, chunk=min(remaining, 0x19000))
