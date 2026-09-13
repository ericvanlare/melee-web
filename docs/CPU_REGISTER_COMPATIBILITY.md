# CPU register compatibility investigation

This follow-up starts from `6b8bdda2b02a6a5e753e857891e964b5059f817f` and
targets `codex/public-prototype-shell`. PR #10's exact checkpoint passed both
the [push CI run](https://github.com/ericvanlare/melee-web/actions/runs/34733043195)
and the [pull-request CI run](https://github.com/ericvanlare/melee-web/actions/runs/34733044472).
No checkpoint repair or history rewrite was needed.

The goal is to remove the four-player CPU-input difference at source tick 2495
using an explicit shared source compatibility model. The three accepted retail
pairs, their input plans and MWRC recipes remain fixed. Diagnostic captures
observe the original execution separately; they do not replace those references
or supply CPU decisions to the port.

## Established boundary

The [existing DOL audit](CPU_ZERO_KNOCKBACK_ABI.md) identifies the undefined
source path in `ftCo_800AC5A0`. Its only direct retail caller is the state-18
dispatch in `ftCo_800B2790` at `0x800B2AA8`. That caller sets `r30` to the
fighter pointer. The nearly-zero-knockback branch skips the stick-conversion
stores and loads; commands at `0x800AC74C` and `0x800AC75C` therefore consume
the low bytes of incoming `r5` and `r30`, respectively.

The earlier CPU dispatch can leave a stack output pointer in volatile `r5`.
The reaching floor-query path and exact pointer values still require direct
register observation. The compatibility model must derive the relevant source
address relationships; native/Wasm addresses and captured stick constants are
not replacements for them.

The agreeing two-player trace has four state-18 ticks (864–867), all with
nonzero raw knockback. The three-player trace has 130 state-18 ticks for Falco
and 74 for Mario, also with nonzero raw knockback. This is a post-tick inventory,
not a direct observation inside the CPU function. Those cases do not establish
agreement for the zero-knockback residual-register path.

## Investigation and validation scope

Read-only probes must retain instruction pins, registers, pointer identities,
bounded stack bytes, call provenance and fighter/CPU state before and after
the original routine. Raw diagnostic failures remain separate from admitted
evidence. No game memory, register, RNG or executable writes are permitted.

The final comparison must preserve all 3838 four-player core ticks, all 1199
two-player browser ticks, all 4346 three-player browser ticks, the existing
480-tick level-1 and level-9 checkpoints and the 240-tick human regression.
Visible source-drawn browser execution remains authoritative for complete
matches. Headless drawing omissions and expanded camera/draw differences stay
explicit. No reserved holdouts or additional scenarios are part of this work.

Camera arithmetic, preparation draws, native omitted drawing, performance,
Sudden Death, runtime extraction, deployment and controller recording remain
outside this PR. Any source-address context needed from another owner will be
documented as a handoff before changing that owner's files.

Status: diagnostic tooling and source audit in progress; no compatibility fix
or new port-accuracy result is claimed by this initial investigation checkpoint.
