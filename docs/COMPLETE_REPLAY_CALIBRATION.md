# Complete-game replay calibration

This is one derived vanilla Fox/Falco Battlefield workload, not broad content
admission. It extends the 240-tick neutral and 686-tick movement controls through
an original elimination ending and source teardown. The reference remains a
separate pinned Dolphin process; the browser executes compiled game source.

## Input and reference identity

The input donor is the public Slippi 2.0.1 recording
`19_06_05 Fox + Falco (BF).slp` from `erickfm/slippi-public-dataset-v3.7`, dataset
revision `c82be5f6e43f3388555cfe0cf8652580601f396d`. Its SHA-256 is
`bd3112441fdbf4c11d8e0332a64236c4022c7f930d51c1226f8e3e30def057af`.
The complete file has 9,898 frames, -123 through 9774, and a game-end record.
Slippi JS 9.1.3 independently parses the same file. Both players used UCF;
those recorded post-frame states are never vanilla expectations.

This older format lacks raw stick bytes. The explicit `dolphin-pipe-processed-v2`
policy derives canonical sticks from its processed axes, rejects values beyond
the admitted range and enforces the retail mode-3 trigger contract. A/B analog
pressures are zero; digital L/R retain their full-pressure behavior. This is a
reproducible derived workload, not reconstruction of the original hardware
inputs. Export requires the complete parsed file and game-end record before
selecting an input prefix.

The newly executed vanilla game ends after 3,122 ticks. Its input plan SHA-256 is
`787d0d4b25aaed5f2684d2938080090994b5c2c5d594dfa541228b03c3d5b86d`;
the paired MWRC recipe is
`fa4e646ffa56aa0ad34938a4f427fa9e5600ad18a6308a165566f33e5f36e54a`.
The pinned disc, Dolphin binary, ordinary SSS checkpoint, external GC data,
fixed RTC and controller configuration are recorded with each capture. See
[the capture procedure](RETAIL_REPLAY_CAPTURE.md).

Two fresh JITARM64 captures repeat exactly across all 3,122 declared ticks:

| Capture | SHA-256 | Wall time |
| --- | --- | --- |
| A | `15146ce4b3b542ee1ddf82d5345395dec551612c4f7ad82152e0d58ca061ff0d` | 59.22 s |
| B | `56e90c852bfe0305b42e93dc3a9d71ec2a2206b9f2fde3a80483631fd6c84511` | 61.77 s |

Each source tick consumes exactly the intended four-port PAD vector. Both runs
observe the original scene-exit return at source tick 3121, its elimination
caller, result 2 and the final draw. They include the 114-tick GAME! ending.
Each camera audit observes 3,120 draws; source indices 1264 and 2265 have no
observed traversal. Every observed traversal preserves the declared state.
Sparse reference drawing is explicit; this is not cadence or pixel equality.

The faster collector passed the immutable 240/686-tick interpreter controls.
The final collector now also passes the entire 3,122-tick cross-check in both
backends against frozen A/B, including every declared state/input field and the
end record. Both final captures have valid completion sidecars and 3,120
unchanged camera traversals with the same two explicit source-index gaps.

| Final collector run | SHA-256 | Wall time |
| --- | --- | --- |
| JITARM64 | `31650aed7073b49b23bede7e4503d9ce08aafcfb85cbb39e50bcf9e9565dc53f` | 60.40 s |
| Interpreter64 | `7c42abc07256fcd9f17f7b40e1f81dd261128042e8d9af162cbcd5c622a5e864` | 630.86 s |

Earlier interpreter attempts remain invalid evidence: late input publication
failed around tick 2264, and the subsequent pre-IRQ publisher exposed an unsafe
master-queue entry observation. The final collector observes the consumed slot
at `80377584`, after queue decrement and before interrupt restoration, validates
its preserved pointer/index, and independently verifies all four actual PAD
vectors at scheduler return. PADRead publishes the successor input after all
four statuses are copied but before it restores interrupts. The capture header
names the protected dequeue boundary explicitly; old entry-queue captures
remain readable but cannot be mixed into an independent repeat pair. No
controller or state mismatch was waived to obtain these passes.

## Runtime defects exposed and fixed

The comparison retains exact float bits. Each arithmetic change below follows
the pinned retail instructions; general fast-math and implicit contraction
remain disabled.

| Boundary | Evidence and shared correction |
| --- | --- |
| Hermite animation/root motion | First divergence at tick 192. `splGetHelmite` retains the ordinary multiply followed by three `fmadds` at `80378A80..80378A8C`. |
| Directional influence | Tick 2750 knockback divergence. Magnitudes, cross product and angle accumulation retain the original scalar fused rounding in `ftCo_8008E5A4` and the hitlag-exit path. |
| Linear animation | `FObjUpdateAnim` uses the retail `fmadds` at `8036AF98`. |
| Joint matrix concatenation | Tick 2789 grabbed-fighter position divergence. The gameplay SDK replacement preserves `PSMTXConcat` paired-single operation order, its zero/one lanes and output aliasing. Explicit `C_MTXConcat` remains distinct. |
| Knockback decay | Tick 2834 velocity divergence. `Fighter_procUpdate` retains the magnitude `fmadds` and decay `fnmsubs` boundaries. |
| Effect parameters | Shield reflection at tick 173 corrupted a fighter-data pointer. Original `efLib` setters indexed beyond `efLib_AnimQueue`, relying on retail adjacency to `efLib_ParamTable`. They now address the actual parameter-table object. No fighter-data pointer is refreshed to hide corruption. |

The parameter-table regression executes the actual patched setters and checks
that the animation queue and neighboring storage stay unchanged. Headless
replays inspect asset ownership after each source tick; teardown checks retain
phase and expected/actual ownership diagnostics. The matrix test executes the
Wasm primitive, including aliasing, signed zero and an unfused negative control.

## Port and browser evidence

Release and RelWithDebInfo headless replays match every declared field for all
3,122 ticks, reach elimination with Falco winning, and finish teardown. The
visible Release state capture also matches every declared field after source
drawing and closes successfully. Its trace SHA-256 is
`af184be8e15b061cf60930a8176373f36af5b091a2f68a17150df608edb16414`.
The neutral and movement comparisons still pass. The original Mario/FD menu
transition control also retains all nine lifecycle, audio-continuity, rules and
RNG boundaries.

The browser discovery run exposed 38 additional portable pipeline descriptors.
The reviewed seed preserves all previous row payloads and now contains 427
type-1 rows plus the existing type-0 row. Its database SHA-256 is
`8e4c016b60ac9f2ae041312d81b55953bdad59fa4572f568c909ee41e6815fed`.
The final state run creates no live pipelines. Separate final-build visible
Release performance runs pass the complete recipe with ordinary audio enabled:

| Metric | Cleared origin | Warm full application reload |
| --- | --- | --- |
| Source ticks | 3,122 | 3,122 |
| Worst native callback | 11.330 ms | 12.485 ms |
| Worst browser interval | 21.365 ms | 22.010 ms |
| Intervals/callbacks over 33.3 ms | 0 | 0 |
| Browser long tasks | 0 | 0 |
| Live pipelines queued/created | 0 / 0 | 0 / 0 |
| Audio underrun/overflow frames | 0 / 0 | 0 / 0 |
| Preparation pauses or resumes | 0 | 0 |
| Match preparation, reported separately | 259.150 ms | 238.825 ms |
| Live texture upload bytes | 5,460,992 | 5,460,992 |
| Wasm heap growth bytes | 122,028,032 | 226,492,416 |

Both finish elimination and teardown without losing visibility. The cold report
SHA-256 is `843fc82c901bf7954c606bd169760be99105d553c2d55adb060a80e8dbe9cfcf`;
the warm report is
`fa6b8076de8963e717e530f55612ee74180ff8bc283293d3cae6740091753555`.
The joined `scoped-replay-evidence.json` (SHA-256
`a5d3d163e174f2ccad7a1cc40c97a9b7a087550d352f9aaa74e0628d75f497d8`)
revalidates the raw references,
completion sidecars, recipe, drawn trace, both timing reports and final Release
artifact hashes. Console inspection found no browser errors. The profile is a
visible Chromium 152 browser on an AC-powered Apple M4 with a 640×480 framebuffer
and DPR 2. Builds, tests and Dolphin were stopped during timing. Clearing the
origin does not clear browser/driver caches. These are callback/transport gates,
not GPU execution timing, pixel/PCM equivalence or broad content admission.

Final verification: all 435 automated tests pass. Affected Release browser,
headless, content-lifecycle, menu-transition and close-range combat targets were
rebuilt; the separate rebuilt combat regression passes. Test fixture manifests
now include the owned rumble data and current menu audio banks. Owned Dolphin,
GDB, browser-tab and local evidence-server lifetimes were closed after capture.

Ignored evidence lives under `work/combat-replay/`: paired captures, completion
sidecars, MWRC, exact comparisons, DOL disassemblies, retained pre-fix traces,
the immutable browser exports, pipeline review and observed coverage. Portable
tools, negative controls and this evidence ledger are tracked; game assets,
donor files and generated captures are not.

## Coverage and next admission work

Coverage comes from the new vanilla execution. It includes 63 Fox motion IDs,
60 Falco motion IDs, seven stock losses, three respawns per player, shield and
reflect transitions, rolls, air dodges, lasers, side specials, a grab/back throw
and damage/knockback. It has only five damage increases and no up-special.
The altered vanilla trajectory contains many self-destructs. A long input donor
alone does not guarantee dense combat coverage.

Next, select a small representative set plus an untouched held-out set by
observed vanilla coverage. Fill hitlag/DI, up-special, ledge, grab/throw and
stage-specific gaps before broadening admission. Reuse immutable reference
traces during routine iteration and recapture only when their input/profile
contract changes. Keep state, lifecycle, normal-audio transport and visible
cold/warm timing gates separate. Pixels, emitted-PCM equivalence, physical input,
driver-cold behavior and full-match equivalence outside declared fields remain
unestablished.
