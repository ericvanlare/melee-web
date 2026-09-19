# AX audio resampler contract

This document records the bounded contract used by the browser audio boundary.
It pairs the preserved port behavior with independently inspected original AX
DSP observations and AX state definitions. Those observations support only the
specific facts identified below, not every arithmetic compatibility rule.
It does not claim that this C implementation is a bit-exact replacement for the
hardware DSP.

## Primary evidence

The primary program is the pinned original source
`.deps/melee/extern/dolphin/src/dolphin/ax/DSPCode.c`, whose `axDspSlave`
contains 3312 16-bit words (source-file text SHA-256:
`8807c54266535396ea190e40e370fa045a3461ddbe52c5a5ef09913e4baaddb9`).
The source is the raw Nintendo AX DSP program; the addresses below are word
addresses in that array. An existing local analysis-only disassembly was used to label the words and
checked against the pinned program. It is not tracked, an HLE implementation,
or a numerical substitute; the instruction addresses refer to the pinned source.

- The SRC dispatcher loads the selected routine at `0x026b` and calls it at
  `0x026d`.
- Polyphase SRC starts at `0x05a8`, linear SRC at `0x065d`, and direct SRC at
  `0x0707`.
- `0x05e3` loads the selected coefficient bank, `0x05e5-0x05e8` loads the
  Q16.16 ratio and source phase, and `0x05eb-0x05f2` copies four retained
  samples into the DSP work ring.
- Polyphase indexing at `0x060e-0x0615` masks the phase with `0x01fc` after
  shifting by seven. Each selected bank therefore has 128 four-word rows
  (512 signed words).
- Polyphase output at `0x0643-0x065a` uses a 40-bit accumulator and stores its
  middle word at `0x0659`. Linear output at `0x06ef-0x0704` uses the DSP
  multiply/accumulate path after `SET15`, `M0`, and `SET40`.
- Filtered paths save four retained samples and the low phase word at
  `0x062b-0x0634` and `0x06d7-0x06e0`.
- Direct SRC reads a 32-sample block at `0x0743-0x0748`, saves the four trailing
  samples at `0x074b-0x0755`, and writes the phase word from `ACL0` at
  `0x0757`. The preceding `0x0756` clears `ACC1`, not `ACC0`; that pair alone
  does not prove that the saved phase becomes zero.

Selector mapping is from the original public AX setter in
`.deps/melee/extern/dolphin/src/dolphin/ax/AXVPB.c`: `AX_SRC_TYPE_4TAP_*`
selects `srcSelect=0` and `coefSelect=0..2`; `AX_SRC_TYPE_LINEAR` selects
`srcSelect=1`; and `AX_SRC_TYPE_NONE` selects `srcSelect=2`. The same source's
`AXSetVoiceSrcRatio` converts a float to truncated `65536.0f * ratio` and
caps it at `0x40000` before storing `ratioHi`/`ratioLo`.

## C boundary behavior

`MeleeWebAudioResample.history[0..3]` is chronological, with index zero at
the current source position. `fraction` is the low 16 bits of a non-negative
Q16.16 phase. A filtered call forms `total = fraction + ratio`, consumes
`floor(total/65536)` source samples by shifting the four-sample window left and
appending callback data, saves `total % 65536`, and then renders from that
updated history and phase. This is the compatibility order at the C boundary.
Consequently, a filtered call with ratio zero consumes no input, while a call
with ratio `r` consumes `floor((fraction+r)/65536)` samples.

For selector 0, the row is `(fraction >> 9)` after that phase update and the
four signed coefficients are consumed in increasing order. The C boundary
accumulates four 16x16 products, divides by `2^15` with an arithmetic (floor)
shift, then clamps to signed 16-bit output. For selector 1, it computes the signed linear
blend

```
(history[0] * (65536 - fraction) + history[1] * fraction) / 65536
```

using the same arithmetic floor and signed 16-bit clamp. Selector 2 reads one
source sample directly for each output, shifts the history window, ignores
ratio, and leaves `fraction` unchanged.

## Remaining hardware uncertainty

The port leaves fraction unchanged in direct mode. The original direct path
writes its saved phase at `0x0757` using accumulator state. The local disassembly
shows `CLR ACC1` followed by a store of `ACL0`, so the clear instruction alone
cannot establish that the stored fraction is zero. Instruction semantics and
incoming accumulator state must be verified before calling this a confirmed
port deviation. Use a focused original-DSP direct-to-filtered transition trace;
same-output comparisons against the old port cannot answer that question.

The raw program proves the fixed-point instruction sequence, phase/index
addressing, and state writes. It does not provide the DSP instruction-set
numerical definition needed to settle every signed product-middle-word edge
case from source alone. In particular, no explicit rounding constant or
saturating instruction occurs in either SRC output loop; the exact behavior of
DSP product scaling, accumulator middle extraction, and overflow at extreme
coefficients still needs an owned hardware/DSP trace. The C boundary therefore
uses the deterministic arithmetic-floor and final signed-16-bit clamp above as
its documented behavior. This is an implementation contract and an explicit
uncertainty record, not a claim of clean-room legal clearance or hardware
bit-exactness.

## Implementation provenance

The implementer inspected the original program, source setters and existing
local disassembly without reading the old GPL adapter. The lead supplied the
port compatibility specification after having inspected that adapter, then
reviewed and edited the new implementation. This process is recorded factually;
it is not a formal clean-room or legal-clearance claim. The historical source
and the separate Dolphin reference tools retain their original notices.
