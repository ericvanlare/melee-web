# Reference capture application

The reference capture application is a private macOS operator tool for one
controlled original-game observation. It launches the reviewed Dolphin build
as a separate process, records the passive observer stream, and retains the
raw run until its transport and semantic lifecycle have been checked. It does
not make a browser build, a synthetic scene, a short prefix, or one capture a
gold reference.

## Evidence boundary

The current capture contract is a single ordinary boot with one human player
and one CPU player. The observer requires a human P1 versus CPU P2 setup and
records PAD consumption, source frames, draw audits, result publication, and
scene teardown. Physical-controller readiness is checked before launch, but
physical input behavior and end-to-end latency are still separate evidence
items.

Use this controlled operator plan for the capture pass:

1. Boot through the ordinary in-game CSS and SSS.
2. Select Mario for P1 and Fox for P2 at CPU level 1, with P1 human and P2
   CPU.
3. Select Final Destination, two stocks, a one-minute time limit, and items off.
4. Play through the result and let the original scene teardown finish before
   stopping the application.

The capture code does not turn those menu choices into an independent
acceptance claim. Keep the run notes with the private capture workspace and
report only what the frozen bundle and later comparison actually establish.

## Build and provision the private environment

The commands below use portable paths. Keep the disc image, extracted
`main.dol`, prepared fixture, Dolphin build receipt, and capture output under
`$HOME`; none belongs in Git or in the application bundle.

The dependency bootstrap validates the Dolphin source pin but does not clone
entries whose lock kind is `source`. Prepare that checkout once, or point the
builder at an equivalent clean checkout, before building. This example starts
from the pinned upstream URL and leaves the checkout detached at the reviewed
commit:

```sh
mkdir -p "$PWD/.deps"
git clone https://github.com/dolphin-emu/dolphin.git "$PWD/.deps/reference-dolphin"
git -C "$PWD/.deps/reference-dolphin" checkout --detach \
  c77bbaa0f372c3f72281602a8b087206706542cb
test -z "$(git -C "$PWD/.deps/reference-dolphin" status --porcelain)"
```

If the checkout already exists, fetch the commit through the normal Git
workflow and repeat the detached checkout and clean-status check. Do not put
local edits in this source tree. The builder refuses a dirty or differently
pinned checkout and never modifies the pinned source tree:

```sh
python3 scripts/build_reference_dolphin.py \
  --source-dir "$PWD/.deps/reference-dolphin" \
  --work-dir "$PWD/work/reference-dolphin-source" \
  --build-dir "$PWD/work/reference-dolphin-build" \
  --manifest "$PWD/work/reference-dolphin-build/reference-dolphin-build.json"
```

Provision settings from already-owned inputs. The fixture directory must be
the independently prepared private unlock fixture; the script checks its
expected GCI and SRAM hashes and refuses another card:

```sh
CAPTURE_ROOT="$HOME/Library/Application Support/WebMelee Reference Capture"

python3 scripts/configure_reference_capture.py \
  --disc "$HOME/Private/Melee/GALE01r2.iso" \
  --dol "$HOME/Private/Melee/main.dol" \
  --build-manifest "$PWD/work/reference-dolphin-build/reference-dolphin-build.json" \
  --fixture-gc "$HOME/Private/Melee/prepared-fixture" \
  --root "$CAPTURE_ROOT" \
  --install-dolphin
```

The settings file is private and mode-restricted. Verification binds the
selected disc and embedded DOL, Dolphin binary, isolated profile, fixture,
observer identity, and timing policy. It also checks the currently connected
controller without storing device serials. `--install-dolphin` publishes the
verified app under a binary-SHA-256 version directory in the private support
root and binds its receipt, runtime inventory, bundle inventory, and
corresponding-source archive before the settings point at it.

Installed versions are stable: an existing binary-hash directory is validated
and never overwritten. `--refresh-build` archives the prior environment and
receipt under `BuildHistory/<old-binary-sha256>` while preserving the private
disc, fixture, profile, and controller settings, so a reviewed older receipt
can be selected for an explicit rollback. The copied source archive is
inventory-hashed byte-for-byte; source drift or a receipt collision aborts the
operation.

Install the app with the exact Python runtime that will execute its bundled
supervisor:

```sh
python3 scripts/install_reference_capture.py \
  --python "$(command -v python3)" \
  --destination "$HOME/Applications/WebMelee Reference Capture.app"
```

Use `--skip-alias` when a Desktop Finder alias is not wanted. The installer
copies only reviewed source runtime files. It excludes discs, Dolphin state,
memory cards, captures, and other private payloads.

## Operate a capture

Version 0.2.0 retains the pinned SDL discovery helper for physical SDL devices
and adds Dolphin input recording and **Replay Capture…**.
Dolphin's controller database may assign a name different from macOS IOHID's
product name. The helper derives the same SDL name and per-name index as Dolphin,
then the supervisor binds that selection to observed physical vendor/product IDs.
There are no device-name aliases or an "any connected controller" fallback.
Discovery happens before launch; connection monitoring during play only reads
the OS device inventory and does not open another SDL client.

Build and install the helper after the pinned Dolphin build and before opening
the capture app. See [the controller probe](../reference-capture/controller-probe/README.md)
for the exact SDL source pin, build inputs, and command options. Installation
retains the operator's existing controller mapping and saves prior settings.
The helper binary, source receipt, and dynamic dependencies are verified before
readiness; a mismatch keeps capture disabled.

```sh
python3 scripts/build_reference_controller_probe.py \
  --source "$PWD/work/reference-dolphin-source" \
  --build "$PWD/work/reference-dolphin-build" \
  --output-root "$PWD/work/reference-controller-probe"
python3 scripts/install_reference_controller_probe.py \
  --manifest "$PWD/work/reference-controller-probe/build-manifest.json" \
  --root "$CAPTURE_ROOT"
```

Quit the capture app before installing a helper or application update. The
installer preserves the configured Dolphin mapping and recovers its exact SDL
index when upgrading an older name-only capture setting.

Launch the installed app:

```sh
open "$HOME/Applications/WebMelee Reference Capture.app"
```

The app verifies its executable, Python manifest, and bundled runtime before
starting the supervisor. Use **Configure Controller…** to configure port 1 in
the isolated Dolphin profile. Select a physical SDL gamepad or Nintendo
adapter, then use **Rescan** until the app reports an accepted disc and a
connected physical controller. The **Start Capture** button is enabled only
when both gates are ready.

macOS may ask this app to access Documents when the configured prepared save
is stored there. Allow that folder prompt to let verification read the existing
fixture. The app explains the wait in its verification details. A denied prompt
leaves capture unavailable; no memory-card copy or alternate profile is used.
No Accessibility or Screen Recording permission is required by the capture app.

Start Capture launches the ordinary Dolphin boot with an isolated `-u` user
directory, the selected disc, cheats disabled, fixed emulation speed, the
reviewed JITARM64 observer, and a fixed RTC. The observer is dormant unless
all required `MWRC_*` identity variables match. Do not use a debugger, save
state, or custom input path for this run.

The prepared unlock fixture is a private setup aid. The capture uses a
read-only reference to its SRAM and runs with `Core.SaveDataWritable=False`;
guest-side changes disappear with the emulation process. The fixture is not
copied into the app, retained in the raw bundle, or used as comparison data.

If the operator stops the run, Dolphin exits unexpectedly, the observer reports
an error, or semantic teardown is missing, the app leaves an inspectable
`.partial` bundle. Do not rename it by hand or treat it as a completed retail
run. Start a new run after correcting the cause.

## Replay a recording in Dolphin

New recordings contain `inputs.mwri`, an ordered stream of original Dolphin
controller reads from boot through the declared match teardown, plus a private
snapshot of the exact isolated configuration. Select **Replay Capture…** and
choose the original finalized recording in `acceptedunprocessed` or `ingested`.
Replay starts a new isolated Dolphin process and a new observed capture. It
does not require a connected physical controller. **Stop Capture** also stops
a replay and preserves its incomplete evidence.

The installed Dolphin/observer, disc, fixture, operating system, locale and
timing policy must match the original recording. The application rejects drift
before launch. It restores the recorded controller configuration in a fresh
session directory and supplies only the recorded host device samples. Original
controller calibration, guest PAD handling and CPU decisions still execute in
the game. No guest memory, fighter state, CPU output or captured address is
supplied as replay input.

After complete teardown, the application validates the new raw bundle and
compares both authoritative semantic streams. The report binds both manifests
and records the first differing event and field, with separate coverage counts
for PAD, setup, fighter/CPU state, RNG, camera, HUD, subject, magnifier, draw,
result, scene and teardown observations. Missing domains are explicit. It is
an original-versus-original diagnostic comparison, not gold admission or a
claim of web-port equivalence. Host timestamps and independent session identity
are provenance; source observations and addresses remain comparison targets.

The replay bundle and comparison report appear in the same inbox as ordinary
captures. The raw original is never overwritten; comparison reports live under
`Captures/derived/`. To replay again, select the original recording, rather
than the resulting observation-only replay bundle.

Recordings made before version 0.2.0 do not contain the emulator input stream.
Their human-input recipes and web/native comparison reports remain usable, but
faithful Dolphin boot/menu replay requires a new recording. The app explains
this limitation instead of guessing missing controller reads.

The private CLI exposes the same operation for unattended diagnostic runs:

```sh
python3 scripts/reference_capture_app.py --replay-bundle "$ORIGINAL_BUNDLE"
python3 scripts/compare_reference_sessions.py --help
```

The [input-stream contract](../reference-capture/dolphin/INPUT_STREAM.md)
documents the thin SI patch, format, failure conditions and reproducible source
boundary. Physical replay and native GameCube-adapter connection transitions
require their own operator validation; a Pipe-driven run does not establish
those cases.

## Bundle states and validation

Capture output is under:

```sh
INBOX="$CAPTURE_ROOT/Captures"
```

The inbox has exactly these states:

| State | Meaning |
| --- | --- |
| `activepartials` | A live or failed `.partial` run whose raw bytes remain inspectable. |
| `acceptedunprocessed` | A complete raw bundle that passed transport, semantic, and manifest checks. |
| `failed` | A quarantined partial kept for diagnosis. |
| `ingested` | The same accepted raw bundle after explicit ingestion. |
| `derived` | Hash-bound diagnostic artifacts stored separately from raw evidence. |

Inspect the inbox and validate an accepted run:

```sh
python3 scripts/reference_capture_inbox.py --root "$INBOX" --pretty list
python3 scripts/reference_capture_inbox.py --root "$INBOX" --pretty \
  validate SESSION_ID --state acceptedunprocessed
```

Validation requires contiguous record sequence numbers, a typed event and
evidence payload for each lifecycle phase, an entry, source gameplay,
ending/result, teardown, and clean observer end in order, no observer or
writer faults, and a complete semantic report. The observer parser also
rejects bad framing, truncation, checksum failures, malformed slices, and
sequence gaps. Pre-match PAD polls remain in the stream but do not count as
gameplay.

The finalized `manifest.json` inventories every regular payload file except
itself, including `observer.bin`, `records.jsonl`, status, environment,
observer status, logs, and `validation.json` when present. It records bytes and
SHA-256 for each file and binds the complete inventory with its own hash. A
symlink, special file, extra file, missing file, or changed byte makes
validation fail.

Move a validated raw bundle to `ingested` only with its manifest digest:

```sh
MANIFEST_SHA256="$(python3 -c \
  'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["manifest_sha256"])' \
  "$INBOX/acceptedunprocessed/SESSION_ID/manifest.json")"

python3 scripts/reference_capture_inbox.py --root "$INBOX" --pretty ingest \
  SESSION_ID --manifest-sha256 "$MANIFEST_SHA256"
```

Quarantine an unfinished run without deleting it:

```sh
python3 scripts/reference_capture_inbox.py --root "$INBOX" --pretty quarantine \
  SESSION_ID --state activepartials
```

The raw bundle is never rewritten by ingestion or derivation. A failed
validation preserves the partial and writes `failure.json` with the run-local
reason.

## Diagnostic derivation and claims

After validation, the replay tool extracts source-consumed PAD queue samples
and writes diagnostic artifacts under the separate `derived` state:

```sh
python3 scripts/ingest_reference_capture.py \
  --bundle "$INBOX/acceptedunprocessed/SESSION_ID" \
  --derived-root "$INBOX/derived"
```

This command ingests the accepted raw bundle first when necessary. The derived
manifest binds every output to the raw manifest and observer digest. CPU rows
are retained as a sidecar for inspection; CPU-generated decisions are excluded
from replay input. A derived run is explicitly a single-capture diagnostic:
repeatability, port equivalence, performance acceptance, and gold admission
remain unclaimed until their separate evidence gates pass. Do not call one
capture a gold pair.

## Compare one capture with a port or browser trace

Use the maintained comparison command after a derived directory has been
written. It rechecks the derived manifest and every artifact hash, validates
`candidate.jsonl` with the strict retail replay validator, then compares one
port-format trace. The trace can be a native diagnostic trace or a browser
state trace; the label is retained in the report:

```sh
python3 scripts/compare_reference_capture.py \
  --derived "$INBOX/derived/SESSION_ID" \
  --trace "$HOME/Private/Melee/browser-state.jsonl" \
  --trace-kind browser \
  --output "$INBOX/comparisons/SESSION_ID.json"
```

Pass `--bundle` to recheck that the raw finalized bundle's manifest hash is
the source bound by the derived directory. When both sides have strict CPU
observation JSONL sidecars, add `--reference-cpu` and `--trace-cpu`; the report
retains the earliest state difference and the first divergence per observation
domain. A matching report covers the fields and source ticks actually
compared. If a requested sidecar is malformed, the report marks
`cpu_observation` as missing coverage and preserves any replay difference under
the `invalid_input` status. It does not establish repeatability, full port
equivalence, performance acceptance, pixels, or gold admission.

If a port trace ends before successful teardown, the command validates its
observed prefix and retains the first difference under `incomplete_prefix`.
Exit status remains 2, even if every observed field matches. Missing port
completion and unavailable CPU-prefix comparison are explicit coverage gaps.
Malformed prefixes and attempted completion records still fail validation.
This route never fabricates a second original reference or shortens the
requested workload to make an interrupted replay pass.

## First physical-controller session

The operator completed a four-stock Falco versus level-5 Marth match on Yoshi's
Story with no timer. The version 0.1.1 app accepted 35,519 contiguous observer
events, 8,348 PAD polls, 6,492 source ticks and 6,487 draws through Falco's win
and fighter teardown. The physical-session evidence is the operator's report,
the connected device identity and the accepted stream. The raw preflight
`physical_session_validated: false` and parser
`not_performed_by_automation` fields remain immutable; the operator attestation
lives in the separate derived report. Input latency and performance are not
accepted: the operator reported slow, choppy play.

The human-only MWRC recipe completes all 6,492 ticks in a visible Release
browser, with the same winner. Supplied inputs, PAD state, RNG, match frame,
HUD, magnifier and match observations agree throughout. The first core fighter
difference is Marth's input at zero-based index 5,057; the CPU command/stick
output first differs at the same index. CPU-generated decisions were never
supplied as input. The cause has not been established, and no gameplay change
was made. Camera FOV differs at tick 0, subject bone rounding at tick 73, and
the browser has five additional draws at indices 1,631, 2,632, 3,633, 4,634 and
5,635. Pixel and audio comparison remain excluded.

The headless native trace stops after 5,704 ticks with an unsupported pause/exit
and no successful teardown. Its first RNG/damage difference is index 1,482:
retail has Marth's magnifier offscreen flag and one additional point of damage;
the headless path lacks them. The visible browser reproduces this original
offscreen damage, consistent with the already documented source-drawing
dependency. The native run remains an incomplete diagnostic. Its CPU sidecar
is preserved but is not admitted by the complete-only CPU validator.

The inbox associates the immutable raw manifest with the input recipe and a
separate aggregate native/browser comparison. Full traces, screenshots and
failed attempts remain private. The [portable receipt](evidence/reference-operator-session-v1.json)
contains artifact hashes, comparison boundaries and test results without raw
capture payloads. The full repository suite passes 867 tests with 37 optional
skips; all nine single-reference comparison tests pass.

### Dolphin rerun boundary

This recording is useful for port comparison without another Dolphin pass:
it already contains typed retail state and CPU-output observations. It is
not yet a demonstrated repeatable Dolphin boot/menu recording. Its passive
PAD observations were not saved as a Dolphin input movie, and the session has
no corresponding savestate checkpoint. Replaying the source-consumed match
inputs alone does not prove reconstruction of the earlier scene or heap context.

The existing `capture_retail_replay.py` route can instead run human inputs
from a synthetic SSS checkpoint with setup and PAD context injection. It
requires the matching checkpoint/GC state, template profile and provenance;
even a passing independent pair would establish only that narrower synthetic
match-boundary repeatability. It cannot establish this physical cold boot's
allocation context. That rerun was not performed for this session.

Reliable original-session reruns need a replayable emulator input stream plus
an independently reproducible boot context, or a matching checkpoint with an
explicitly narrower scope. Validate that route against the frozen original
typed records before relying on it to add offline observations. Additional
observers still need declared coverage; a rerun does not automatically capture
every field needed for a future accuracy investigation.

## Observed first-session validation

The [portable evidence receipt](evidence/reference-capture-app-v1.json) binds the
automated cold-boot session and both port comparisons. Normal controller inputs
navigated the original menus, selected Mario versus level-1 Fox on Final
Destination, and played the existing human input plan. Host scheduling did not
prescribe source inputs; the observed PAD queue supplied the recipe. The match
ran 3,838 source ticks through its one-minute ending, published Fox as the
winner, and released both fighters. Finalization accepted 60,908 contiguous
observer events, including 24,972 PAD polls and 3,836 paired draws.

The native and visible-browser replays complete all 3,838 ticks. CPU decisions,
HUD, match clock and result observations match. Both first differ in the core
fighter fields at zero-based source index 1,976: Mario's vertical-position bits
are `c081c072` in retail and `c081c074` in the port. Camera differs earlier
(native index 1, browser index 0); camera-subject bone rounding first differs
at index 82. The browser performs two additional draws, at source indices
2,373 and 3,374. Native drawing is excluded. These are preserved diagnostic
failures, not gameplay changes or reference repeatability claims.

The prior 1,199-tick two-player gold workload also retains its declared core
and CPU agreement in fresh native and visible-browser runs. All 753 gold files
retain their hashes. PR #16 remains draft at its original head; its 14 existing
modified source files and 1,113 allocation evidence files remain unchanged.
Failed observer boots and superseded derivations remain private and inspectable.

Fresh installation, repeated replacement, Finder alias creation, relaunch,
ordinary/narrow window layouts and shutdown during a macOS permission wait
were exercised locally. This automated validation preceded the operator's
Documents-folder permission grant and the physical session described above;
it makes no physical-controller or latency claim by itself.

The complete local repository suite passed 851 tests with 37 documented
optional-target/fixture skips. The subsequently added controller-setup drift
guard passed with the 16-test supervisor suite. Release browser and native
trace targets, checked browser and trace targets, and both macOS application
architectures built successfully. Synthetic tests cover sequence gaps, CRC and
framing faults, overflow, cancellation, crashes, configuration/binary drift,
immutable finalization, nested inbox discovery and derived input contamination.

## Source and license provenance

The observer is a downstream diagnostic overlay for Dolphin Emulator commit
`c77bbaa0f372c3f72281602a8b087206706542cb`, under GPL-2.0-or-later. The
tracked patch and overlay live under `reference-capture/dolphin/`; the
corresponding-source notice is in
[`reference-capture/dolphin/LICENSES.md`](../reference-capture/dolphin/LICENSES.md)
and the license text is in
[`docs/licenses/dolphin-gpl-2.0-or-later.txt`](licenses/dolphin-gpl-2.0-or-later.txt).
The build receipt records patch and overlay hashes, the composed source diff,
compiler/CMake details, and the resulting binary hash. Keep that receipt with
the private build; its `source` and `build` fields identify the corresponding
source worktree and build output, and contain local paths that are not tracked.
The selected `GALE01r2` executable is pinned by SHA-1
`08e0bf20134dfcb260699671004527b2d6bb1a45` and SHA-256
`dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646`.

The app bundles a Dolphin notice but no Dolphin binary, game disc, memory card,
save state, or extracted game data. The ordinary Dolphin source checkout stays
untouched while the build helper uses an ignored worktree.

No completed original-game capture, physical-controller validation, or gold
comparison is implied by installing this tool. Those results remain evidence
pending until a real run and its independent checks are recorded.
