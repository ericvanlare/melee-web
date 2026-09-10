# CSS, SSS and match-entry retail comparison

This gate compares the supported source lifecycle through CSS, SSS cancellation,
a second CSS-to-SSS transition, and match entry. It is semantic evidence for this
boundary only. Gameplay state, rendered frames, emitted PCM, physical input and
browser performance retain their separate acceptance gates.

The JSONL schema is `melee-web-transition-trace` version 1. A run must contain:

1. `capture_begin` in a live CSS with `menu01.hps` active.
2. CSS exit and SSS entry.
3. SSS exit back to CSS.
4. CSS entry, CSS exit and a second SSS entry.
5. SSS exit toward a match.
6. Completed match entry.

The comparator requires the exact event order and routes. Every menu event must
retain active `menu01.hps` under owner epoch zero. Match entry must establish one
new active owner and the retail capture must observe no stream start, stream stop,
AX driver initialization or language-bank initialization during the preceding
CSS/SSS transitions. The committed rules, all four `PlayerInitData` records and
source RNG are compared at SSS exit and completed match entry. Addresses and
callback pointers are deliberately absent.

## Port capture

Build and run the existing native menu host trace with an output path. This uses
only local, owned assets and writes ignored evidence under `work/`:

```sh
.venv/bin/cmake --build build/browser-release --target native_menu_host_trace -j4
.deps/emsdk/node/24.19.0_64bit/bin/node \
  build/browser-release/native_menu_host_trace.js \
  assets-local/native-menus assets-local/next-gate 32 \
  work/port-transition-fd.jsonl "$(git rev-parse HEAD)"
```

Run zero carries Mario/Mario and run one carries Falco/Mario. The trace itself is
the authority for exact costume indices, rules and inactive slot values that the
retail setup must reproduce. Stage kinds currently admitted by the host trace are
Final Destination `32`, Battlefield `31`, and Yoshi's Story `8`.

## Read-only retail capture

Use the pinned isolated Dolphin setup in
[`ORIGINAL_COMPARISON.md`](ORIGINAL_COMPARISON.md). Configure the output before
sourcing the collector if the default `work/reference-transition.jsonl` is not
desired:

```sh
set environment MELEE_TRANSITION_TRACE /absolute/ignored/output.jsonl
source tools/reference_transition_capture.py
```

Reach CSS through normal retail input, reproduce the selected run's rules and
players, and confirm that `menu01.hps` is already active. Then start collection:

```text
ref-transition begin 0
```

Use ordinary controller input to enter SSS, press B to return to CSS, enter SSS
again, and select the target stage. The collector advances its one lifecycle
breakpoint from each function entry to its return address. It also observes the
retail stream start/stop and audio initialization calls. These counters are
installed by `ref-transition begin`, so normal boot and oracle setup do not pay
their interpreter cost. All breakpoints are hardware execution breakpoints and
the collector performs bounded reads only. Collection stops automatically after
`gm_Scene_Vs_OnEnter` returns; interrupt GDB and run `ref-transition off` to
remove the dormant counters before continuing the match. A second isolated setup
can append run one with `ref-transition begin 1`.

The script is pinned to the recorded GALE01 revision-2 addresses and DOL SHA-1.
The retail header also pins the Dolphin version and commit, CPU core and thread
mode, and fixed RTC. The port header pins the full Git revision and
browser-release configuration. The comparator rejects missing or mismatched
provenance. Do not use retail output after changing the disc or emulator setup.

## Comparison

```sh
python3 tools/compare_transition_trace.py \
  work/reference-transition-fd.jsonl work/port-transition-fd.jsonl \
  --run 0 --output work/transition-fd-comparison.json
```

Malformed, incomplete or reordered captures fail before comparison. A semantic
mismatch reports the first event and field. Passing this gate establishes only
the declared lifecycle scope; it does not promote match simulation, rendering,
PCM output, latency or performance evidence.

## Current pinned baseline

The ignored local `work/reference-transition-stock-fd.jsonl` capture uses four
stocks, P1 yellow Mario, P2 red Mario and Final Destination. It completes all
nine events. Lifecycle order and audio continuity pass: retail records no menu
stream stop/start or AX reinitialization and changes once from `menu01.hps` to
`sp_end.hps` at match entry. The full semantic report remains red. Its first
divergence is `sss_exit_complete.selection.rules.match_kind`: retail still has
the pre-`gm_16AE` payload at that return boundary, while the port has already
normalized the stock payload. At completed match entry, item-mask, rumble flag,
inactive-slot stock and RNG fields also differ. RNG equality requires a saved
starting state and frame-counted input recipe; wall-clock-driven menu input is
valid for lifecycle/audio evidence but cannot establish exact RNG equivalence.
