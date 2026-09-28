# Private Slippi local testbed

This is a desktop interoperability testbed. It does not change Melee Web's
browser architecture. The first networking claim is intentionally limited to a
local match/rematch through the replacement matchmaker; rollback correctness,
browser cross-play, public Internet/NAT, production Slippi services, ranking,
rendering, audio and physical-controller usability are separate work.

The [scoped two-cycle receipt](../../docs/evidence/local-slippi-connectivity-v1.json)
records the accepted run and hashes the retained local integration files. The
underlying logs, replays and per-client receipts stay in the ignored run
directory named in that receipt.

## Pinned inputs and licensing

[`client.lock.json`](client.lock.json) is the source of truth for the client
commit, recursive submodule revisions, build profile, Rust extensions, game
modification and owned disc digest. Do not update one of these components
independently. Apply the two files in [`patches/`](patches/) only to local
working copies of the matching commits; keep clean upstream checkouts intact.
The client patches retain their upstream GPL terms. The generated game
modification retains the pinned ASM repository's GPL-3.0-only terms. The
project-authored ENet matchmaker and Python orchestration have separate root
MIT entries; see [`LICENSES.md`](LICENSES.md) and the root
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
  -G Ninja -DSLIPPI_DOLPHIN_SOURCE=.deps/slippi-dolphin
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
