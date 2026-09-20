# Scene asset loading boundary

The integration branch's development player keeps one validated local disc
session and imports an exact menu or selected-match scope. The public player
still uses the existing fixed import. This change is an asset lifetime boundary,
not additional fighter, stage, mode or performance acceptance.

## Ownership and source ordering

`DiscAssetSession` accepts a local File, validates the pinned USA revision-2 DOL,
indexes the FST once, and checks the entire requested path set before reading
payloads. The existing 64 MiB per-file and 128 MiB requested-FST limits remain.
DOL-derived font bytes and independently generated, integrity-checked DSP
coefficients are explicit inputs. They are counted separately from FST bytes.
Closing the session invalidates pending reads and releases its File/DOL/FST
references; already returned byte maps remain owned by their callers.

Native descriptors contain all 33 required menu inputs or the selected match's
common, HUD, fighter/action, neutral and selected costume, effect, stage and
audio inputs. The menu bank registry includes each admitted fighter voice: original CSS OnExit
can request those banks even while abandoning a scene. The old fixed 13-bank
registry crashed unloading CSS after Bowser returned from a match; that failed
browser run is retained. Costume identities come from the source registry. The music
closure covers all four authored StageParam candidate words; it does not call
the music selector. An owned-archive test independently reads these words and
compares their paths through the pinned source HPS table. The original match
constructor still chooses and commits music at its existing RNG boundary.

On CSS/SSS-to-match and match-to-CSS transitions, the source owner closes first,
then the decoded cache closes, then source file vectors are released. The menu
host keeps only its copied selection and RNG lease across the handoff. The
browser reads and transfers the next scope while the native preparation gate
keeps source ticks and draws stopped. There is no timer-driven simulation or
page reload at this boundary.

`RuntimeAssetScope` stages a generation with an exact expected-name set.
Duplicate, unexpected, empty, oversized, missing and stale-generation inputs
fail explicitly; only a complete transaction publishes its map. Aborting a
transaction does not publish partial data. The browser transition has already
closed its outgoing scene, so a failed handoff stays stopped; it does not claim
that the previous scene was restored. Initial import failures can retry with a
fresh generation. Fatal or destroyed owners close late disc-open results too.

Unload closes native scene/cache owners and clears active/staged file vectors.
The development owner can retain the validated File session for a subsequent
start; disc replacement and destroy close that session. Allocated Wasm memory
need not shrink when its contents are released. File-byte receipts therefore
remain distinct from live allocations, reserved heap size and total browser RAM.

## Validation scope

Focused tests execute the transaction ledger and the actual JavaScript owner,
including asynchronous handoff, bounded batches, failed reads/transfers/commits,
stale generations, retry, unload/restart, late-open cancellation and teardown
failure cleanup. The owned CISO session check covers executable/FST validation,
preflight before payload reads, repeated scopes, generated-input provenance and
close during callback/await.

The retained implementation review is
`work/full-game/asset-scope-lifecycle-review-v1.md`. Both cleanup findings were
fixed and exercised by the scoped owner tests. The native v3 adjudication
retains and rejects two v2 findings after checking the compiled CSS availability
filter and the documented fatal-handoff/fresh-generation retry contract. No
actionable native lifetime, clock, RNG or audio-closure finding remained. Earlier manifest build failures
retain the missing source-type includes and include-order diagnostic; the final
header loads the compatibility owner before original menu types.

The final development browser check completes a Mario/FD four-stock diagnostic
ending (2,125 ticks, three respawns), then a Bowser/Mario Fountain pause and
L+R+A+Start No Contest, returning through the original CSS after each match.
A 200 ms hold at each real asynchronous handoff observes unchanged source-step
counts and zero outgoing/staged file bytes. Unload clears every imported file;
restart rereads the menu scope from the retained validated session.

Menus retain 18,452,982 native input bytes, Mario/FD 19,077,674 and
Bowser/Fountain 21,401,641. These totals include font/DSP generated inputs;
the respective FST totals are 18,301,942, 18,926,634 and 21,250,601 bytes.
Reserved Wasm memory grows from 278,396,928 to 334,102,528 bytes across the
sequence even though unload clears imported vectors. This is not a memory
plateau or performance result.

A fresh drawn browser replay matches all 240 declared Mario/FD updates,
including entry, input, fighter state, RNG, match clock and PAD history, against
the independently repeated original v2 references. The preceding browser
attempt had completed both scene loops and restart but correctly rejected the
older v1 recipe without PAD history; that failure remains retained.
Browser, full-suite and integration receipts are indexed by the corresponding
checkpoint in STATUS.md. Broader scene modes, public scoped imports, four-player
residency, long-run memory plateaus and complete original/pixel/PCM/performance
acceptance remain separate work.
