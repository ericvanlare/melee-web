# Desktop rollback diagnostic

The optional downstream client patches `patches/0003-desktop-rollback-diagnostic.patch`
through `patches/0006-jitter-reorder-native.patch` are enabled only when
`SLIPPI_ROLLBACK_DIAGNOSTIC_CONFIG` names a valid JSON file. The ordered patch
list and target tree are pinned by `client.lock.json`.
The process writes JSONL observations to the config's `log_path`; both paths are
operator-owned and must remain outside Git.

The config schema is `melee-web-slippi-rollback-diagnostic-v1`:

```json
{
  "schema": "melee-web-slippi-rollback-diagnostic-v1",
  "enabled": true,
  "rng_offset": 4660,
  "log_path": "/private/run/p1/rollback.jsonl",
  "stage_id": 32,
  "input_profile": {"name": "mario-fd-rollback-v1", "role": 1},
  "overlay": null,
  "transport": {"action": "hold", "frame": 98, "release_frame": 104}
}
```

`rng_offset` is the explicit 16-bit match offset. `stage_id` is either null or
exactly `32` (Final Destination), and is applied only when the normal selection
request is random. The overlay changes exactly the eight network PAD bytes on
its first forward send for the selected source frame and leaves the four
status/padding bytes in the guest TXB untouched. A `hold` retains every normal
outgoing PAD packet with `frame <= packet_frame < release_frame`, then sends the
held packets in order immediately before the release-frame packet. The bounded
interval is at most eight frames; diagnostic JSONL records each packet hash and
the interval begin/release counts. `drop` remains a single packet case and
retains the ordinary redundant PAD history. `duplicate` sends role 2's frame-98
packet twice and requires two receiver rows with the same packet hash. `jitter`
holds frame 98 until frame 99 and requires receiver order `[98, 99]`; `reorder`
sends frame 99 before the held frame 98 and requires receiver order `[99, 98]`.
Both delayed recipes require the receiver's native `inputs_to_copy` result and a
sender/receiver hash join. Their interval is one frame, and all transport fault
recipes are role-2-only where the native patch requires it. Use `action: "none"`
with null frame fields to disable impairment. Transport `payload_hash` values use
FNV-1a64 for packet correlation; artifact, config, and replay identities use
SHA-256.

For the pinned HOLD recipe, only the eight network PAD bytes are changed: wire
frames 98 through 103 are held and released in order immediately before wire
frame 104. The trailing four status/padding bytes remain untouched.

The optional `input_profile` must name `mario-fd-rollback-v1` and role `1` or
`2`. It generates neutral input before frame 90; role 1 holds right on frames
90-139, neutral on 140-149, then left from 150 onward. Role 2 holds left on
90-126 and A on 96-100, neutral otherwise. The profile replaces only the first
eight PAD bytes on every forward send and preserves the trailing four bytes.

The authored parser and canonical writer are in
`tools/slippi_rollback_diagnostic.py`. The writer creates a fresh config
exclusively and refuses to overwrite an existing operator file. Invalid keys,
ranges, byte lengths, disabled active controls, malformed preservation arrays,
version-mismatched preservation sizes, and out-of-bounds ODB/RXB observations
fail the diagnostic rather than silently falling back. Native diagnostic JSONL
rows include `observer_context` (`exi` or `transport`), zero-based
`game_sequence` (initially `-1`, advanced at each context's match reset), and
monotonic `event_sequence` within each observer context. EXI also records each
pinned `CMD_RECEIVE_GAME_INFO` boundary as `recording_game_start`, clearing
cached preservation addresses before the next `recording_frame_start` event.
It records each pinned `CMD_INITIAL_RNG` frame-start payload as
`recording_frame_start` with `frame`, `rng`, and `scene_frame`; cached ODB/RXB
snapshots are attached only after those addresses are known. The pinned ASM
sizes are ODB 3311,
RXB 297, and SSCB 158.

Frame-start payloads are authored by the source recording callback and can be
batched before the recording buffer is flushed. Their `frame`, `rng` and
`scene_frame` describe that callback. Attached rollback fields describe the
later DMA observation; they cannot establish callback-time rollback flags.
The B2 load and completion events surround the actual native savestate load.
The current no-pause profile maps B2 online frames to the recording scene clock.
A pause-enabled profile must account for the pinned recording pause adjustment.

## Run the bounded fixture

Prepare the pinned client and local service with [the testbed guide](LOCAL_TESTBED.md).
Use the workspace/storage policy before building or recording. Set `MELEE_DISC`
to the owned image, `MELEE_RUN_ROOT` to an absent private output directory on the
verified external volume, and `MELEE_SHORT_PROFILE_ROOT` to an existing short
external directory for the client's Unix sockets.

```sh
python3 scripts/agent_workspace.py run -- python3 reference-capture/slippi/run_local.py \
  --disc "$MELEE_DISC" --run-root "$MELEE_RUN_ROOT" \
  --profile-temp-root "$MELEE_SHORT_PROFILE_ROOT"
```

The default is two fresh private profile pairs, each playing a match and rematch.
The last cycle also checks peer disconnect. With no diagnostic environment
variable, this is the ordinary diagnostic-disabled lifecycle and retains the
normal controller script and transport.
The ordinary runner checks its two fresh profile lifecycles, menu traversal,
natural ending, rematch, disconnect/interruption cleanup, process ownership and
socket gates. It does not make the diagnostic runner's finalized replay or
receiver-attributed transport claims. A mismatch stops before the next long
game, retaining its first field and frame in the private lifecycle receipt.

For an impaired case, first create a separate passed diagnostic baseline with
the same source-profile client and service. This `none` invocation is only the
comparison fixture consumed by `run_rollback.py`; it is not the ordinary
diagnostic-disabled lifecycle above. Then choose another absent
`MELEE_RUN_ROOT` and point `MELEE_BASELINE_ROOT` at that diagnostic baseline:

```sh
python3 scripts/agent_workspace.py run -- python3 reference-capture/slippi/run_rollback.py \
  --disc "$MELEE_DISC" --run-root "$MELEE_BASELINE_ROOT" \
  --profile-temp-root "$MELEE_SHORT_PROFILE_ROOT" --scenario none \
  --service-lineage "$MELEE_SERVICE_LINEAGE"
```

```sh
python3 scripts/agent_workspace.py run -- python3 reference-capture/slippi/run_rollback.py \
  --disc "$MELEE_DISC" --run-root "$MELEE_RUN_ROOT" \
  --profile-temp-root "$MELEE_SHORT_PROFILE_ROOT" \
  --scenario hold --baseline-root "$MELEE_BASELINE_ROOT" \
  --service-lineage "$MELEE_SERVICE_LINEAGE"
```

`none`, `duplicate`, `jitter`, `reorder`, `hold`, and `drop` are the accepted
`--scenario` values. `none` is the required two-cycle baseline; every impaired
run requires that passed baseline root and the same service-lineage receipt.
The service-lineage receipt is produced only after the public PR #133 ref
`codex/local-api-framing-pr` at commit
`cdc4b899ee85861cce7f2cebfc9d756a30326ab6` has been path-applied and built in
this checkout as described in `LOCAL_TESTBED.md`. The runner verifies the public
commit/parent, path-diff and source-file identities, configured service build
output, cache and source inventory fields; it refuses a receipt
or binary copied from an unrelated service tree. The receipt's build and
service hashes are generated for the current applied composition and are
bound identically into the passed baseline and each impaired run.
The pinned
input delay is two frames; this interval covers the A-button transition generated
at source frame 96. The fixture requires the actual held packet identities,
history, queue counts and release order to match that schedule. `drop` removes
only packet 98, preserving the normal input history; later packets can repair
this loss without a prediction-error rollback. Both peers must agree exactly
with the corresponding baseline game's parsed declared finalized state and
input fields. The raw replay records do not expose analog A/B pressure.

The run report keeps `rollback_correctness_claimed` false until native
prediction-error load and source resimulation observations have been checked.
Each observed load must also have a paired speculative remote input and post-state
in the latest pre-load recording occurrence that differ from the finalized pair.
The corrected pair must occur in that load's own post-completion recording
interval, before the next load. The complete native and raw replay FrameStart
vectors must agree on frame, RNG, scene and occurrence order, with exactly one
remote PRE/POST pair per occurrence. Repeated loads cannot reuse a corrected
occurrence. The strict decoder validates revision pairing and finalization before
this read-only revision check. Every loaded scene must have contiguous repeated
recording payload coverage and an observed rewind. This is an explicit
coverage gate for the fixture; the recording buffer does not guarantee every
native simulation iteration will be recorded. Matching finalized replays alone
cannot satisfy these gates. Actual browser simulation, cross-machine
connectivity, pixels, PCM, foreground timing and physical input have separate
acceptance gates. The [scoped E06 baseline and transport matrix](../../docs/evidence/desktop-transport-matrix-e06-v1.json)
records the six desktop scenarios and their receiver-attributed transport rows;
the [original diagnostic receipt](../../docs/evidence/desktop-rollback-diagnostic-v1.json)
records two fresh baseline pairs and two fresh held-packet pairs, each playing
a match and rematch. Two fresh single-packet DROP pairs also pass the same
finalized comparison through complete matches and rematches, with exact packet
98 loss and redundant history retained. No native loads or speculative corrections
were observed in that case; it proves tolerance of this loss, without a rollback
claim. The ordinary diagnostic-disabled match/rematch, disconnect and interruption
regression also passes. These native desktop results do not establish
browser gameplay or the rest of the fault matrix.
