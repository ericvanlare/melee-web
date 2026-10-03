# September 20 production checkpoint

Feature additions are paused at PR #49. The candidate exposes sixteen public
fighters and seven stages; Donkey Kong's platform shield-drop crash is repaired
on this branch (see [Donkey Kong's measured scope](../DONKEY_KONG_PORT_NOTES.md)),
re-enabling him in public character selection pending the remaining
[issue #50](https://github.com/ericvanlare/melee-web/issues/50) verification.
Both public and development players now load exact scene asset scopes from a
validated local disc session. That checkpoint's public audio was disabled.

Both Release builds and the local 1,099-test suite pass (374.968 seconds,
41 explicit skips). The since-removed Donkey restriction had passed 26 focused
menu and release checks. The audited production candidate passes local HTTP checks,
all ten public browser checks, two ordinary-key Mario/FD pause/No Contest
round trips, Eject/reload and reimport. Public menu and Mario/FD scopes contain
32/27 inputs and 18,448,886/19,073,578 bytes. A fresh drawn Mario/FD control
matches all 240 declared updates and PAD-history fields against the independent
original pair and retires all native scene/file owners afterward.

Retained harness failures and an instrumented timing pause are recorded. Two
controls with reduced duplicate callback logging pass the same route and 120
further CSS ticks without a resume; the final control verifies hidden loading
panels. These lifecycle checks do not establish performance or complete-game
acceptance.
PR #49 merged as `979fd09` and its exact staging-tested package was deployed to
[production](https://bed694b0.webmelee.pages.dev). Both the immutable origin and
webmelee.gg passed exact resource/header/route checks and ten public browser
checks each. The owner also tested staging and reported multi-CPU loading lag
as follow-up work. See the [release record on PR #49](https://github.com/ericvanlare/melee-web/pull/49),
[release checkpoint](../PRODUCTION_CHECKPOINT_20260920.md) and
[hash-bound receipt](../evidence/production-checkpoint-20260920-v1.json).

The subsequent PR review's two P3 findings are corrected: reports reject
untracked original-source files, and Fountain's music tuple matches its authored
four words. All seven tuples and derived music sets pass owned-archive checks;
both Release targets and the refreshed candidate audit pass. See the
[review-fix receipt](../evidence/production-checkpoint-review-fixes-v1.json);
final full-suite and pushed-head verification are recorded on PR #49.
