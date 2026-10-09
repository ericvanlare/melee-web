"""Explicit source-slot maps for the bounded original two-human probes."""

DEFAULT_SOURCE_SLOTS = (0, 1)
SPARSE_SOURCE_SLOTS = (0, 2)
SOURCE_SLOT_COUNT = 4


def declared_source_slots(value):
    """Accept only the two already-supported two-human source layouts."""
    if (not isinstance(value, (list, tuple)) or
            any(type(slot) is not int for slot in value) or
            tuple(value) not in (DEFAULT_SOURCE_SLOTS, SPARSE_SOURCE_SLOTS)):
        raise ValueError("Unsupported original source-slot pair")
    return tuple(value)


def lane_source_slot(lane, source_slots=DEFAULT_SOURCE_SLOTS):
    slots = declared_source_slots(source_slots)
    if type(lane) is not int or not 0 <= lane < len(slots):
        raise ValueError("Logical Pipe lane is outside the declared pair")
    return slots[lane]


def expand_lanes(lanes, source_slots=DEFAULT_SOURCE_SLOTS, *, inactive=None):
    """Place lane-ordered values into their original four-source-port slots."""
    slots = declared_source_slots(source_slots)
    if not isinstance(lanes, (list, tuple)) or len(lanes) != len(slots):
        raise ValueError("Logical lane values do not match the declared source pair")
    result = [inactive] * SOURCE_SLOT_COUNT
    for lane, value in enumerate(lanes):
        result[slots[lane]] = value
    return result


def project_sources(values, source_slots=DEFAULT_SOURCE_SLOTS):
    """Project a complete four-port source vector into declared lane order."""
    slots = declared_source_slots(source_slots)
    if not isinstance(values, (list, tuple)) or len(values) != SOURCE_SLOT_COUNT:
        raise ValueError("Source vector must contain all four original ports")
    return [values[slot] for slot in slots]


def inactive_source_slots(source_slots=DEFAULT_SOURCE_SLOTS):
    slots = declared_source_slots(source_slots)
    return tuple(slot for slot in range(SOURCE_SLOT_COUNT) if slot not in slots)


def signed_pad_errors(raw):
    """Read the four signed PADStatus error bytes from a 0x30-byte vector."""
    if not isinstance(raw, (bytes, bytearray)) or len(raw) != 0x30:
        raise ValueError("Four-port PADStatus vector must be exactly 0x30 bytes")
    return [value - 256 if value >= 128 else value for value in raw[10::12]]
