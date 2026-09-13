# CPU zero-knockback register boundary

The four-player development reference exposes a second CPU undefined-local
case, independent of the repaired `ftCo_800ADE48` hitlag carry. At source tick
2495, P2 Mario's retail CPU emits stick bytes `90,40` (signed -112,64), while
the compiled port emits `00,00`. Both independent retail captures agree.
The complete browser run retains this first input difference while RNG, PAD
history, HUD and outcome remain exact. This case is not admitted CPU behavior.

## Observed context

At tick 2494, both paths agree on CPU state 2, target slot 3, attack queue
`[40,41,31]`, command duration 16, cursor 2 and the active command bytes.
At 2495, the hitlag behavior reaches `ftCo_800AC5A0` with zero knockback;
fighter position, motion 228, ground/air state and velocity still agree.
The original function's periodic difficulty branch uses stick locals that it
only assigns when knockback magnitude is nonzero. Its source already labels
the uninitialized use as a bug.

## GALE01 revision 2 instruction audit

The DOL audit distinguishes register residue from the function's local stack:

| Boundary | Original behavior |
| --- | --- |
| `800AC6A8–800AC6D0` | The nearly-zero magnitude branch skips the normal stick conversion stores. |
| `800AC744–800AC74C` | Command `80` receives the existing low byte of volatile register `r5`. |
| `800AC754–800AC75C` | Command `81` receives the low byte of `r30`. |
| `800B27A4`, `800B2AA4` | The state-dispatch caller puts the fighter pointer in `r30` and calls the helper without replacing that carry. |
| `800B46DC`, `800B4754` | The command writer retains the argument and stores its low byte. |

The original static audit found that the kind-4 dispatch entry at `803C5CF8` reaches `800B2F68`, then
`ftCo_800B24B8`, which calls `ftCo_800ADE48` at `800B25DC`. That helper uses
`r5` as a stack output pointer at `800ADE70`. `mpCheckFloor` copies it to
`r17` at `8004F05C` and can replace `r5` with its own stack output at
`8004F188`. There is no subsequent scalar definition on the early-return path
at `800AE290`. The local conversion slots at `r1+10` and `r1+18` are skipped
in the zero-knockback case; copying them would model the wrong source.

That floor-only explanation was incomplete for the first divergence. The
subsequent [register investigation](CPU_REGISTER_COMPATIBILITY.md) observed
the no-target call at `800B276C`, followed by `HSD_Randf` at `800AE21C`.
The RNG routine replaces `r5` with the seed pointer before state-18 dispatch.
At tick 2495, the consumed X value is therefore the seed pointer's low byte,
not the earlier floor-output stack pointer's. The new diagnostic prefix is
compared against both fixed gold captures; it replaces neither complete
reference. Stack residue remains relevant to other call paths.

## Required follow-up

An accurate compiled implementation needs the original fighter-address carry
and relevant volatile-register/stack-output residue modeled explicitly in the
shared source layer. Native Wasm addresses cannot stand in for retail addresses.
The earlier boolean hitlag carry fix is insufficient for these byte-valued
inputs. This is a broader ABI compatibility problem than hydrating CPU tables
or enabling a missing action row.

No replacement neutral rule or per-scenario stick constants were added.
Recorded CPU outputs remain observation fields and never enter the accepted
input recipe. The current native/browser mismatch stays in the strict corpus
report. Future work must validate its general model on fresh independent
contexts, difficulties and player arrangements, including unseen zero-knockback
hitlag cases. These development captures cannot become holdouts retroactively.
