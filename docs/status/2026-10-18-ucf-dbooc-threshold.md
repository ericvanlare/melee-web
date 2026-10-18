# UCF 0.84 DBOOC threshold component

**Source identified / Native and Node/Wasm scalar component / Live UCF and profile acceptance unproved**

[Milestone #335](https://github.com/ericvanlare/melee-web/issues/335) implements
the first isolated UCF behavior under [mode identity #9](https://github.com/ericvanlare/melee-web/issues/9).
It changes the positive threshold read by the original
`ftCo_SquatRv_CheckInput` comparison. The original comparison, return values and
`ftCo_SquatRv_Enter` transition remain the source boundary. This is one component
of **Slippi-compatible rules (unofficial)**, not complete UCF or an available
online mode. The project is not affiliated with or endorsed by Project Slippi.
The [structured receipt](../evidence/ucf-dbooc-threshold-v1.json) binds the
source, arithmetic review, retained failures and validation identities.

## Arithmetic and source contract

The behavioral source is pinned `External/UCF 0.84/UCF/UCF DBOOC SquatRv Fix.asm`.
The injected instruction sequence uses a zero-extended timer byte, so only timer
zero enters the calculation. Each axis uses its absolute value and one fused
binary32-rounded multiply-subtract with scale 80 and the authored epsilon.
Checked truncation, unsigned modulo addition and low-word squares preserve the
source integer behavior; the final comparison interprets the sum as signed.
Only a sum strictly greater than 6400 selects the replacement positive threshold.
The original source then compares stick Y against the negative threshold.

An independent integer/rational oracle derives all 79 reachable quantization
transitions within finite binary32 axes in [-1, 1], including both signs and
adjacent values. Transition 80 lies outside that domain. The own-C native and
Node/Wasm helpers agree with all 331 declared finite vectors, including the
316 transition neighbors and 15 fixed controls. Separate conversion,
exceptional-value, modular-sum and source-transition controls keep that finite
oracle from being mistaken for an unrestricted numerical proof.

The source reducer uses the production patch-composition functions, then extracts
both the original and profile-overlaid CheckInput and unchanged Enter functions.
Seven original/profile/omitted-helper triples exercise the causal threshold,
X-timer bypass, equality and adjacent comparisons. A wrong-Y-timer mutation is
rejected. Native and Node/Wasm outputs agree on all 386 declared rows; the full
23-test focused profile cohort passes. Services and Fighter data remain
synthetic, with a checked motion-state recorder, so this is source-component
evidence, not an original-game recording.

The helper is wired only into the private diagnostic build under the fixed
`slippi-render-dependencies-dbooc-v4` identity. Earlier identities are rejected.
Compiler-preprocessed profile-off source is byte-identical to the original
function. The historical 373-row scalar result predates the overlay and remains
separate in the receipt. Both affected Release builds and the full regression
suite pass on implementation commit `89646f46`: 2,425 tests with 166 skips.
The final PR head and CI/integration are tracked separately on the milestone
issue. Seventeen build artifacts and the raw validation logs are retained.

Read-only comparison attributes the ordinary Wasm changes to SDL embedding
the new parent repository revision and its relocated strings/pointers. The
remaining sections and instructions are accounted for; no unexplained binary
difference remains. This is artifact attribution, not gameplay equivalence.

## Retained failures and limits

The first two attempts stopped before compilation: extraction counted a forward
declaration as a second definition, then stale expected function hashes rejected
the real extracted bytes. Independent review checked the entire pinned source,
verified that the base patch has no hunk for this file, and bound the sole
boolean return-type transformation before repairing those expectations. Both
failed attempts remain retained.

The first Wasm compile then rejected dynamic-rounding compiler support. The
reviewed fixture applies that compiler option only to the native build; Wasm
retains its fixed nearest-even arithmetic. No warning suppression, relaxed
precision or source arithmetic change was used. An expanded-control run then
failed on a duplicate output label; distinct conversion labels repaired that
fixture collision before the final pass.

The first two overlay attempts also stopped before compilation: a malformed
hunk, then context generated from raw source instead of the production boolean
transform. Both are retained. Independent review repaired composition and verified the
actual overlay; a conditional-body extractor failure and a noncanonical test
discovery invocation are also retained in the receipt.

These checks assume nearest-even arithmetic and masked exceptions. FPSCR flags,
traps, non-default rounding, NaN payload fidelity, live mode activation and a
matched patched-Slippi execution witness remain unproved. No official Slippi
service is contacted. The existing private Slippi testbed has known loopback
redirects, but those settings and periodic socket sampling do not prove that
every network path stays local. It remains unrun until that restriction can be
verified for the actual client. The separate process-local default-deny fixture
and remaining client-lineage gate are recorded in the
[shield-drop boundary](2026-10-18-ucf-shield-drop.md); they do not establish
patched-client execution. [PR #336](https://github.com/ericvanlare/melee-web/pull/336)
merged as `220e8ad9` after independent review and all 14 exact-head checks
(12 successes and two expected skips). Reviewed and merged trees match.
Vanilla source behavior is unchanged by this private profile hook.
