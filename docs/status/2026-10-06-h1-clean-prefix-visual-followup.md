# Exact-recipe H1 diagnosis: clean prefix and black stopped canvas

Evidence labels: **Source identified / Compiled / Browser exercised**. The
[portable receipt](../evidence/h1-248fe763-clean-prefix-v1.json) summarizes the
bounded run retained in the private external run
`h1-natural-pause-diagnostic-repair-20261006-20261006-185549-7bed3de9`.

The headless Release diagnosis used the exact retained MWRC v8 recipe and Melee
USA Rev 2 input, default two frame slots, and no injected load, stall or timing
resume. It reached its cursor limit and crossed the previous cursor/frame pause
identity without a new timing or runtime incident. This is a clean prefix, not a
reproduction or explanation of the earlier pause.

The trace and callback table completed. Sampled frame/staging slot waits were
zero, but the run did not retain aggregate queue-completion records or event IDs;
it cannot classify staging completion. The old report's `stopped_scene_visual`
field meant that a screenshot, viewport and GPU fields existed. Review found the
canvas black, so the field does not establish visible gameplay. The trace also
does not prove bytes consumed by Chrome or the Wasm filesystem: its 32-file local
and independent HTTP pre/post maps are separate provenance checks.

The cause remains unknown. A separate short paired-screenshot reducer tests the
delay hypothesis by stopping at the first positive match frame near cursor 1600,
verifying no source advancement, then capturing immediately and after the
existing trace/GPU-export delay. It does not resume, advance or redraw the source.
That reducer has not yet been run; both black images would leave the presentation
cause unknown and end this boundary.

The supported original CSS → SSS → four-stock Mario/Final Destination → Results
→ CSS route, zero natural timing pauses, performance admission, original visual
comparison, foreground timing, physical input and audio gates remain open.
