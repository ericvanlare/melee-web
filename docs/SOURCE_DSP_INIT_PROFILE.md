# Original DSP startup protocol boundary

This standalone checked-Wasm fixture executes pinned SDK `DSPInit`,
`DSPAddTask`, `__DSP_boot_task` and `__DSPHandler` against a bounded mailbox
and interrupt service. The [scoped receipt](evidence/source-dsp-init-profile-v1.json)
records source identities and validation. This is synthetic service evidence;
it does not execute DSP firmware or establish audio or full-session equivalence.

## Source and ownership

The builder includes unchanged `dsp.c`, `dsp_task.c` and `DSPCode.c` bodies,
with their expected hashes checked before generation and after execution.
A fixture header redirects the original MMIO expressions through a typed
register proxy. Exact `SetInterruptMask` and `__OSUnmaskInterrupts` bodies are
extracted from pinned `OSInterrupt.c`; checked mask cells and register access
implement their service boundary. Source integer, pointer and task layout
checks protect the Wasm32 ABI. SDK DEBUG assertions, SAFE_HEAP and runtime
assertions are enabled; floating-point contraction is disabled.

The task uses the owned SDK AX image and authored boot fields, with a fixture
DRAM buffer and callback. The image is checked using Dolphin's canonical
big-endian-word Ector hash. This identifies the supported HLE protocol image;
it is not firmware execution or a claim about Wasm memory byte order.
The fixture does not run `AXOut` or its original private init callback.

The provider checks all ten boot mails, the ROM handshake and init response.
Mailboxes preserve high/low access order and bounded polling. Init completion
is queued until task submission returns and interrupts are enabled. The pump
then dispatches the original handler with interrupts masked. The handler must
observe the published task, transition its state and invoke the registered
fixture callback on the temporary exception context before restoring the caller.

Original interrupt unmasking programs the hardware DSP mask. A pending event
sets the DSP status bit; the original handler acknowledges it with write-one-to-
clear semantics. The declared initial status and disabled masks are synthetic
profile inputs, not copied original-game observations. Other DSP operations,
registers, images and protocol sequences fail explicitly.

## Validation and limits

After provisioning pinned dependencies:

```sh
python3 -m unittest discover -s tests -p test_source_dsp_init_profile.py -v
```

Each run retains generated source, checked JS/Wasm, source hashes, compiler
output and positive/negative logs under a fresh ignored `work/` directory.
Negative controls exercise changed image data and pointers, invalid lengths,
mail ordering, unsupported registers/status, stalled mailboxes and invalid
dispatch. The receipt gives the exact executed inventory.

There is no retail comparison for this component. AI DMA consumption, AX command
execution, DSP task switching, physical interrupt timing, PCM, full synth/audio
startup and live browser integration remain open. No deployment follows from
this component result.
