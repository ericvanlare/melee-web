# Original AIInit service boundary

The checked-Wasm fixture executes the pinned SDK `AIInit(NULL)` against a
bounded Audio Interface register provider. This component is separate from
browser audio and the [ARInit profile](SOURCE_AR_INIT_PROFILE.md). The
[scoped receipt](evidence/source-ai-init-profile-v1.json) records source identities,
validation and the exact original comparison, when available. Full audio startup
and full-session equivalence remain open.

## Source and ownership

Original `ai.c` is compiled as Wasm32 C++ with checked integer and pointer widths,
SAFE_HEAP, assertions and floating-point contraction disabled. The explained
`patches/source-ai-callback-stack.patch` replaces only the PowerPC callback-stack
assembly with an explicit failure. The adapter accepts only a null callback
stack. Upstream source bytes are hash-checked before and after compilation;
the checked patch applies to an ignored generated copy.

Typed proxies retain the original MMIO expressions. The default profile starts
with the pinned Dolphin AudioInterfaceManager GameCube reset control, `0x42`.
This path does not enter sample-rate calibration. Four authored AI registers
and the DSP control/DMA registers used by the fixture are accessible. Unknown
registers, unsupported context services and exhausted operation budgets fail.

The fixture emits its startup snapshot immediately after original AIInit returns,
before synthetic DMA, volume or trigger changes. It observes original private
bounds, callbacks and initialization state; it does not assign expected results
to those private fields. Handler identities are checked against the included
source functions. `unmask_requested` records this component's requested bits,
not the original game's global interrupt-mask snapshot. The original SDK
`SetInterruptMask` and `__OSUnmaskInterrupts` bodies execute against owned
physical-mask cells and the checked register proxies. The declared standalone
mask profile begins with all global sources disabled and no local masking;
it does not copy the original game's global mask. Unsupported interrupt banks
fail explicitly. This source service is required: unmasking changes the AI
control register as well as the software mask.

Separate synthetic checks program one owned aligned DMA buffer. Enabling DMA
validates the decoded register span against that object. This checks programming
ownership, not DMA consumption, PCM, interrupts or audible output. Non-null
callback stacks are rejected at the adapter boundary, not tested as a supported
source callback path.

An explicitly selected `--control calibration` mode exercises a deterministic
virtual clock using the pinned GameCube sample-rate divisors. It is synthetic
service evidence, not a hardware-timing or original-startup comparison. A stalled
clock is rejected within a bounded number of operations.

## Original comparison

The retained original-game probe observes verified AIInit entry and return,
using read-only AI/DSP state peeks and guest-memory reads. The first comparison
found a control-register mismatch because the fixture omitted the hardware
writes performed by interrupt unmasking. That failure is retained in the receipt.
Captured values enter only the post-run comparison, never the fixture.

The comparator checks four AI registers, nine source-private fields, two
installed handler identities and the cleared-mask-bit relation. The absolute
software masks, DSP state, clock progress and interrupt timing are outside
this comparison. A shifted private bound must fail the same comparator.

## Reproduction and limits

After provisioning pinned dependencies:

```sh
python3 -m unittest discover -s tests -p test_source_ai_init_profile.py -v
```

Each compile and negative run retains evidence in a fresh ignored `work/`
directory. Missing dependencies skip this local component test; integration CI
must execute it with the pinned SDK provisioned.

AI callbacks, AX initialization, DSP task execution, cache-visible audio buffers,
physical timing, PCM, full `lbAudioAx`, application heaps and live source-address
bindings remain outside this target. This fixture does not establish full-session
equivalence or authorize deployment.
