#!/usr/bin/env python3
"""Prepare, audit and HTTP-verify the production player with replacement audio.

Reuses the source-bound Release audio producer and its restricted native API.
The explicit production policy changes hosting metadata and notices, without
changing source simulation, DSP arithmetic or the native audio implementation.
"""
from stage_audio_preview import main


if __name__ == '__main__':
    main(production=True)
