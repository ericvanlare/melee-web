# Original Synth startup on shared audio services

This checked Wasm fixture keeps the original HSD allocation prefix alive, then
executes original ARInit, ARQInit, AIInit and HSD_SynthInit. Synth owns the AXInit
call. Original AXOut, DSP task boot and DSP interrupt handling enable the first
AI DMA; the first Synth DevCom request then completes through original AR and
ARQ handlers. See the [scoped receipt](evidence/source-synth-joined-startup-v1.json)
for tested source identities, retained failures and validation.

One platform owner supplies the DSP register bank, interrupt masks, SDK contexts
and cache spans. The [AR service](SOURCE_AUDIO_AR_SERVICES.md) owns decoded DMA
requests and binds them to a shadow pointer and lifetime generation. Original
OS interrupt-mask routines publish hardware enable bits. Source handlers retain
acknowledgement and callback ownership. DMA completion is an explicit functional
schedule, not evidence of hardware timing.

Same-translation-unit accessors expose the authored AX objects and two DevCom
relay buffers. Cache flush/store publishes bytes; DMA consumes only owned,
published subranges. Probe registrations expire after ARInit. The test starts
ARAM with nonzero bytes and verifies the initial clear remains deferred, clears
exactly its source reservation after completion, and preserves adjacent bytes.
A direct source audio-heap allocation remains live across that completion.

The standalone parameter probe derives driver arguments from pinned source and
owned DOL tables. The boot adapter derives arena and heap roots from the owned
disc, apploader and DOL. The original sound-mode getter consumes owned SRAM
settings behind a checked read-only lock using the shared interrupt owner. No
allocation capture configures these inputs. Captured allocation metadata is
used only after execution to compare the three AR reservation sizes, final AR
stack and remaining block count; altered comparison fields must fail.

The source ABI retains four-byte MSL bool, original SDK structures and ordinary
float arithmetic. The source compilation helper records pristine and prepared
hashes and reviewed compatibility patches. The runtime uses actual configured
dependencies. Aurora's heap boot requires its clock object, so a named linker
wrap routes OSGetTime calls to the checked audio clock without duplicate symbol
selection. The fixture is Node-only and reads the explicitly supplied SRAM file;
it is not a browser runtime target.

## Reproduce locally

Configure the pinned browser dependencies with
`python3 scripts/build.py --configure-only` first. The runtime builder refreshes `hsd_native_runtime` and
requires its configured archive closure. Use new artifact directories:

```sh
python3 -m tools.source_synth_joined_startup --artifact-dir work/synth-profile
python3 -m tools.source_synth_joined_runtime \
  --profile work/synth-profile/receipt.json \
  --artifact-dir work/synth-runtime --run \
  --owned-dol assets-local/main.dol --owned-disc assets-local/game.ciso \
  --owned-symbols .deps/melee/config/GALE01/symbols.txt \
  --source-root .deps/melee --owned-sram-envelope assets-local/SRAM.raw
```

The focused tests create fresh synthetic inputs and binaries. Runtime tests
explicitly skip when the local CMake dependencies are unavailable; such skips
provide no joined execution evidence. Owned runs derive inputs directly from
the supplied disc/DOL/source and actual 68-byte SRAM envelope. JSON input receipts
are accepted only by the separately labeled synthetic test API.

## Deliberate limits

This boundary stops at HSD_SynthInit and its first deferred request. It does not
execute the complete lbAudioAx/AXDriver setup, standard reverb/delay setup, SFX
bank loads, lbMemory/lbHeap, preload or scene allocations. Unreachable DVD
requests fail explicitly. No audio frame callback or DSP firmware executes, so
there is no PCM claim. Nested caller contexts and general inbound audio DMA are
outside this top-level completion scenario.

The HSD prefix reports its own earlier omissions before the joined audio phase;
that prefix record alone does not describe the final joined boundary. Full-session
equivalence, live fighter allocation binding, timing and browser validation remain
open. No deployment follows from this fixture.
