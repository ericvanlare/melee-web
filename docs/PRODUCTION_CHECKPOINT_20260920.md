# September 20 production checkpoint

Feature expansion is paused at [PR #49](https://github.com/ericvanlare/melee-web/pull/49).
The candidate has fifteen public fighters and seven stages, original CSS/SSS,
and scene-specific imports from the user's validated local disc. The public
profile remains deliberately silent. This is a bounded alpha checkpoint,
not completion or acceptance of the whole vanilla game.

The added public fighters are Ganondorf, Captain Falcon, Luigi, Pikachu,
Pichu, Jigglypuff and Bowser. The added stages are Hyrule Temple, Fountain
of Dreams and Yoshi's Island 64. Existing per-fighter and stage notes retain
their native, browser, original-comparison and performance scopes.

Donkey Kong remains the sixteenth development fighter. The project owner
approved excluding him from this public release because platform shield-drop
reaches an unresolved terminal SPL0 animation consumer. Both public CSS
availability and final match validation reject him; development retains his
implementation. [Issue #50](https://github.com/ericvanlare/melee-web/issues/50)
tracks repair and re-enablement. The original consumer guard remains intact.

The public asset owner imports 32 menu inputs, then the exact selected match
scope while source ticks and draws are stopped. It closes source owners and
decoded caches before replacing file bytes. Public descriptors and the disc
wrapper exclude DSP coefficients. The existing file limits, music selection,
source arithmetic and original game ordering are preserved.

## Verification

The [checkpoint receipt](evidence/production-checkpoint-20260920-v1.json)
binds both Release builds, the audited production package, local HTTP checks,
ten public browser checks, two ordinary-key Mario/FD No Contest round trips,
Eject/reimport, and the independent 240-update drawn Mario/FD comparison.
The full local suite passes 1,099 tests in 374.968 seconds with 41 explicit
skips. The subsequent public Donkey restriction passes 26 focused menu and
public-release checks, including a post-SSS-OnExit rejection. Final pushed-head
GitHub verification remains the merge gate.

The runtime and public-owner reviews found no further concrete regression.
Their public-release blockers were the known Donkey route and unverified public
scoped loading; the exclusion and candidate evidence address those boundaries.
The preceding development scene checkpoint passed all GitHub Verify jobs in
6m 04s.

Failed attempts remain retained. An export-list ordering mismatch was caught
by packaging. One public lifecycle harness wrongly required transfer order
instead of an exact complete asset set; both match loops had passed before that
assertion. A state-control harness waited for an Unload control after replay
completion had already unloaded the scene. Corrected checks retain the native
transaction and teardown requirements. The instrumented public route also
records a timing pause. Two follow-up controls omit duplicate full callback
payload logging while retaining native scope calls and timing summaries; both
complete the same route and 120 further CSS source ticks without a resume.
The final control also verifies that status/loading panels are hidden during
active play and after return. This supports instrumentation sensitivity without
establishing general performance or erasing the earlier pauses.

## Release boundary and next work

The audited candidate is local and has not been deployed. The live alpha remains
the [verified September 19 release](evidence/public-marth-pipeline-release-v1.json),
whose immutable deployment is
[61b8658f.webmelee.pages.dev](https://61b8658f.webmelee.pages.dev).
Keep that deployment and its manifest for rollback. After merge/deployment
authorization, promote only the audited candidate and run the
[hosted verification procedure](PUBLIC_DEPLOYMENT.md#post-deployment-verification) on the
returned immutable origin and production origin. Local Wrangler reserved-config
502 limitations never satisfy a hosted 404 check.

Results PR #44 and audio PR #42 remain separate drafts. The legacy native menu
transition's rumble/RNG reference reconciliation, full fighter interactions,
four-player residency, remaining modes, original pixels/PCM, physical input,
retained-history memory and broader cold/warm performance remain open. The
public alpha's existing opcode-63 limitation also remains explicit. Resume new
feature work only after this checkpoint is reviewed.
