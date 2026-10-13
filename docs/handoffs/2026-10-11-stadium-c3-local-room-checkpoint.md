# Stadium C3 checkpoint (October 13 update)

Continue `codex/stadium-c3-lifetime` and draft PR #301. Main `7e430889` is
integrated at merge `0a8f176b`. The current runtime producer is `056c2708`;
capture harness and full-regression source are `c3cd27e5`. Both Release builds
and the full regression passed; exact merged-head CI remains pending. The
[current status boundary](../status/2026-10-07-pokemon-stadium-profile-oninit.md)
and [portable evidence receipt](../evidence/issue251-stadium-profile-oninit-v1.json)
contain the changing measurements, full validation details, raw result and
cleanup bindings.

Keep the historical browser identities distinct: the earlier C/F capture used
`e0c072cc` / `af0e44fe` / `caabbb19`; the original reference uses observer
`3d4ee3d3`, binary `c0c51f4f` and reader `9e721b13`. Their results remain
separately scoped in the status and receipt.

C3 remains open. The original reference covers 3,500 post-GO ticks and its full
Setup-to-terminal schedule has 7,167 actions. The current browser result covers
only a short source-ordered prefix. The next experiment is to measure per-action
host queue/approval overhead within the existing 150-second capture-work bound
before admitting the full interval. The full-schedule protocol extension,
lifetime A, checked retirement/recreation and lifetime B with per-owner RNG
comparison remain open. No capture lane is currently released; keep PR #301
draft.

Tracking: [#251](https://github.com/ericvanlare/melee-web/issues/251),
[draft #301](https://github.com/ericvanlare/melee-web/pull/301),
[priorities #158](https://github.com/ericvanlare/melee-web/issues/158).
