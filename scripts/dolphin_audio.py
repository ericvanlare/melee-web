"""Host-output policy for owned Dolphin development/capture processes."""


def dolphin_audio_options(*, audible=False):
    """Mute speakers without disabling DSP emulation or raw PCM dumping.

    Audible sessions use the owned profile's backend and volume. Callers must
    arrange listening with the user; headless video alone never implies mute.
    """
    if not isinstance(audible, bool):
        raise TypeError("audible must be a boolean")
    if audible:
        return ["-C", "Dolphin.DSP.Muted=False"]
    return ["-C", "Dolphin.DSP.Backend=No Audio Output",
            "-C", "Dolphin.DSP.Muted=True"]
