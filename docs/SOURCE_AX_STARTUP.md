# Original AX startup boundary

The checked fixture executes original `AIInit` → `AXInit`, including all nine AX
translation units, original DSP boot/task/interrupt routines and the original AX
initialization callback. It stops after AX enables its first 640-byte AI DMA.
The [scoped receipt](evidence/source-ax-startup-v1.json) records the frozen run.
This is **Compiled / Source identified / Native traced** evidence in checked
Wasm32, with modeled hardware services. It does not execute DSP firmware, emit
PCM, compare original audio output, establish timing, or integrate browser audio.

Run the focused boundary with the pinned dependencies provisioned:

```sh
python3 -m unittest discover -s tests -p test_source_ax_startup_services.py -v
```

The runner compiles the nine original AX units with read-only accessors in their
own translation units. These expose actual arrays, sizes, callbacks and state;
addresses are fixture Wasm addresses, not retail source identities. Original AI,
DSP and OS interrupt-mask routines use checked register and context providers.
Source hashes are verified before compilation and after execution. Generated
sources, commands, binaries, output and failures remain in unique ignored
`work/source-ax-startup-*` directories.

The fixture retains original arithmetic and initialization order. Explained
patches restore the original 32-byte alignment of three AX arrays and bind the
SDK bus-clock read to the declared 162 MHz GameCube profile. The existing AI
callback-stack patch fails explicitly if the unreachable PowerPC stack-switch
path is entered. No original AX startup loop or callback body is replaced.

Source DSP boot writes enqueue initialization mail. Delivery occurs at the
checked return from original `DSPAddTask`, under a masked interrupt owner; the
original DSP handler invokes the actual AX callback. The source routine restores
CPU interrupt state before boot writes, so that earlier restoration cannot be
used to deliver a completion that has not yet been queued. No mailbox write
invokes a callback inline. Register reads, virtual time and process execution
have finite budgets.

Cache backing is initialized before source startup. Flush publishes owned CPU
ranges; invalidation reloads their backing without implicitly publishing writes.
DMA requires explicit output publication and the source-selected output buffer.
The task's image, DRAM span, authored lengths/vectors and callback identities are
checked. Negative controls corrupt actual mailbox/register/task state or omit
publication and require explicit rejection.

The next integration joins original AR/ARQ and HSD Synth using one shared DSP
register bank, interrupt table and context owner. Synth must call AX initialization
in its authored order. This isolated boundary neither supplies the full audio
allocation history nor closes the session's live fighter identity/register-carry
boundary. See [CPU carry](CPU_R5_CARRY.md), [AR initialization](SOURCE_AR_INIT_PROFILE.md)
and [DevCom startup](SOURCE_DEVCOM_STARTUP.md).
