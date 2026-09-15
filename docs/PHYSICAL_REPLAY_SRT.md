# Physical replay: joint-matrix rounding

The four-stock Falco versus level-6 CPU Marth recording on Yoshi's Story
exposed a shared HSD matrix rounding error. Preserving four original fused
operations in `HSD_MtxSRT` removes the first fighter-position difference at
source tick 473. The full visible browser replay now matches every declared
fighter field for all 9,019 ticks except 17 `input_hex` samples, first at tick
901. Those samples belong to the separate CPU register-carry problem.
The [bounded evidence receipt](evidence/physical-replay-srt-v1.json) binds the
inputs, outputs, scalar fixture, diagnostic observer and regression comparisons.

## Cause and correction

At tick 473 (match frame 350), both independent original sessions report Falco
X as `426abaaa`; the prior native and browser paths report `426abaab`.
`mpColl_800454A4_RightWall` receives an already different collision box:
its half-width is `402baab0` in the port and `402baaa8` in the original.
Changing wall arithmetic would therefore address the wrong boundary.

A temporary passive Dolphin observer sampled the end of `mpColl_LoadECB_JObj`
at source PC `8004290c`. It read the collision data and the six source joints'
parent chains without requesting matrix reconstruction or writing guest memory.
The corresponding native probe sampled the same function's return. Their
local scale, Euler rotation, translation, flags and parent matrix agree at the
first differing joint. Its computed world matrix differs before either wall
correction or drawing consumes it.

The pinned DOL contains `fmsubs` / `fmadds` at `8037a388`, `8037a38c`,
`8037a3b4` and `8037a3b8` in `HSD_MtxSRT`. The downstream patch uses explicit
`fmaf` at those four boundaries, retaining the separately rounded inner
products, original MSL trigonometry, scale correction and paired-single matrix
concatenation. It adds no character, stage, tick, address or expected-output
condition. Global floating-point contraction remains disabled.

`tests/test_gameplay_srt.py` exercises eight observed operand sets selected
where the local pose and parent matrix agree. Both native clang and checked
Wasm (`ASSERTIONS=2`, `SAFE_HEAP=1`) reproduce all original expected matrices.
The pristine unfused source reproduces all eight prior native matrices as a
negative control. Expected matrices are test outputs only. Existing trig,
quaternion, concatenation and pose tests also pass. The full repository suite
passes 895 tests with 37 optional skips; the six focused math tests pass. Both
the Release browser and checked headless targets build successfully.

## Replay evidence and limits

The diagnostic Dolphin build uses the existing passive JIT callback and
strict SI input replay. It completes all 21,268 recorded SI operations and
47,658 observer events, including 9,019 source ticks, 9,011 draws, the result
and teardown. Every existing semantic event matches the original physical
recording. This is a diagnostic comparison across an explicitly changed
observer build; it does not bypass the application's same-environment replay
admission check. The installed application and Dolphin remain unchanged.

The private probes and complete logs remain outside Git. Their patch, source
revision, build and output digests are retained with the experiment. The
observer probes additionally record CPU registers at `800ade48`, `800ae220`,
`800ac5a0`, `800ac74c`, `800ac75c` and `800b46b8`. These are bounded offline
observations, not interactive performance measurements or allocation tracing.

The corrected browser completes the original 9,019-tick human-input recipe,
publishes Falco's win and tears down. Position, motion, animation, velocity,
knockback, facing, ground/air state, damage, shield and stocks agree for both
fighters at every declared tick. Supplied human/device input, PAD history,
RNG, match clock, HUD, magnifier and result observations also agree. Strict
comparison still reports divergence: CPU input first differs at 901, camera
FOV at zero, subject Z at 160, and eight additional source draws remain. The
previous subject difference at 73 is removed. Pixels, PCM and performance are
not accepted by these observations.

The headless trace still stops at 5,901 / 9,019 ticks with the existing
unsupported pause/exit result. Its first fighter difference moves to 901;
its RNG and match-clock differences remain at 4,468 and 5,787. The incomplete
prefix is diagnostic evidence and is not admitted as a complete replay.

The existing development regressions retain their original comparison scope:

| Workload | Headless native | Visible browser |
| --- | --- | --- |
| Mario / Fox CPU 1, FD, 1,199 ticks | Core and CPU agree through completion; exact prior core trace hash | Core and CPU agree through completion; subject fields now also agree |
| Marth / Falco CPU 5 / Mario CPU 9, Yoshi's Story, 4,346 ticks | Prior incomplete failure and first RNG difference at 2,241 remain | Core and CPU agree through completion; first subject difference moves to 2,862 |

Camera and source-draw differences remain compared. The four-player workload
and reserved holdouts were not run. No broader content or performance
admission follows from these development cases.

## CPU prerequisite established at tick 901

At match frame 778, the original `HSD_Randf` call at `800ae21c` leaves the
selected seed pointer in volatile `r5`. The passive probes observe that value
unchanged at `ftCo_800AC5A0` and the command-80 writer call at `800ac74c`.
The command-81 call at `800ac75c` takes the fighter pointer retained in `r30`
by `ftCo_800B27A4`. Their low bytes produce the two nonzero stick operands
that the port currently leaves zero.

This establishes the producers for this recording, not a portable substitute
for them. The source's near-zero-knockback path skips assignments to those
locals. CPU X is visible in the serialized fighter input; the observations
do not establish a later position, damage or motion consequence. The full
browser field audit finds only the 17 differing input samples among the
declared fighter fields.

Integration still requires independently deriving the original selected RNG
global and fighter allocation identities and preserving the source register
carry. Captured pointers, command bytes and CPU decisions must remain expected
outputs, never recipe or model inputs. This change does not extend allocation
history, alter the tick-2,495 path, or modify draft PR #16 or its evidence.

For local reproduction, run the focused math tests, build the development
runtime and headless trace, and use `capture_cpu_native.py` /
`capture_cpu_browser.mjs` with the hash-bound human recipe in the receipt.
Assemble a fresh local runtime with `prepare_prototype.py`; retain each output
in a new private directory and run `compare_reference_capture.py` against the
unchanged derived recording. The maintained scalar fixture is the portable
reproduction path when private recordings are unavailable. No game assets,
private saves, raw capture payloads or personal paths are tracked.
