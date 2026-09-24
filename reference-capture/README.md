# Reference capture implementation

This directory contains the reviewed native observer overlay, its fixed binary
stream reader, the desktop app source, and the on-disk bundle schemas. The
capture path is an evidence collector for a private original-game run. It is
not a game emulator, a browser validation shortcut, or a gold-corpus
admission tool.

## Source provenance and build boundary

The passive observer is built against Dolphin Emulator commit
`c77bbaa0f372c3f72281602a8b087206706542cb` with the tracked patch
`dolphin/patches/0001-jitarm64-reference-observer.patch` and the tracked
`dolphin/source/` overlay. Dolphin is GPL-2.0-or-later. Keep the corresponding
source and notices in `dolphin/LICENSES.md` and
`../docs/licenses/dolphin-gpl-2.0-or-later.txt` with any supplied binary.

The observer patch tests read an optional pinned checkout at
`.deps/reference-dolphin`. Install it without a full clone, then
`tests/test_reference_dolphin_observer.py` verifies that the tracked patch
series applies to the clean pinned tree instead of skipping:

```sh
git clone --filter=blob:none --no-checkout --depth 1 \
  https://github.com/dolphin-emu/dolphin.git .deps/reference-dolphin
git -C .deps/reference-dolphin fetch origin c77bbaa0f372c3f72281602a8b087206706542cb
git -C .deps/reference-dolphin sparse-checkout set Source/Core/Core
git -C .deps/reference-dolphin checkout c77bbaa0f372c3f72281602a8b087206706542cb
```

`scripts/build_reference_dolphin.py` verifies that the input checkout is clean
and pinned, clones an ignored worktree, applies the patch with Git, overlays
the observer sources, and builds there. Its JSON receipt records:

- the Dolphin commit and build commands;
- patch, overlay, composed-diff, compiler, and CMake identities;
- the expected `GALE01r2` DOL SHA-1/SHA-256 and JITARM64 mode; and
- the resulting binary hash.

The receipt contains local source and build paths and belongs in the private
support root, never in a tracked fixture or app bundle. The installer copies
only reviewed Python sources and the Swift front end. It rejects symlinks,
discs, save files, memory cards, captures, and private directories from a
runtime payload.

The receipt's `source` and `build` fields are the discoverable locations for
the corresponding-source worktree and build output on the operator's machine;
they are intentionally not reproduced in tracked documentation. The selected
`GALE01r2` executable identity is SHA-1
`08e0bf20134dfcb260699671004527b2d6bb1a45` and SHA-256
`dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646`.

## Observer transport

The C++ observer is dormant unless all of these identity variables are present:

```text
MWRC_ENABLE=1
MWRC_OUTPUT=<new observer stream path>
MWRC_DOL_SHA256=dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646
MWRC_CPU=JITARM64
MWRC_SOURCE_REV=GALE01r2
```

`MWRC_STATUS` optionally names the status JSON. The stream reader in
`dolphin/reference_observer_stream.py` reads little-endian, length-delimited
MWRO records and fails closed on:

- bad magic, unsupported version, unknown event, invalid length, truncation, or
  CRC failure;
- reordered or gapped transport sequence numbers;
- invalid boundary metadata, GPR count, slice tag, overlap, unexplained gap,
  trailing bytes, or payload bounds; and
- an invalid observer status sidecar.

Raw transport events are `handshake`, `start`, `boundary`, `progress`,
`error`, and `end`. Boundary payloads contain the observed PC/LR, 32 GPRs,
source and draw ordinals, and bounded typed memory slices. The observer flushes
guest register state before its callback, never writes guest memory, and uses a
separate writer thread with a bounded ring. Overflow, serialization failure,
stream failure, or status failure latches invalidity instead of dropping a
record.

The observer writes to a fresh output path. The application retains that raw
`observer.bin` beside the semantic JSONL records. The raw bytes are never
rewritten during validation, ingestion, or derivation.

### Experimental whole-session stream

The single-match stream remains the default. Setting
`MWRC_WHOLE_SESSION_MATCHES` to a decimal value from `3` through `64` opts
into the experimental repeated-match producer. With the variable unset, the
handshake/start JSON, transport schema version, boundary IDs, and payload
bytes retain the v1 behavior.

The opt-in handshake and start records declare `whole_session: true` and the
same `match_count`. Whole-session boundary payloads retain transport schema
version 1 and set boundary flag `1`; their final eight bytes are the little
endian tuple `<u16 match_index, u16 boundary_kind, u32 reserved>`, with the
reserved word zero and the kind matching the boundary prefix. The stream
reader rejects an unannounced flag, unknown flags, a mismatched kind, gaps,
overlaps, or malformed metadata.

The pinned observer checks the original DOL prologue at every added source
hook. The experimental lifecycle emits the following ordered boundaries for
each match: CSS/SSS entry and exit, VS entry/setup, source draw returns, VS
exit and return, VS mode exit, Results enter, one or more Results GObj process
callbacks, Results exit, Results mode exit, scene teardown, and return CSS.
The first match includes `css_enter`; a return-CSS boundary after a
non-final teardown advances to the next match, whose next record is
`css_exit`. The final return-CSS boundary is required before natural stream
completion. Results process records carry the original PAD/input snapshot,
RNG pointer/value, and Result payload slices. No guest memory is written.

CSS and SSS enter hooks and the CSS exit hook run at function entry. Enter
records read their source argument because the scene's static pointer has not
yet been assigned; CSS exit reads the live static pointer. SSS exit runs at
its verified return instruction, after the original callback writes its route.
VS exit entry retains a diagnostic pre-callback Result; VS exit return carries
the completed Result. Entry-hook menu audio is likewise pre-callback state.
These scopes remain distinct from the completed-callback events used by the
existing transition comparator. The required join must
retain that distinction, including the final return-CSS entry.

`validate_whole_session_observer_records` in
`tools/reference_capture_semantics.py` audits these decoded observer records,
rejecting missing, duplicated, out-of-order, or wrong-PC boundaries. It
returns `complete: false`, `experimental: true`, and
`accepted_for_reference_bundle: false` until one capture also supplies the
existing CSS/SSS transition-trace join, menu audio owner epoch, and capture
identity. A declared match count or a syntactically complete raw stream is
not a retail capture result and cannot promote a bundle.

## Semantic adapter and lifecycle

`tools/reference_capture_semantics.py` consumes every parsed transport envelope
using the stable typed shape:

```json
{
  "seq": 0,
  "event": "source_tick",
  "source_tick": 12,
  "draw_ordinal": 12,
  "payload": {
    "pc": 2151244468,
    "lr": 0,
    "gprs": [],
    "slices": []
  }
}
```

The example is abbreviated for readability; a raw boundary payload has exactly
32 GPR values and complete bounded slice descriptors. The semantic adapter
does not accept missing fields when a boundary requires them.

The decoder resolves the pinned boundary PCs and produces semantic records for
PAD polling/consumption, fighter creation, match entry and initial state,
source ticks, draw entry/return, result entry/return, and scene reset. It keeps
pre-match transport records in order, but only source ticks in the active match
count as gameplay. A complete semantic report requires match construction,
source ticks with one consumed PAD sample each, draw audits, a published
result, and teardown with no live fighter/entity ownership left.

The bundle validator uses the semantic completion report as a second gate. Its
generic lifecycle vocabulary is deliberately small: `entry`, `gameplay`,
`ending_result`, `teardown`, and `observer_end`. Empty lifecycle payloads,
noncontiguous `seq`, identity mismatches, explicit observer end failures, and
any missing-poll, drift, overflow, writer, crash, drop, or transport fault
keep the run partial.

The application supervisor owns the complete sequence:

1. Verify the private environment and physical controller readiness.
2. Create `activepartials/<session>.partial` with a unique session/run identity.
3. Launch ordinary Dolphin with an isolated user directory and the selected
   disc; the observer writes `observer.bin` and its status sidecar.
4. Parse every raw record, consume it through `SemanticSession`, and append the
   ordered semantic envelope to `records.jsonl`.
5. Require the observer to end naturally and the semantic report to be
   complete. Otherwise write failure evidence and preserve the partial.
6. Freeze `validation.json`, inventory every regular payload file, hash the
   inventory, and atomically rename the partial to
   `acceptedunprocessed/<session>`.

There is no success path that fabricates an observer-end row, source frame,
result, or teardown. The app may retain an incomplete `semantic-validation.json`
for diagnosis, but it cannot promote that run.

## Bundle and derived schemas

The schemas are intentionally separate:

| File | Contract |
| --- | --- |
| `schemas/reference-session-bundle-v1.schema.json` | Logical session identity, sequence start, and typed record envelope. |
| `schemas/reference-validation-v1.schema.json` | Frozen semantic validation sidecar bound to the session and record count. |
| `schemas/reference-inbox-v1.schema.json` | Exact state names: `activepartials`, `acceptedunprocessed`, `failed`, `ingested`, `derived`. |
| `schemas/reference-derived-v1.schema.json` | A derived artifact and its source manifest digest. |

`tools/reference_session_bundle.py` adds the operational rules that JSON
Schema cannot express: exclusive creation, append ordering, no symlink or
special-file escapes, no destination overwrite, complete manifest inventory,
SHA-256 binding, and failure preservation. `manifest.json` excludes itself
from the file list and binds the complete list through `manifest_sha256`.

Derived data is written under its own directory and manifest. It must first
validate the source raw bundle, then records the source manifest hash and
observer raw hash. It never changes the raw bundle. The replay derivation
retains CPU observations for audit but uses only source-consumed human PAD
samples as replay input. Its claims explicitly say that one capture has no
established repeatability, port equivalence, performance acceptance, or gold
admission.

## Private data boundary

The application support root is normally:

```text
$HOME/Library/Application Support/WebMelee Reference Capture/
```

It contains private settings, the isolated Dolphin profile, session work
directories, raw captures, logs, and the five-state `Captures/` inbox. Disc
images, extracted `main.dol`, prepared fixture bytes, controller payloads, and
the Dolphin build receipt stay outside the repository. Tracked schemas and
synthetic tests contain no game bytes, observer payloads, personal paths, or
private fixture contents. Runtime diagnostics may contain local paths, so do
not copy a private bundle or receipt into Git.

The prepared unlock fixture is not a natural progression result. It is a
private, independently hashed setup aid. `Core.SaveDataWritable=False` and the
observer's card/SRAM shutdown keep backing writes disabled; guest mutations are
discarded when Dolphin exits. The fixture is not retained in comparison data.

## Developer checks

Run the focused capture tests while changing this boundary, then the repository
suite:

```sh
python3 -m unittest \
  tests.test_reference_capture_app \
  tests.test_reference_capture_environment \
  tests.test_reference_capture_install \
  tests.test_reference_capture_replay \
  tests.test_reference_capture_semantics \
  tests.test_reference_observer_stream \
  tests.test_reference_session_bundle -v

python3 -m unittest discover -s tests -v
```

These tests exercise synthetic transport and lifecycle cases. They do not
establish a real retail capture, physical-controller behavior, a gold pair, or
browser equivalence. Those claims require the operator flow and independent
evidence described in `docs/REFERENCE_CAPTURE_APP.md`.
