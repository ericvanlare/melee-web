"""Normalize repeated debugger traps, never differing source observations."""


class BoundaryObservations:
    """Each phase key may repeat only with an exactly identical observation.

    Callers key scheduler returns by the original scene counter (which advances
    through Ready), and PAD entries by that counter plus queue read identity.
    The payload includes machine context and all measured state. A differing
    payload at the same key is an error, not a sample to discard.
    """

    def __init__(self):
        self.previous = {}
        self.duplicates = {}
        self.seen = {}

    def accept(self, phase, key, observation):
        previous = self.previous.get(phase)
        if previous is not None and previous[0] == key:
            if previous[1] != observation:
                raise ValueError(f"Conflicting {phase} observation at the same source boundary")
            self.duplicates[phase] = self.duplicates.get(phase, 0) + 1
            return False
        seen = self.seen.setdefault(phase, set())
        if key in seen:
            raise ValueError(f"Out-of-order {phase} source boundary")
        seen.add(key)
        self.previous[phase] = (key, observation)
        return True
