# Stadium C3 checkpoint

At the owner's October 11 request, Stadium C3 is checkpointed while the next
bounded target is local room UI readiness. Continue existing branch
`codex/stadium-c3-lifetime` and draft PR #301; do not duplicate its implementation.
The current main base is `5f274ed3fc6743937c8ec2ae66e23c1db4094c4e`.

Source checkpoint: `af0e44fe5be5ec9e5b33d28ef93e52a6cc4b61a8`.
Runtime producer: `caabbb196fad1beff68cee41607b5debcb1bc4c9`; the later commit
changes only two test fixtures. Both Release builds pass. The full suite passes
2,497 tests with 180 skips; [exact-source CI](https://github.com/ericvanlare/melee-web/actions/runs/38165072323)
also passes. Prior fixture failures remain retained.

The new diagnostic preserves 86 original four-port PAD bundles and 171 source
ordered tick/draw actions, using the same strictly compared Setup owner and
host-only expected state. C precedes draw84; F follows tick85 and precedes draw85.
The first attempt failed before Chrome started because its configured temporary
directory parent was absent. No C/F gameplay state was observed. The three
known capture/server PIDs are absent; owner `cleanup_passed=false` remains
recorded because no browser was attributed. An earlier prelaunch input/output
classification failure is also retained.

The corrected launcher passes a no-spawn preparation control. The future run
root is untouched and **no further capture is authorized**. Resume by reviewing
its frozen candidate against current source/producer identities, then allow one
bounded capture and inspect the first failure. Do not repeat unchanged runs.

Private evidence run `stadium-c3-lifetime-20261009-074216-4d7cf7d1` contains
`sol-matched-setup-cf-run2-preparation-v1/freeze.json`,
`sol-matched-setup-cf-result-review-v1/review.json`, caab producer retention,
run1 reports and `luna-matched-setup-cf-docs-fragment-v1` for the next boundary
update. Locate it through the external-storage project runs and coordination
checkpoint. The earlier successful observation remains in the
[Stadium status entry](../status/2026-10-07-pokemon-stadium-profile-oninit.md).

C3 remains incomplete: original-matched post-GO state/per-owner RNG, two
3,500-tick lifetimes and retirement/recreation are still required. Stage
admission, pixels, PCM, physical input and foreground timing remain open.

Tracking: [#251](https://github.com/ericvanlare/melee-web/issues/251),
[draft #301](https://github.com/ericvanlare/melee-web/pull/301),
[owner priority change](https://github.com/ericvanlare/melee-web/issues/158#issuecomment-6112686789).
