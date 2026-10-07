# MWRC v10 primary fighter identity observed through first match entry

**Compiled / Source identified / Browser exercised**

A single ordinary headless state-mode replay emitted primary fighter identity rows at match setup and on the first match source ticks. The capture reached the requested source cursor 1200 exactly. Its 1,200 `session_frame` records, indexes 0–1199, partition in source order into CSS at 0–936, SSS at 937–1183, and match at 1184–1199, with the setup event between SSS and the first match frame.

The setup event and all 16 match records passed the unchanged `tools/whole_session_state_compare.py::_browser_entities` validator. Each row contains the four ordered primary identities for slots 0–3, entity index 0, generation 0, the matching fighter player ID, and a linked Fighter GObj. Every observed match row reports `match_frame: 0`; this is first-entry identity evidence, not active gameplay.

The last live replay poll was at 24,577.5 ms in phase 7 with cursor 1200 and `running: 0`. Its status was `Preparing first-use rendering... · 0 ms · audio paused`. The report's later cursor-0 snapshot is from deliberate unload. The trace header records `comparison: not_run`; no original-state or whole-session state comparison was performed.

The [portable receipt](../evidence/b4-v10-primary-identity-target1200-v1.json) binds the source, 32 Release artifacts, exact v10 recipe and disc identities, browser/tool environment, trace, identity validation, inventories and cleanup. The private run is `b4-v10-primary-identity-target1200-20261006-a02043f4`; its identity-analysis summary SHA-256 is `9ee02dbdd7744669675a0976802d045b926250409f55dcfa6ab11c31386b79b2`. The Release build and full suite ran against the tested implementation before it was committed as `be37047`; no executable content changed between validation and that commit. The capture used that clean commit over `558d6e8`. The Release runtime build passed, and the full Python suite completed 1,942 tests, with 142 skipped.

## Deliberate incomplete result

The bounded capture was deliberately unloaded after the target cursor. The capture and wrapper exited 1; the browser report retained an `incomplete` result because whole-session completion was not reached. Its first error describes post-unload finalization; there were no browser errors and no runtime error at the last live cursor. These failures remain in the private reports and portable receipt.

The pre- and post-capture inventories each matched all 32 expected artifacts. CDP attribution, OS process observation and strict cleanup passed; capture and server process groups were absent, and the port was free. The exact owned timing marker was released after verifying its identity. The fresh mode-0700 external temp root remained empty and was preserved.

This result does not establish active gameplay, original-state agreement, whole-session acceptance, pixels, PCM, performance, foreground timing, GPU cause or competitive accuracy. The earlier [target-1200 match-entry capture](2026-10-06-b4-match-entry-target1200.md), [CSS prefix](2026-10-06-b4-css-observer-repair-prefix.md) and [first callback](2026-10-06-b4-first-replay-callback.md) remain separate evidence.
