"""Read-only CPU/camera/HUD companion to the existing retail replay collector.

Offsets are the pinned GALE01r2 types, not a memory-dump comparison. Pointers
become slot identities or offsets inside the source CPU command buffer. Only
live queue elements and written command bytes are read. No CPU output is an
input to the replay recipe. The native counterpart is gameplay_cpu_observation.c.
"""
import json
from pathlib import Path
import struct

SCHEMA = 'melee-web-cpu-observation'
VERSION = 1
CAMERA = 0x80452C68
HUD = 0x804A10C8
MAGNIFY = 0x804A1DE0
MATCH = 0x8046B6A0


def u32(raw, offset=0):
    return struct.unpack_from('>I', raw, offset)[0]


def i32(raw, offset=0):
    return struct.unpack_from('>i', raw, offset)[0]


def i16(raw, offset=0):
    return struct.unpack_from('>h', raw, offset)[0]


def vector(raw, offset):
    return [raw[i:i + 4].hex() for i in range(offset, offset + 12, 4)]


class CpuObservation:
    def __init__(self, path, memory):
        self.path = Path(path)
        self.memory = memory
        self.types = []
        self.draws = 0
        self.started = False
        self.result = None

    def emit(self, value):
        with self.path.open('a') as stream:
            stream.write(json.dumps(value, separators=(',', ':'), sort_keys=True) + '\n')

    def begin(self, setup_hex, frames, fighters):
        raw = bytes.fromhex(setup_hex)
        if len(raw) != 0x138:
            raise ValueError('CPU observation requires complete source setup')
        types = [raw[0x61 + slot * 0x24] for slot in range(6)]
        slots = [slot for slot, kind in enumerate(types) if kind in (0, 1)]
        if (not 2 <= len(slots) <= 4 or slots != list(range(len(slots))) or
                any(kind != 3 for kind in types[len(slots):])):
            raise ValueError('CPU observation requires contiguous active ports 1 through 2–4')
        if self.started or self.path.exists():
            raise ValueError('CPU observation output must be new')
        self.types = types[:len(slots)]
        self.draws = 0
        self.started = True
        self.emit({'record': 'header', 'schema': SCHEMA, 'version': VERSION,
                   'frames_requested': frames, 'source_drawing': True,
                   'setup_hex': setup_hex})
        self.emit({'record': 'initial', **self.snapshot(fighters)})

    def cpu(self, address, fighters):
        raw = self.memory(address, 0x57C)
        base = address + 0x454
        write, cursor = u32(raw, 0x554), u32(raw, 0x450)
        defend, attack = raw[0xC8], raw[0xEC]
        if not base <= write <= base + 0x100 or (cursor and not base <= cursor < base + 0x100):
            raise ValueError('CPU command pointers escaped the source buffer')
        if defend > 8 or attack > 8:
            raise ValueError('CPU decision queue exceeds source capacity')
        target = u32(raw, 0x44)
        targets = {pointer: slot for slot, pointer in fighters.items()}
        if target and target not in targets:
            raise ValueError('CPU target is not an observed fighter slot')
        return {'kind': i32(raw, 0xC), 'level': i32(raw, 0x10),
                'state': i32(raw, 0x18), 'default_state': i32(raw, 0x1C),
                'secondary_state': i32(raw, 0x20), 'target_slot': targets.get(target, -1),
                'buttons': u32(raw), 'sticks': list(struct.unpack_from('bbbb', raw, 4)),
                'triggers': [raw[8], raw[9]], 'command_duration': u32(raw, 0x44C),
                'command_cursor': cursor - base if cursor else -1,
                'command_bytes': raw[0x454:0x454 + write - base].hex(),
                'defend_queue': [i32(raw, 0xA8 + 4 * i) for i in range(defend)],
                'attack_queue': [i32(raw, 0xCC + 4 * i) for i in range(attack)]}

    def snapshot(self, fighters):
        if sorted(fighters) != list(range(len(self.types))):
            raise ValueError('CPU observation active fighter identities changed')
        match = self.memory(MATCH, 0x2E)
        camera = self.memory(CAMERA, 0x4C)
        gobj = u32(camera)
        if not gobj:
            raise ValueError('CPU observation has no source camera')
        cobj = u32(self.memory(gobj + 0x28, 4))
        projection = self.memory(cobj, 0x51)
        players = []
        for slot, pointer in sorted(fighters.items()):
            subject_pointer = u32(self.memory(pointer + 0x890, 4))
            subject = None
            if subject_pointer:
                raw = self.memory(subject_pointer, 0x28)
                subject = {'state': i32(raw, 8), 'timer': i16(raw, 0xE),
                           'on_ledge': (raw[0xC] >> 7) & 1,
                           'force_inactive': (raw[0xC] >> 6) & 1,
                           'was_framed': (raw[0xC] >> 5) & 1,
                           'position_bits': vector(raw, 0x10), 'bone_bits': vector(raw, 0x1C)}
            hud = self.memory(HUD + slot * 0x64, 0x11)
            flags = hud[0x10]
            magnify = self.memory(MAGNIFY + 0x14 + slot * 0x10 + 0xC, 1)[0]
            players.append({'slot': slot,
                'cpu': self.cpu(pointer + 0x1A88, fighters) if self.types[slot] == 1 else None,
                'subject': subject,
                'magnifier': {'offscreen': (magnify >> 7) & 1,
                              'ignore_offscreen': (magnify >> 6) & 1, 'edge': magnify & 63},
                'hud': {'present': bool(u32(hud)), 'damage': i16(hud, 0xA),
                        'old_damage': i16(hud, 0xC), 'last_attack_damage': hud[0xE],
                        'shake_frames': hud[0xF], 'explode': (flags >> 7) & 1,
                        'randomize_velocity': (flags >> 6) & 1,
                        'force_shake': (flags >> 5) & 1, 'hide_digits': (flags >> 3) & 1,
                        'animation_status': (flags >> 1) & 3}})
        return {'match': {'frame': u32(match, 0x24), 'seconds': u32(match, 0x28),
                          'subframe': struct.unpack_from('>H', match, 0x2C)[0],
                          'outcome': match[8], 'end_state': match[0]},
                'camera': {'position_bits': vector(camera, 0x2C),
                           'interest_bits': vector(camera, 0x14),
                           'projection': projection[0x50],
                           'fov_bits': projection[0x40:0x44].hex() if projection[0x50] == 1 else '00000000',
                           'near_bits': projection[0x38:0x3C].hex(),
                           'far_bits': projection[0x3C:0x40].hex()}, 'players': players}

    def tick(self, index, fighters):
        self.emit({'record': 'frame', 'index': index, **self.snapshot(fighters)})

    def draw(self, source_index, fighters):
        self.emit({'record': 'draw', 'index': self.draws, 'source_index': source_index,
                   **self.snapshot(fighters)})
        self.draws += 1

    def end(self, frames, remaining_fighter_slots):
        self.emit({'record': 'end', 'frames': frames, 'draws': self.draws,
                   'remaining_fighter_slots': remaining_fighter_slots, 'result': self.result,
                   'status': 'captured'})
