"""Input-only plans for the pinned Dolphin pipe boundary; no expected state.

Stick calibration must be empty, with zero dead zone and virtual notches. The
emulator then maps signed axes / 127 to the requested raw bytes. Digital L/R
force analog 255; A/B pressure follows the corresponding digital button. These
are explicit workload derivations, not recovered original hardware pressure.
"""
import hashlib
import json
from pathlib import Path
import re
import struct

SCHEMA = 'melee-web-retail-input-plan'
POLICY = 'dolphin-pipe-raw-v1'
MAX_FRAMES = 36000
PAD = struct.Struct('>HbbbbBBBBb')
DISCONNECTED_PAD = '00' * 10 + 'ff'
BUTTONS = ((1,'D_LEFT'), (2,'D_RIGHT'), (4,'D_DOWN'), (8,'D_UP'),
           (16,'Z'), (32,'R'), (64,'L'), (256,'A'), (512,'B'),
           (1024,'X'), (2048,'Y'), (4096,'START'))
BUTTON_MASK = sum(bit for bit, _ in BUTTONS)


def _keys(value, expected):
    if not isinstance(value, dict) or set(value) != set(expected):
        raise ValueError('Input plan has missing or unexpected fields')


def _integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError('Input plan integer is outside its bounds')


def _pad(value):
    if not isinstance(value, str) or re.fullmatch('[0-9a-f]{22}', value) is None:
        raise ValueError('Input plan requires 11 canonical hexadecimal PAD bytes')
    fields = PAD.unpack(bytes.fromhex(value))
    buttons, x, y, cx, cy, left, right, a, b, err = fields
    if (buttons & ~BUTTON_MASK or min(x,y,cx,cy) < -127 or err != 0 or
            a != (255 if buttons & 256 else 0) or b != (255 if buttons & 512 else 0) or
            (buttons & 64 and left != 255) or (buttons & 32 and right != 255)):
        raise ValueError('PAD sample is not representable by the declared pipe policy')
    return fields


def validate_plan(value):
    _keys(value, ('schema','version','policy','source_sha256','first_frame',
                  'source_stage','source_characters','frames'))
    if value['schema'] != SCHEMA or type(value['version']) is not int or value['version'] != 1 or value['policy'] != POLICY:
        raise ValueError('Unsupported input plan schema or controller policy')
    digest = value['source_sha256']
    if not isinstance(digest, str) or re.fullmatch('[0-9a-f]{64}', digest) is None or digest == '0'*64:
        raise ValueError('Input plan requires its Slippi source SHA-256')
    # This profile starts an ordinary match and does not support seeking or
    # pretending a mid-recording slice contains the original initial history.
    _integer(value['first_frame'], -123, -123)
    _integer(value['source_stage'], 0, 0xffff)
    if not isinstance(value['source_characters'], list) or len(value['source_characters']) != 2:
        raise ValueError('Input plan requires two source characters')
    for character in value['source_characters']:
        _integer(character, 0, 255)
    frames = value['frames']
    if not isinstance(frames, list) or not 1 <= len(frames) <= MAX_FRAMES:
        raise ValueError('Input plan requires a bounded nonempty timeline')
    for frame in frames:
        if not isinstance(frame, list) or len(frame) != 2:
            raise ValueError('Each input plan tick requires both human ports')
        for pad in frame:
            _pad(pad)
    return value


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate input plan JSON key')
        result[key] = value
    return result


def load_plan(path):
    path = Path(path)
    if path.stat().st_size > 4*1024*1024:
        raise ValueError('Input plan exceeds byte limit')
    raw = path.read_bytes()
    if len(raw) > 4*1024*1024:
        raise ValueError('Input plan exceeds byte limit')
    return validate_plan(json.loads(raw, object_pairs_hook=_unique)), hashlib.sha256(raw).hexdigest()


def plan_from_timeline(timeline, source_sha256):
    header = timeline.header.record()
    if (header['is_teams'] or tuple(p['port'] for p in header['players']) != (1,2)
            or any(p['player_type'] != 0 for p in header['players'])):
        raise ValueError('Retail input plan requires human singles on ports 1 and 2')
    if not 1 <= len(timeline.frames) <= MAX_FRAMES:
        raise ValueError('Input plan requires a bounded nonempty timeline')
    frames = []
    for frame in timeline.frames:
        if frame.number != timeline.frames[0].number + len(frames):
            raise ValueError('Input plan requires contiguous finalized source frames')
        if len(frame.inputs) != 2 or any(v.is_follower for v in frame.inputs):
            raise ValueError('Input plan does not support follower inputs')
        ports = {v.port: v for v in frame.inputs}
        if set(ports) != {1,2}:
            raise ValueError('Input plan requires both human ports each tick')
        pads = []
        for port in (1,2):
            value = ports[port].reconstructed_pad()
            buttons = value['buttons']
            triggers = [255 if buttons & mask else raw
                        for raw, mask in zip(value['triggers'], (64,32))]
            pads.append(PAD.pack(buttons, *value['stick'], *value['cstick'],
                *triggers, 255 if buttons & 256 else 0, 255 if buttons & 512 else 0, 0).hex())
        frames.append(pads)
    return validate_plan({'schema':SCHEMA, 'version':1, 'policy':POLICY,
        'source_sha256':source_sha256, 'first_frame':timeline.frames[0].number if frames else -123,
        'source_stage':header['stage_id'],
        'source_characters':[p['character_id'] for p in header['players']], 'frames':frames})


def pipe_commands(pad):
    buttons, x, y, cx, cy, left, right, _, _, _ = _pad(pad)
    commands = [('PRESS ' if buttons & bit else 'RELEASE ') + name for bit,name in BUTTONS]
    for name, axes in (('MAIN',(x,y)), ('C',(cx,cy))):
        commands.append('SET ' + name + ' ' + ' '.join(format(.5 + v/254, '.17g') for v in axes))
    for name, raw in (('L',left), ('R',right)):
        commands.append('SET ' + name + ' ' + format(raw/255, '.17g'))
    return ('\n'.join(commands)+'\n').encode('ascii')


def verify_entry(plan, start_hex):
    raw = bytes.fromhex(start_hex)
    if (len(raw) != 0x138 or int.from_bytes(raw[14:16], 'big') != plan['source_stage'] or
            [raw[0x60], raw[0x84]] != plan['source_characters']):
        raise ValueError('Retail menu selection differs from the donor characters/stage')


def verify_tick(plan, index, inputs):
    expected = plan['frames'][index] + [DISCONNECTED_PAD, DISCONNECTED_PAD]
    if inputs != expected:
        raise ValueError('Input intent mismatch at tick %d: expected %s, consumed %s' %
                         (index, expected, inputs))


def verify_capture(plan, capture):
    verify_entry(plan, capture.match_enter['start_melee_hex'])
    if len(capture.frames) != len(plan['frames']):
        raise ValueError('Capture does not consume the complete input plan')
    for index, frame in enumerate(capture.frames):
        if len(frame['consumed_inputs']) != 1:
            raise ValueError('Input plan requires one consumed vector per tick')
        verify_tick(plan, index, frame['consumed_inputs'][0])
