# Private Slippi local testbed

This is a desktop interoperability testbed. It does not change Melee Web's
browser architecture. The local match/rematch receipt and the optional browser
transport probe are separate scoped results. The probe proves one headless
browser can exchange actual frame-tagged PAD records with adapted desktop
clients through the loopback relay; it does not run Melee in the browser or
establish general browser cross-play. Rollback correctness, public Internet/NAT,
production Slippi services, ranking, rendering, audio and physical-controller
usability remain separate work.

The [scoped two-cycle receipt](../../docs/evidence/local-slippi-connectivity-v1.json)
records the accepted run and hashes the retained local integration files. The
underlying logs, replays and per-client receipts stay in the ignored run
directory named in that receipt.

The [browser-to-desktop transport receipt](../../docs/evidence/slippi-browser-desktop-transport-v1.json)
records a separate one-pair input probe. Its retained external run includes the
headless Chrome screenshot, WebSocket/relay events, both local PAD streams, and
the second client's byte-exact remote-PAD observations. The evidence report
contains hashes for the retained files without recording an operator path.

The [local reporter framing receipt](../../docs/evidence/local-slippi-reporter-framing-v1.json)
records six direct requests for each of the old and close-delimited local
service variants. Each request received the exact fail-closed 503 JSON body;
the new variant omits `Content-Length` while retaining `Connection: close`.
This is a compiled/source-identified HTTP boundary check. It does not claim
native gameplay, browser interoperability, official-service behavior, or a
deterministic cause for the separately retained reporter-thread failure.

## Source Slippi profile prefix diagnostic

The [bounded source-profile prefix receipt](../../docs/evidence/source-slippi-profile-prefix-v1.json) records a source-only check whose finalized raw PAD records are the sole step inputs. It pins the source probe, explicit RNG profile helper, runtime ABI and clean lifecycle boundary. The receipt does not claim a complete replay, pending-state snapshot, browser gameplay, draw/PCM agreement or rollback acceptance. Run the portable checker in `tools/slippi_profile_prefix_check.py` with a local `.slp`, runtime pair, assets and explicit source/profile identities; it fails at the first divergence and retains its failure directory.

Its `queued_transfer_and_default_pair` section records a later root-audited source-only Node pair: a snapshot admitted at quiescent scene 106, raw scene 107 observed pending, scene 108 clear, three restores through scenes 107--110, and a separate old-baseline/candidate scripted/raw byte comparison. The `recorded_match_1342` section separately records the source/native comparison through scene 1341, ending at the fourth P1 stock loss; it does not include Results, rematch, browser, rollback or foreground timing. The portable checker defaults to scenes 0--110 and accepts the explicitly observed full endpoint with `--scene-last 1341`; that full command remains a source/native reproduction gate and does not imply browser/player rollback.

Build `gameplay_snapshot_probe` with `python3 scripts/build.py --trace-target gameplay_snapshot_probe --configuration Release`. With locally owned assets and the pinned desktop replay, run the command below from the repository root. Replace the local input paths and runtime pair, and use their audited SHA-256 identities; `--out` must be absent. The measured identities and fresh full-suite/build/checker results are in the receipt's `required_validation` section. A different replay or build needs its own receipt.

```sh
python3 scripts/agent_workspace.py run -- python3 tools/slippi_profile_prefix_check.py \
  --replay /path/to/local.slp --runtime /path/to/gameplay_snapshot_probe.js \
  --assets /path/to/local-assets \
  --source-probe tests/gameplay_snapshot_probe.cpp --source-cmake cmake/FighterRuntime.cmake \
  --profile-helper reference-capture/slippi/profiles/slippi_rng_profile_gpl.cpp \
  --probe-sha256 AUDITED_PROBE_SHA256 --cmake-sha256 AUDITED_CMAKE_SHA256 \
  --profile-helper-sha256 AUDITED_HELPER_SHA256 \
  --runtime-sha256 AUDITED_JS_SHA256 --wasm-sha256 AUDITED_WASM_SHA256 \
  --profile-offset 0x1234 --seed 0x13579bdf --scene-last 1341 \
  --node /path/to/pinned-node --timeout 30 --cleanup-timeout 6 \
  --out /path/to/fresh-check-directory
```


### Native initializer and original scheduler masks

The [initializer/mask follow-up receipt](../../docs/evidence/source-slippi-native-initializer-mask-v1.json) binds a separate fresh source build. The opt-in initializer uses source slots 1/2, controllers 0/1, two Mario players with color 0, four stocks and seed 4660. Its 60-byte diagnostic and JSON expose copied selection fields and the actual `stage_info.grkind` through a C-only bridge. The default profile retains slots 0/0, colors 0/1 and seed `0x13579bdf`. The existing bare CPP identity remains unchanged in meaning; the bridge has a separate hash export.

For the native initializer, use the prefix command above with `--seed 4660 --initializer-profile native --stage-kind-bridge tests/gameplay_snapshot_stage_kind_bridge.c --stage-kind-bridge-sha256 AUDITED_BRIDGE_SHA256`. Keep the other source/runtime pins and `--scene-last 1341`. Native profile configuration rejects another seed and any initializer change after init. Neither the prefix nor initializer comparison certifies Results or current network consumption.

The pause checker exercises 124 neutral source ticks and 17 ordinary PAD controls. It checks the source-prepared process mask and callback trace; it never writes pause, scheduler masks or callback state. Early P1/P2 START attempts stay paused, then the original pause timer permits P1 resume. Run it against the same fresh Release probe:

```sh
python3 scripts/agent_workspace.py run -- python3 tools/source_pause_mask_check.py \
  --runtime build/browser-release/gameplay_snapshot_probe.js \
  --assets /path/to/local-assets --out /path/to/fresh-pause-check \
  --source-probe tests/gameplay_snapshot_probe.cpp --source-cmake cmake/FighterRuntime.cmake \
  --profile-helper reference-capture/slippi/profiles/slippi_rng_profile_gpl.cpp \
  --stage-kind-bridge tests/gameplay_snapshot_stage_kind_bridge.c \
  --probe-sha256 AUDITED_PROBE_SHA256 --cmake-sha256 AUDITED_CMAKE_SHA256 \
  --profile-helper-sha256 AUDITED_HELPER_SHA256 \
  --stage-kind-bridge-sha256 AUDITED_BRIDGE_SHA256 \
  --runtime-sha256 AUDITED_JS_SHA256 --wasm-sha256 AUDITED_WASM_SHA256 \
  --worker-sha256 AUDITED_PAUSE_WORKER_SHA256 \
  --node /path/to/pinned-node --node-sha256 AUDITED_NODE_SHA256 --timeout 30
```

The launcher reserves five seconds of its whole 30-second cap for owned group cleanup and retains a failure report. Passing requires source close, matching wait/cleanup return codes, released group and absent positive leader PID. A fresh child directory separates worker evidence from the process log. The worker hash is required before spawn, and the launcher records both checker and worker hashes. Supplied symlink inputs are refused before canonicalization. Synthetic controls detect changed callback count and prepared mask; the actual source run checks the full recipe.

An optional MEMFS browser module can be compiled with `python3 scripts/build.py --trace-target gameplay_snapshot_probe_browser --configuration Release`, using the ignored `assets-local/snapshot-mario` fixture. This source-only diagnostic module has no player or draw surface. Its build result is separate from browser execution; it does not enable the multiplayer playing route.

## Pinned inputs and licensing

[`client.lock.json`](client.lock.json) is the source of truth for the client
commit, recursive submodule revisions, build profile, Rust extensions, game
modification and owned disc digest. Do not update one of these components
independently. Apply the two files in [`patches/`](patches/) only to local
working copies of the matching commits; keep clean upstream checkouts intact.
The client patches retain their upstream GPL terms. The generated game
modification retains the pinned ASM repository's GPL-3.0-only terms. The
project-authored matchmaker, peer relay, browser bridge and Python orchestration
have separate root MIT entries; see [`LICENSES.md`](LICENSES.md) and the root
[`LICENSE_SCOPE.md`](../../LICENSE_SCOPE.md).

The run requires these local, ignored artifacts:

- `work/slippi-local-networking/SlippiHeadless.app`, built from the exact
  pinned client and containing its `Contents/Resources/Sys` runtime files;
- `work/slippi-local-networking/matchmaker-build/slippi-local-matchmaker`,
  built from `local_matchmaker/` against ENet from the pinned client checkout;
- the operator-owned CISO whose digest matches `owned_disc.sha256` in the lock.

The harness checks the pinned Dolphin commit and recursive submodules, the exact
applied downstream patch trees, CMake/compiler/Rust build profile, client and
matchmaker binaries, packaged `Sys` resources, game modification and disc
hashes before starting processes.
Its expected local client bundle is macOS arm64 with the Release, NoGUI,
headless-platform, Null-video and No Audio Output settings recorded in the
lock. Use the repository bootstrap and workspace ownership commands before
preparing or rebuilding these artifacts. Do not copy the disc, generated
modification, executable, profile or replay into Git.

## Prepare the local artifacts

From a fresh checkout, these commands create the clean pinned sources and an
isolated patch target. Skip cloning a checkout that is already present, after
verifying its commit and recursive submodules against `client.lock.json`:

```sh
python3 scripts/agent_workspace.py run -- git clone --no-checkout \
  https://github.com/project-slippi/dolphin .deps/slippi-dolphin
python3 scripts/agent_workspace.py run -- git -C .deps/slippi-dolphin checkout --detach \
  41a7a3a110ed52999486ae1901c8fbb9a63d4f13
python3 scripts/agent_workspace.py run -- git -C .deps/slippi-dolphin \
  submodule update --init --recursive
python3 scripts/agent_workspace.py run -- git clone --shared --no-checkout \
  .deps/slippi-dolphin .deps/slippi-dolphin-local
python3 scripts/agent_workspace.py run -- git -C .deps/slippi-dolphin-local checkout --detach \
  41a7a3a110ed52999486ae1901c8fbb9a63d4f13
python3 scripts/agent_workspace.py run -- git -C .deps/slippi-dolphin-local \
  submodule update --init --recursive
python3 scripts/agent_workspace.py run -- git apply \
  --directory=.deps/slippi-dolphin-local \
  reference-capture/slippi/patches/0001-client-loopback-and-observation.patch
python3 scripts/agent_workspace.py run -- git apply \
  --directory=.deps/slippi-dolphin-local/Externals/SlippiRustExtensions \
  reference-capture/slippi/patches/0002-rust-local-endpoints.patch
python3 scripts/agent_workspace.py run -- git clone --no-checkout \
  https://github.com/project-slippi/slippi-ssbm-asm .deps/slippi-ssbm-asm
python3 scripts/agent_workspace.py run -- git -C .deps/slippi-ssbm-asm checkout --detach \
  fcf47f10dc244152c2ebaa3a9dec142ea42243b7
```

The generated `Output/Netplay/GALE01r2.ini` must match the lock hash. Install
Rust 1.88.0 and use the locked CMake, Ninja and Apple Clang versions. The
runner rejects mismatched revisions, patch trees, compiler/build settings, or
generated game modification. Keep the clean Dolphin checkout unchanged.

For the client build, configure with the profile recorded in `client.lock.json`
and build the NoGUI executable:

```sh
python3 scripts/agent_workspace.py run -- cmake \
  -S .deps/slippi-dolphin-local \
  -B work/slippi-local-networking/dolphin-build \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS=-DFMT_CONSTEVAL= \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0.0 \
  -DENABLE_ANALYTICS=OFF -DENABLE_AUTOUPDATE=OFF \
  -DENABLE_HEADLESS=ON -DENABLE_LLVM=OFF -DENABLE_NOGUI=ON \
  -DENABLE_QT=OFF -DENABLE_SDL=ON -DENABLE_TESTING=OFF -DENABLE_TESTS=OFF \
  -DENABLE_VULKAN=OFF -DUSE_DISCORD_PRESENCE=OFF \
  -DUSE_RETRO_ACHIEVEMENTS=OFF -DUSE_SANITIZERS=ON -DUSE_UPNP=OFF
python3 scripts/agent_workspace.py run -- cmake --build \
  work/slippi-local-networking/dolphin-build --target dolphin-emu-nogui
python3 scripts/agent_workspace.py run -- mkdir -p \
  work/slippi-local-networking/SlippiHeadless.app/Contents/MacOS \
  work/slippi-local-networking/SlippiHeadless.app/Contents/Resources
python3 scripts/agent_workspace.py run -- cp \
  work/slippi-local-networking/dolphin-build/Binaries/dolphin-emu-nogui \
  work/slippi-local-networking/SlippiHeadless.app/Contents/MacOS/
python3 scripts/agent_workspace.py run -- cp -R \
  .deps/slippi-dolphin-local/Data/Sys \
  work/slippi-local-networking/SlippiHeadless.app/Contents/Resources/
python3 scripts/agent_workspace.py run -- cp \
  .deps/slippi-ssbm-asm/Output/Netplay/GALE01r2.ini \
  work/slippi-local-networking/SlippiHeadless.app/Contents/Resources/Sys/GameSettings/
```

Build the local matchmaker against ENet from the clean, pinned Dolphin checkout;
it does not need the client patches:

```sh
python3 scripts/agent_workspace.py run -- cmake \
  -S reference-capture/slippi/local_matchmaker \
  -B work/slippi-local-networking/matchmaker-build \
  -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSLIPPI_DOLPHIN_SOURCE=.deps/slippi-dolphin
python3 scripts/agent_workspace.py run -- cmake --build \
  work/slippi-local-networking/matchmaker-build \
  --target slippi-local-matchmaker slippi-local-matchmaker-tests \
  slippi-local-matchmaker-integration-test
python3 scripts/agent_workspace.py run -- python3 -c '
import os
from pathlib import Path

build = Path("work/slippi-local-networking/matchmaker-build")
cache = (build / "CMakeCache.txt").read_text().splitlines()
ctest = next(line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_CTEST_COMMAND:"))
os.execv(ctest, [ctest, "--test-dir", str(build), "--output-on-failure"])
'
```

The local API listener binds only to `127.0.0.1:43114` and returns HTTP 503 for
user, GraphQL and reporting requests. The pinned client adaptations point its
user and GraphQL calls there, disable browser login/update actions, and point
matchmaking at `127.0.0.1:43113`. Analytics, auto-update, Discord presence and
UPnP are disabled in the build profile. The runner samples each owned process
with `lsof`; any non-loopback bind or peer fails the run.
Each private profile has a source-schema-complete local `user.json`, including
the client version pinned in `client.lock.json`. The local API still fails
closed; the profile data lets the original game expose its online modes without
depending on an account service.

## Run

From the repository root, run this command with the path to the owned image:

```sh
python3 scripts/agent_workspace.py run -- python3 reference-capture/slippi/run_local.py \
  --disc /path/to/your/owned/melee.ciso
```

By default it creates two fresh profile pairs, each completing a four-stock
Mario/Mario Direct match followed by a Final Destination rematch in the same
two client processes. Direct mode's first game uses the game's authored random
stage pool; after that game, its loser chooses the rematch map through the
original SSS. The runner confirms from the CSS with source START inputs, which
locks the winner to the loser's stage and sends the loser into SSS. It commits
Final Destination there and checks both replay headers, stock counts, and
source transitions.
The final pair also exercises an intentional peer disconnect. A separate fresh
pair is interrupted with SIGINT after the real peer connection is established,
so the harness can verify its nested process-group and port cleanup. Startup,
pairing, gameplay and rematch each have deadlines. `--repeat 1` is useful for a
single-profile diagnostic; use the default repeat count for the fresh-profile
acceptance run.

### Optional browser-to-desktop transport probe

The optional probe starts one fresh local pair, waits for the original game
scene, then has a real headless Chrome page send 24 fixed eight-byte PAD samples
with frame tags selected from the live desktop PAD sequence. The local ENet
relay replaces only those exact records on player 1's outgoing Slippi PAD
packets, retains their ENet channel and packet flags, and forwards the peer's actual PAD
records back to the browser. Player 2's existing remote-PAD consumer must expose
all 24 browser samples byte-for-byte. The probe ends during the first game after
its input observations; it does not validate a complete match/rematch or execute
Melee in the browser.

After preparing the pinned client, matchmaker, installed Node.js, Playwright,
and headless Chrome, run:

```sh
python3 scripts/agent_workspace.py run -- python3 reference-capture/slippi/run_local.py \
  --disc /path/to/your/owned/melee.ciso \
  --repeat 1 --input-probe-only --browser-transport-probe \
  --browser-node /path/to/node \
  --browser-playwright /path/to/node_modules/playwright
```

The browser service accepts only IPv4 loopback connections from the page's exact
origin. The runtime socket audit includes the headless Chrome process group and
rejects non-loopback endpoints. This remains a local transport smoke test: it
does not exercise public routing, NAT, delay, packet loss, reordering, rollback,
or browser gameplay.

The command exits nonzero at the first failed boundary. It does not retry a
failed pair or reuse its profiles. Each invocation chooses a new evidence
directory under ignored `work/slippi-local-networking/runs/`. Keep failed
directories for diagnosis. A passing `integration.json` links the per-cycle
receipts and interruption receipt. Those receipts contain hashes, minimal
pairing and configuration records, source scene transitions, replay settings
and outcomes, consumed remote-PAD observations, runtime socket destinations,
and cleanup results; they omit disc paths and local play keys. Replay files,
private logs and client binaries remain alongside the ignored local evidence.

Menu actions are ordinary `Pipe/0` controller states. The runner waits for the
original online chooser to reach source frame 50, then uses the original Main
Stick-down input to select Direct. It selects Mario in each fresh CSS before
searching because the original Direct-mode handler ignores START until a
character is selected. CSS stores Mario as CharacterKind 8; SLP headers store
Slippi's Mario character ID 8, while this repository's internal FighterKind is
0. The patched MemoryWatcher sends a
complete first source-memory scan so omitted differential words are never
mistaken for guest zeros. Direct mode then opens the original connect-code entry
screen. The runner uses Z to commit the profile's saved reciprocal code, waits
until the game's source menu reports confirm item 57, then uses A and waits for
that client's local ENet ticket acknowledgement. Before pairing, each isolated
desktop process controls its original CSS cursor through SI port 1. During
matchplay, each isolated process uses its local SI port 1 through pad1. The
Slippi peer assignment separately identifies online player slots 1 and 2, and
the pinned ASM keeps the local pad source index separate from that online
player index.
The post-pair receipt captures both source CSS slots on each client. Confirming
the Direct Code starts the first match through the game's original random-stage
path, so the runner does not inject another CSS or SSS selection after pairing.
It verifies that replay's stage belongs to the pinned Direct random pool. After
that game, both clients return to CSS. It treats replay files as complete only
after that source transition and a full Slippi raw-length envelope parse. The
runner checks the replay's final stocks, confirms both with source START inputs,
then verifies that the winner
stays in CSS and the loser enters SSS. It selects Final Destination as the
loser's stage choice for the rematch. The runner waits 30 source frames after
SSS entry, then samples the original cursor's bounded sweep once per source
frame over the `[-27, 27] × [-19, 19]` cursor region. It commits only after the
source hover table reports Final Destination (kind 32) and that selection stays
stable for six frames; `stage_selection_trace` retains the observed frames,
directions and hover kinds. Replay baselines are captured before the ticket
submission and stage confirmation because each action can start a game before
the scene watcher reports it.
During each match, P1 holds left to self-destruct. After the pinned ASM's
opening input freeze, P2 holds A and right for 30 source frames. The outgoing
Slippi PAD observation records those local frame bytes; each receiver's peer-pad
observer records remote bytes only when the game consumes them. The completed
replays must also show those inputs on the expected local and remote ports.

## Focused service checks

The service and integration test source is MIT-scoped and builds against the
locked ENet checkout. After building the targets above, run its registered
CTest cases:

```sh
python3 scripts/agent_workspace.py run -- python3 -c '
import os
from pathlib import Path

build = Path("work/slippi-local-networking/matchmaker-build")
cache = (build / "CMakeCache.txt").read_text().splitlines()
ctest = next(line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_CTEST_COMMAND:"))
os.execv(ctest, [ctest, "--test-dir", str(build), "--output-on-failure"])
'
```

The suite covers valid reciprocal pairing, malformed requests,
disconnected-ticket cancellation,
timeout, local API fail-closed behavior, idle-client shutdown, and released
service ports. The Python tests cover fresh profiles, source MemoryWatcher
decoding, ordinary Pipe input, loopback socket gates and owned process cleanup.

The run is accepted only when the generated receipt says `passed`; a client
boot, connected peer, short replay prefix, successful build or average frame
rate alone is not the integration result.

## Source-runtime snapshot experiment

The multiplayer workstream's two early questions are controlled desktop
rollback and restoration of WebMelee's own source simulation. The latter has
a separate **Compiled / Native traced** feasibility target. It uses the actual
`GameplayMatchSession`, original raw PAD renewal and scheduler, Mario articles,
Final Destination collision, and native audio synthesis. It does not draw or
enter the browser player. Its [hash-bound receipt](../../docs/evidence/source-snapshot-feasibility-v1.json)
records exact scope, repetitions, coverage, identities, costs and retained
failed hypotheses. Execution issue
[#115](https://github.com/ericvanlare/melee-web/issues/115) tracks acceptance.

Prepare the reviewed source target and supply an operator-owned Mario/FD asset
directory, including the common match, HUD, trophy, rumble and audio files
required by current source ownership. The runtime reports missing files; it
does not exempt assets. Use a new ignored output directory for every run:

```sh
python3 scripts/build.py --configuration Release --trace-target gameplay_snapshot_probe
python3 scripts/agent_workspace.py run -- \
  .deps/emsdk/node/24.19.0_64bit/bin/node scripts/check_source_snapshot.mjs \
  --runtime build/browser-release/gameplay_snapshot_probe.js \
  --assets assets-local/snapshot-mario \
  --out work/source-snapshot/fresh-01
python3 scripts/agent_workspace.py run -- \
  python3 -m unittest discover -s tests -p test_source_snapshot.py -v
```

The fixture is one-shot per module and disables Asyncify. Capture occurs after
a synchronous native
export returns and the combined source-file/HPS/SSM drive owner reports idle.
The host copies the entire Wasm memory and records the exported stack pointer.
Restore requires the same module, full ArrayBuffer identity, capacity and stack
boundary. Active calls, asynchronous calls, shared memory, changed views,
growth and closed owners are refused. A failed initialization releases its
owned files and partially constructed session; it cannot be reused as a fresh
fixture. Source close releases the match, world and audio owners.

The owned linear state includes native globals, C/C++ allocation metadata,
pointer identities and source-address shadows; fighters, items, stage and
collision; HSD objects and process ordering; source RNG and clocks; semantic
PAD Master/Copy/Game history; match/HUD flow; native AX, SSM/HPS transport and
PCM buffers. Restoring preserves pointers because it uses the same memory
instance. No emulator savestate is transplanted. All fixture files are internal
`RuntimeFiles`, so the asynchronous JavaScript file bridge is unreachable.
Native audio is rendered into a copied comparison buffer without a host sound
sink. Replay therefore has no audible commit to duplicate. Source drawing,
GPU submission, Web Audio and persistent card commits are unreachable in this
target; their ownership is still an implementation dependency for the player.

For each checkpoint, the driver advances a known sample-indexed PAD stream,
restores and replays depths 1, 2, 4 and 7 three times. It compares the entire
native observation and PCM bytes at every replayed sample and all linear
memory at each depth endpoint. Every forward sample retains full observation
and PCM hashes. The observation includes both full match-stat
records, semantic PAD history, source frame/RNG, stock/motion and lifecycle
fields, article count and scheduler counts. It requires observed Mario fireball
and both stock-loss coverage. A one-bit source RNG perturbation must diverge,
then an intact restore must recover agreement. Raw input and forward-state
logs, baseline observations, first-divergence details and preparation failures
remain in the ignored run directory.

This is a full-memory correctness baseline, not a production rollback ring.
Wasm mutable globals beyond the exported stack, function tables, JavaScript
module/event-loop state, host files/clock, browser input/clock queues, renderer
resources, externally committed audio and saves are not restored. Endpoint
memory equality does not claim hidden memory equality at every intermediate
sample. No retail/Slippi semantic equivalence, full match/rematch, browser
rollback, visual/audio accuracy, physical input or foreground performance is
established. The next source experiment must account for actual draw and host
ownership and reduce snapshot size/cost before enabling player rollback.

### Bounded shared-page follow-up

The [shared-page receipt](../../docs/evidence/source-shared-page-snapshot-v1.json)
records a Node-only diagnostic owner with at most eight snapshots and a hard
256 MiB limit on unique owned page payload. Each snapshot covers every byte of
linear memory. First capture hashes every page; later captures compare each
page exactly against the most recent retained immutable snapshot. Changed
pages use SHA-256 buckets followed by exact byte comparison. Released handles
drop their page references, and failed captures roll back partial ownership.
No guessed dirty flags or excluded address ranges are used.

Native initialization may grow memory and leave transfers pending. This driver
records the actual capacity, executes original neutral sample 0, requires
native quiescence, then binds the owner. Every later capture and restore retains
the same identity/capacity/stack guards. The complete entry sequence is logged.
The driver repeats the existing depths and sensitivity control, then restores
all eight retained states and verifies observation, PCM and full-memory hashes.
It reports first and subsequent capture costs separately; the payload limit
does not bound total Node RSS or establish browser performance.

To reproduce the measured 64 MiB initial-capacity candidate, first configure
the existing Release build through its normal entry point, set the diagnostic
target's cache value with the checkout's pinned tools, then build it. This value
does not shrink the authored native arenas or prevent growth:

```sh
python3 scripts/build.py --configuration Release --configure-only
python3 scripts/agent_workspace.py run -- env \
  PATH="$PWD/.venv/bin:$PATH" EMSDK="$PWD/.deps/emsdk" \
  EM_CONFIG="$PWD/.deps/emsdk/.emscripten" \
  EM_CACHE="$PWD/.deps/emsdk/upstream/emscripten/cache" \
  EMSDK_PYTHON="$(command -v python3)" \
  .venv/bin/cmake -S . -B build/browser-release \
  -DMELEE_WEB_SNAPSHOT_INITIAL_MEMORY=67108864
python3 scripts/build.py --configuration Release --trace-target gameplay_snapshot_probe
python3 scripts/agent_workspace.py run -- \
  .deps/emsdk/node/24.19.0_64bit/bin/node scripts/check_shared_page_snapshot.mjs \
  --runtime build/browser-release/gameplay_snapshot_probe.js \
  --assets assets-local/snapshot-mario \
  --out work/source-snapshot/shared-pages-fresh-01
python3 scripts/agent_workspace.py run -- \
  python3 -m unittest tests.test_shared_page_snapshot -v
```

Use a second fresh output path for repetition. Initial capacity defaults to
128 MiB in a new build; this is a persistent CMake cache value. Set it back to
`134217728` through the same cache command before returning to the full-copy
driver, which binds memory before initialization. The shared-page driver
records whichever actual capacity was built. Both drivers
remain correctness experiments for the declared source-only boundary. Mutable
Wasm globals/table state outside memory and external browser effects retain
the exclusions above. A first capture still exceeding one frame, and these
headless Node costs, do not close player rollback admission.

Controlled desktop rollback remains a separate gate: retain identical
frame-indexed inputs and initialization, a bounded fault schedule, actual
prediction/load/resimulation observations and finalized state comparisons.
Its diagnostic adaptation must remain part of the pinned client bundle.
