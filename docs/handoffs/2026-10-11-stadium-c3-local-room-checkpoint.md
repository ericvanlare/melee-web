# Stadium C3 checkpoint (October 13 update)

Continue `codex/stadium-c3-lifetime` and draft PR #301. Main `d2cefef8` is
integrated at `17255683`, which also produced both passing Release builds.
The projection capture and full-regression source are `09acb92b`; the timing
baseline harness remains `3376a4d3`. The
[current status boundary](../status/2026-10-07-pokemon-stadium-profile-oninit.md)
and [portable evidence receipt](../evidence/issue251-stadium-profile-oninit-v1.json)
contain the measurements, validation, raw result and cleanup bindings.

Keep historical producers distinct: the first run2 prefix used runtime
`056c2708` and harness `c3cd27e5`; the earlier C/F capture used `e0c072cc` /
`af0e44fe` / `caabbb19`; the original reference uses observer `3d4ee3d3`,
binary `c0c51f4f` and reader `9e721b13`.

The timing probe and native-only observation projection passed. The projection
preserves native fields, getter/callback order, all comparisons and guards while
reducing unused host-history transport. The existing status boundary records
its read-cost improvement and exact regression; do not repeat either passing
capture without a new hypothesis. Next reduce remaining per-action polling and
report persistence in a smaller source-only reproducer before extending the
schedule. Preserve the stopped `1ed2b29c` regression, fixture-only repair, frozen-
plan recovery and prelaunch metadata failures. No capture lane is released by
this checkpoint, and no cap extension is permitted.

C3 remains open: the full source-ordered interval, lifetime A, checked
retirement/recreation, lifetime B and per-owner RNG comparison remain pending.
Keep PR #301 draft. Retain incremental builds for this active milestone; the
successful capture's empty temporary directory was removed after owned-process
absence checks. Failed evidence remains retained.

Tracking: [#251](https://github.com/ericvanlare/melee-web/issues/251),
[draft #301](https://github.com/ericvanlare/melee-web/pull/301),
[priorities #158](https://github.com/ericvanlare/melee-web/issues/158).
