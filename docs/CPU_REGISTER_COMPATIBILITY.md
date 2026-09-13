# CPU register compatibility investigation

**The four-player tick-2495 divergence remains unresolved.** This branch adds
read-only original-register diagnostics, strict comparison of their bounded
source-state prefixes, and a port allocation-address diagnostic. It does not
implement a gameplay compatibility model or claim a four-player accuracy pass.
The follow-up [source-address component](SOURCE_ADDRESS_CONTEXT.md) now models
the supported original allocation operations and defined register-word
consumption. It is tested against untouched SDK/HSD code in native and Wasm
builds. The original context provider and CPU call-site integration remain open.

The isolated branch started from `6b8bdda2b02a6a5e753e857891e964b5059f817f` and
originally targeted `codex/public-prototype-shell` in draft PR #13. PR #10's exact checkpoint
passed both the [push CI run](https://github.com/ericvanlare/melee-web/actions/runs/34733043195)
and the [pull-request CI run](https://github.com/ericvanlare/melee-web/actions/runs/34733044472).
No checkpoint repair or history rewrite was needed.

## Original behavior

The verified GALE01 revision-2 DOL has SHA-1
`08e0bf20134dfcb260699671004527b2d6bb1a45`; the pinned decompilation is
`b43912cc78606f96c9569f5d6229bc9d7e265ea5`. The original instructions and
new four-player register capture establish this chain:

| Boundary | Original behavior |
| --- | --- |
| `8006ABD4`, `800B3928` | Fighter callback calls CPU dispatch, then state dispatch. |
| `800B27A4` | The state dispatcher retains the fighter pointer in `r30`. |
| `800B2AA8` | The only direct caller of `ftCo_800AC5A0` dispatches state 18. |
| `800AC6CC–800AC6D0` | A nearly-zero vector skips the normal stick conversion assignments. |
| `800AC74C` | Command `80` consumes the existing low byte of volatile `r5`. |
| `800AC754–800AC75C` | `r5 = r30`; command `81` consumes the fighter pointer's low byte. |
| `800B46DC`, `800B4754` | The command writer retains its argument and stores its low byte. |

At source tick 2495, P2 Mario is in original motion 228 (`ReboundStop`),
grounded, with zero velocity and zero knockback on all axes. Hitlag and SDI
flags are set. CPU timer `x7C` is 330: `330 % 120 = 90` exceeds level 3's
threshold `120 - 3 * 12 = 84`. The periodic branch reaches the nearly-zero
vector test, whose result is true. Neither conversion local gets assigned.
`ftCo_0A01.c` already marks this uninitialized-local use as a bug; the original
machine instead consumes register values.

### X input: the RNG seed pointer

The earlier [static audit](CPU_ZERO_KNOCKBACK_ABI.md) found a possible floor
stack-pointer carry. New probes establish the actual reaching definition:

1. `mpCheckFloor` returns at `800ADEC0` with `r5 = 804EE844`.
2. The no-target route through `ftCo_800ADE48` calls `HSD_Randf` at
   `800AE21C` while deciding the hitlag transition.
3. At `80380534`, `lwz r5,-0x570c(r13)` loads `seed_ptr`. `8038053C`
   reads the seed through this pointer; `8038054C` writes the updated seed.
   No later instruction replaces `r5` before return at `8038057C`.
4. CPU command reset `800B4A78` through `800B4AAC` preserves `r5`. The
   transition returns through `800AE790`, then `800B2770`.
5. `ftCo_800AC5A0` receives `r5 = 804D5F90`, the original seed pointer.
   Command `80` consequently receives byte `90` (signed -112).

This is a pointer identity, not the numerical RNG result. In original
`sysdolphin/baselib/random.c`, `seed_ptr` points to `seed`, and `HSD_Randf`
updates `*seed_ptr`. A production model must derive the source identity from
the original global/SDA context. These debugger addresses pin observations;
they are not constants installed in the game port.

### Y input: the fighter allocation pointer

The dispatcher carries P2's fighter pointer `80CA7740` in `r30`. The zero-vector
path does not overwrite it with a converted Y value, so command `81` receives
byte `40` (64). Retail emits `809081408e017f7f`; the existing port emits
`800081008e017f7f`. Both fixed retail captures agree on the former.

The capture retains raw GPRs, F0–F6, stack/local bytes, fighter/GObj pointer
relationships, knockback/CPU blocks, allocator globals and thread context
around these calls. Those bytes are not supplied as CPU input to the browser.

## Other callers and agreeing references

The audit covered kind dispatch, target/no-target common behavior, state
dispatch, floor query, RNG and command reset. The first event's residue must
not become a rule for every call:

- Common-routine RNG calls at `800AE110`, `800AE170` and `800AE21C` can leave
  the seed pointer. Already-active state 18 can instead retain a floor-query
  value, including an intentional zero.
- `gm_8016C75C` preserves `r5` on its cached path. Recomputing through
  `gm_80166378` can leave an active team's standings-object pointer plus
  `0x30`. Always substituting the seed pointer would be wrong.
- Before state dispatch, command setup and `ftCo_800ADC28` execute. The
  latter has original character-specific charge-cancel paths; a future model
  must follow those semantics, not add a supported-roster exception.
- At `800AC76C` and `800AC77C`, the explicit neutral branch deliberately sets
  `r5 = 0`. Normal nonzero-vector conversion also replaces the incoming values.

New 2P probes at ticks 864–867 observe the periodic guard taking the explicit
neutral route. Incoming residue differs (seed pointer at 864, floor stack
pointer at 866), but both commands deliberately become zero. New 3P probes at
278–281 observe Mario's nonzero-knockback conversion writing X=-119 and Y=43,
independent of residue (seed pointer at 278, zero at 280). The complete 3P
sidecar inventory contains 130 state-18 ticks for Falco and 74 for Mario, with
nonzero post-tick knockback. That inventory is not an inside-function proof
for every tick. These observations explain the agreeing paths without claiming
they validate the skipped-conversion case.

## Prerequisite for a general compiled model

One consumed byte depends on original allocation history. `Fighter_800679B0`
initializes an object allocator for size `0x23EC`, alignment four. The observed
original object heap is null: new cells use the OS heap, whose cell size is
`roundup(0x23EC + 0x20, 32) = 0x2420`. Alignment fixes only the low five bits.
The object allocator reuses its per-type free list after fighter teardown.

The 3P Mario allocation ends in `C0`; the 4P Mario allocation ends in `40`.
Both captures have the same main-heap bounds. Character identity, slot number
or a single arena base cannot explain the difference. GObj, fighter attributes,
archive and joint allocations interleave with fighter creation. The port's
fresh allocator and different asset preparation do not reproduce that history;
its 4P P2 address ends in `20`.

The future model needs source global/SDA identities, original virtual allocation
identities/lifetimes from the declared application context, and explicit
call-site register carry for relevant source stack/object temporaries. Every
intervening call must preserve or replace the carry according to its original
instructions, including deliberate zero. A shared compiled `ftCo_800AC5A0`
boundary can then consume those values with defined integer operations only
on the skipped-conversion path. Normal conversion and explicit neutral commands
retain their original logic.

This is compiled source compatibility, not a PowerPC interpreter. It requires
source-memory context that the current match-only bootstrap does not supply.
Captured fighter addresses, output bytes, native pointer bytes or guessed
neutral input cannot substitute for it. The new address-only component makes
the allocator portion executable without inventing that context or installing
a gameplay fallback. Its supported modes, differential tests, missing context
and required shared-runtime boundary are documented in
[Source allocation identity prerequisite](SOURCE_ADDRESS_CONTEXT.md).

## Diagnostic-checkpoint validation and retained failures

The results in this section are retained evidence from checkpoint `d2c5db3`;
they are not fresh comparisons of a branch reconciled with current main.

The three gold pairs, full input plans, scenarios and MWRC recipes remain
frozen. New diagnostic processes execute bounded prefixes of the same human
plans with original drawing enabled. CPU decisions remain source-generated.
Strict reports compare each prefix against both complete golds; register
sidecars remain diagnostic-only and cannot become replay references.
All three corrected prefixes agree exactly with both respective gold captures,
including the four-player prefix through tick 2496.

| Fresh run | Result |
| --- | --- |
| 2P visible browser | All 1,199 core ticks exact against both golds; CPU decision domain exact. |
| 3P visible browser | All 4,346 core ticks exact against both golds; CPU decision domain exact. |
| 4P visible browser | Completes all 3,838 ticks; first core difference remains tick 2,495, `fighters[1].input_hex`; CPU command first differs at the same tick. |
| 4P native headless | Completes 3,838 ticks; first core difference at 1,871 in RNG/damage. Gameplay-relevant drawing remains omitted; no full-match pass. |
| Original 2P diagnostic | 868-tick prefix; window 864–867; 346 sidecar records; no read errors. |
| Original 3P diagnostic | 282-tick prefix; window 278–281; 237 records; no read errors. |
| Original 4P diagnostic | 2,497-tick prefix; window 2,494–2,496; 235 records; no read errors. |

Complete browser reports retain all frames, first differences by domain, draw
counts and outcomes. In 4P, RNG, supplied PAD/history banks, HUD, magnifier and
match outcome remain exact despite the CPU/fighter-input difference. Expanded
camera/subject and draw checks remain red: 4P camera interest first differs at
184 and subject bits at 74; actual draws are 3,838 versus 3,836, with extras at
2,268 and 3,269. The 2P/3P camera/subject and preparation-draw differences remain
open as well.

The 480-tick level-1, 480-tick level-9 and 240-tick human exact regressions are
inherited evidence at `6b8bdda`, not newly executed checks on this branch.
Gameplay behavior is unchanged. They must be rerun after a shared compatibility
fix, alongside the complete matches and focused call-site tests. New component
tests validate allocation/register representation only, not CPU caller behavior.

Early diagnostics remain failures: an initial attempt was interrupted; three
later runs tried to read the SDK's terminal stack backchain `FFFFFFFF`. The
corrected observer follows `OSDumpContext`'s zero-or-minus-one termination rule,
retains the terminal word, and still rejects other read errors. Failed raw
outputs and metadata were not rewritten or promoted.

Build/test receipts, frozen-input hashes and exact comparison hashes are in
[the evidence ledger](evidence/cpu-register-diagnostics-v1.json). Browser runs
used real HTTP and headed Chrome, checking immutable artifact hashes before
and after each run. These are development state comparisons, not performance
admission: the machine was shared with active debugger processes.

The full suite passed 619 tests with 46 optional target/fixture skips. A final
CLI report-overwrite guard was then checked by all nine focused prefix tests.
The affected Release browser and trace builds passed. Skip reasons are retained
individually in the ledger; skipped checks are not claimed as passes.

## Reconciled prerequisite validation

The prerequisite was preserved in `0e74ff2`, then main `e24e296` was merged
normally in `224758c` with parents `0e74ff2` and `e24e296`. There were no merge
conflicts, rebases or force pushes. Main's exact automatic
[Verify run](https://github.com/ericvanlare/melee-web/actions/runs/34747201211)
succeeded. The earlier permission failures remain in their original ledger;
GitHub, Git writes and local sockets worked in the continuation task.

Fresh Release `gameplay_menu_browser` and `gameplay_retail_trace` builds passed.
The full local suite ran 673 tests with 46 optional-target/fixture skips and no
failures or errors; the subsequently added browser-observer regression passed
separately. All five source-address component tests pass, with complete retained
synthetic differential streams. An independent allocator review found no
actionable defects within the documented supported boundary. These checks do
not establish original allocation history or CPU call-site semantics.

The frozen development corpus was rerun in headed Chrome 153.0.8010.36 over
real loopback HTTP, using a new immutable snapshot of the rebuilt development
runtime. Each full run retained unchanged served-artifact hashes, a visible
640×480 source framebuffer, complete output, and zero browser errors. CPU
decisions remained independently generated. Both original ledgers and all 15
frozen reference/recipe/scenario/input-plan files remain unchanged.

| Fresh comparison against both retail golds | Result |
| --- | --- |
| 2P visible browser | All 1,199 core ticks and CPU decisions exact. Camera far plane differs at tick 0; P2 subject Z at 82. |
| 3P visible browser | All 4,346 core ticks and CPU decisions exact. Camera FOV differs at tick 0; P1 subject Z at 67; extra draws at 1,598, 2,599 and 3,600. |
| 4P visible browser | Completes 3,838 ticks; first core/CPU-input difference remains at 2,495 with the same command bytes. Camera interest differs at 184; P1 subject Z at 74; extra draws at 2,268 and 3,269. |
| 4P native headless | Completes 3,838 ticks; first RNG/damage difference remains at 1,871. Gameplay-relevant drawing remains omitted. |

The source-address component is still **not linked into gameplay**. This is a
validated prerequisite suitable for separate merge review; resolving tick 2,495
requires the original source-context provider and caller/clobber integration
described above. The camera, preparation-draw and headless differences remain
open. No performance, pixel, PCM, physical-input or holdout acceptance is claimed.

The browser harness needed a bounded observation repair after main moved
`retailRun` into a private module. It now observes `Module.print`/`printErr`
before the generated loader captures them, preserves the original callbacks,
and rejects missing or overflowed observations. Its retained core/timer streams
match the runtime's complete exports byte for byte. A first two-player attempt
failed after 363 ticks with zero-sized WebGPU swapchain errors at a full-page
screenshot boundary; its partial trace, errors and invalid comparison remain
retained. Fresh runs keep the canvas in view and use viewport screenshots, with
the same compiled runtime. No renderer fix or failure waiver was applied.

The [reconciled evidence ledger](evidence/cpu-register-reconciled-v1.json) records
the precise build identities, full comparisons, first differing bits, skip
reasons and retained failures. Exact-head CI is recorded on draft PR #13.

## Historical integration handoff

Thread 1 owns runtime/performance integration; thread 3 owns shared-player
extraction. The eventual source-address/carry model belongs in the shared
compiled runtime used by both entry pages, initialized from the declared source
context and retained through fighter teardown. The public mount API must not
accept scenario stick overrides or recorded retail CPU decisions.

The only existing compiled file touched is `src/gameplay_cpu_observation.c`,
adding an opt-in address diagnostic; it may conflict with parallel observer
changes. `docs/CPU_ZERO_KNOCKBACK_ABI.md` corrects the earlier hypothesis.
Other files are new CPU tooling, tests and documentation. No `runtime.html`,
renderer/cache, performance, deployment, Cloudflare or shared-player extraction
files changed. No additional scenarios, reserved holdouts or merges were run.
