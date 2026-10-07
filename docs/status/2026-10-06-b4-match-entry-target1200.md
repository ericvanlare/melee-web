# MWRC v10 replay reached first match entry at target 1200

**Compiled / Source identified / Browser exercised**

One ordinary headless state-mode replay of the exact MWRC v10 recipe reached
match entry. The last live poll observed cursor 1206 in replay phase 7, while
the requested stop was 1200. Its status named the original Donkey Kong vs
Bowser vs Ness vs Peach match on Final Destination. The later phase-0, cursor-0
snapshot is from deliberate unload and is not the terminal live cursor.

The exported source trace contains 1,209 indexed `session_frame` records,
indexes 0–1208, and one `session_match_enter_complete` event between SSS and the
first match frame. Its ordered scene spans are CSS scene 1 at indexes 0–936,
SSS scene 2 at 937–1183, and match scene 3 at 1184–1208. All 25 exported match
records still report `match_frame: 0`: this is match-entry evidence only, not
active gameplay. The header declares comparison `not_run`, CPU observations
`not_captured`, and draw state `not_captured`.

The [portable receipt](../evidence/b4-match-entry-target1200-v1.json) binds the
source tree, exact recipe and disc hashes, Release artifact identities, tools,
protocol, source ordering, incomplete result, inventory, cleanup and retained
report hashes. Machine-specific locations are omitted. The private run is
identified as `b4-match-entry-current-release-20261006-f6d938f2`; its capture
summary SHA-256 is `23013f3947ff07f808eca81dd9597be744dd46750a53fd90b99984d8d7499fb5`. The
source producer was `63b833b` over `e706a36`. The Release runtime build passed,
and the full Python suite completed 1,941 tests, with 142 skipped.

## Deliberate incomplete result

The capture and wrapper exited 1 as required for an incomplete bounded prefix.
The report's first error followed manual unload: whole-session final CSS was
not entered, manual unload stopped replay, the input timeline was incomplete,
and source tick/draw counts did not match. There were no browser errors or
runtime error before the stop. These completion failures remain in the private
capture report and portable receipt; they are not waived by reaching match
entry.

The five-second observation bound, headless launch and muted host audio were
active; audio processing remained enabled. The launch manifest records the
explicit browser/runtime and external `TMPDIR` settings, with no other
capture-related environment overrides found. All 32 local and served Release
artifacts matched the frozen inventory before and after capture. CDP
attribution, OS process observation and strict cleanup passed; all capture/browser/server process groups were absent and the port was
free. The exact owned timing marker was released after those checks. The fresh
mode-0700 external temp root remained empty and was preserved.

This result does not establish original-state equality, full-session
acceptance, gameplay accuracy, pixels, PCM, performance, foreground timing,
GPU cause or tournament readiness. The v10 whole-session comparator was not
run for this capture. The earlier [CSS prefix](2026-10-06-b4-css-observer-repair-prefix.md)
and [first callback](2026-10-06-b4-first-replay-callback.md) receipts remain
unchanged; neither run is merged into this result.
