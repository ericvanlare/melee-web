# Original audio and memory startup fixture

The bounded fixture now calls original `lbAudioAx_8002838C`, followed by
`lbMemory_8001564C` and `lbHeap_80015F3C`. This extends the
[joined Synth boundary](SOURCE_SYNTH_JOINED_STARTUP.md); it does not change the
browser player. The [receipt](evidence/source-lbaudio-memory-startup-v1.json)
records the actual validation and its limits.

The raw audio translation unit owns AR/ARQ/AI initialization, AXDriver,
standard reverb and delay initialization, three SFX bank descriptors and
bookkeeping. Wrappers observe the original calls and delegate to the source
functions. ARInit registers the original routine's allocation-stack storage
with the existing checked service owner. Unsupported chorus, high-quality
reverb and SFX command execution fail explicitly; those paths are outside this
initialization target. DSP firmware execution and PCM output are not modeled.

The post-audio units use the pinned prepared source and same-translation-unit
read-only accessors. The source derives its ARAM root handle and six heap
descriptors from the existing owners and authored table. Private handle
identities are reported as pool indices and relationships. MEM1 bounds are
translated through the independently derived boot arena only after execution.
`lbHeap_80015F3C` initializes descriptors; it does not create the later scene
heaps or execute preload transitions.

## Reproduce

Prepare the pinned dependencies and configure/build `hsd_native_runtime` using
the [development setup](DEVELOPMENT.md). Compile the profiles into fresh ignored
directories:

```sh
python3 -m tools.source_synth_joined_startup --help
python3 -m tools.source_lbaudio_startup --help
python3 -m tools.source_post_audio_allocations --help
python3 -m tools.source_synth_joined_runtime --help
```

Pass the three receipts through `--profile`, `--lbaudio-profile` and
`--post-audio-profile`. Owned execution additionally requires the disc, DOL,
source symbols, pinned source root and SRAM envelope options. The driver values
are independent comparison expectations; they do not replace the raw routine's
actual initialization arguments. Profiles reject stale source, headers,
wrappers and objects before linking.

The focused CI test `test_source_lbaudio_joined_runtime.py` executes the joined
path with explicitly synthetic boot/SRAM inputs and independently evaluated
source bank tables. Owned-input execution is separate. The original comparison
reads captured values only after execution; no captured allocation or CPU
output configures the fixture.

## Investigation stopped

At the owner's request, full-session equivalence work is paused. The first
retained mismatch remains match 0, tick 1776, P4 `input_hex`; the preceding
comparison agreed through tick 1775. This fixture has not advanced that prefix.

The CPU carry component still lacks production call-site hooks and an
independently derived live Fighter identity. A possible smaller allocation
entry is the original VS preload/heap-reset boundary, but persistent archive
ownership, callbacks/compaction and seed ownership at that boundary remain
unproven. Any resumption must establish those inputs before the source VS
allocation suffix and live Fighter binding. Captured addresses, guessed heaps,
or expected CPU output cannot substitute for that proof.

There is no browser, live timing, pixels, PCM or full-session equivalence claim,
and no deployment accompanies this fixture.
