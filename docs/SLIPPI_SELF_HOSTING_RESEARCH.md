# Slippi self-hosting feasibility research

Research date: 2026-09-26. Evidence level: **Source identified** in the public
upstream paths below. This is an architecture assessment, not a working server,
interoperability result, security audit, gameplay comparison or latency result.
No production account credentials or live matchmaking were used.

An independent service supporting Slippi-compatible rollback is technically
credible. Public client and game-mod sources expose the important boundaries.
The production matchmaking implementation is private, so this would involve
building replacement services. The likely larger challenges for Melee Web are
browser/native transport and correct, fast rollback in the source port.

This research does not change the [roadmap](ROADMAP.md) or establish any of the
[accuracy contract](ACCURACY_CONTRACT.md) gates.
See [project direction](PROJECT_DIRECTION.md) for how this work relates to the
vanilla source port, reference tooling, Slippi compatibility and rollback.

## What is public

The official [developer guide](https://github.com/project-slippi/slippi-wiki/blob/master/GETTING_STARTED.md)
states that the matchmaking server lives in a private repository and is not
currently planned for open sourcing. It also identifies rollback as work shared
between the emulator and the game assembly mods. The public materials inspected
do not provide a complete deployable production backend.

| Component | Public evidence | Consequence |
| --- | --- | --- |
| Matchmaking client | [SlippiMatchmaking.cpp](https://github.com/project-slippi/Ishiiruka/blob/60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2/Source/Core/Core/Slippi/SlippiMatchmaking.cpp) | Request/response contracts can guide a replacement service. |
| Gameplay transport | [SlippiNetplay.cpp](https://github.com/project-slippi/Ishiiruka/blob/60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2/Source/Core/Core/Slippi/SlippiNetplay.cpp) | Peer connections, input history, acknowledgements, selections and synchronization behavior are visible. |
| Game modifications | [Slippi SSBM ASM](https://github.com/project-slippi/slippi-ssbm-asm/tree/fcf47f10dc244152c2ebaa3a9dec142ea42243b7) | Online hooks and rollback engine control are available for inspection. |
| Emulator state capture | [SlippiSavestate.cpp](https://github.com/project-slippi/Ishiiruka/blob/60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2/Source/Core/Core/Slippi/SlippiSavestate.cpp) | Shows the emulator's memory/state boundaries; these need adaptation to our runtime. |
| Account/API clients | [user module](https://github.com/project-slippi/slippi-rust-extensions/blob/c7888e0f4dec0054bd298bbc71c5b2c6636599b7/user/src/lib.rs), [GraphQL client](https://github.com/project-slippi/slippi-rust-extensions/blob/c7888e0f4dec0054bd298bbc71c5b2c6636599b7/slippi-gg-api/src/graphql.rs) | Public profile/authentication plumbing does not include the account service itself. |
| Match reporting client | [report queue](https://github.com/project-slippi/slippi-rust-extensions/blob/c7888e0f4dec0054bd298bbc71c5b2c6636599b7/game-reporter/src/queue.rs) | Result/status requests and replay-upload handling are visible; server decisions remain separate. |

Source availability does not imply MIT licensing. The inspected Dolphin netplay
header identifies GPLv2+, and the ASM repository identifies GPL-3.0. Any later
reuse must preserve the applicable provenance and license boundaries.

## What our servers would do

The inspected matchmaking client uses ENet and reliable JSON. It sends
`create-ticket`, expects `create-ticket-resp`, then receives `get-ticket-resp`.
The request carries user identity and `playKey`, search mode/code, client version
and a LAN endpoint. The assignment supplies players, network endpoints, port
assignments, host selection, match identity and rules. The client closes its
matchmaking connection before connecting to its peers. See the pinned
[matchmaking implementation](https://github.com/project-slippi/Ishiiruka/blob/60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2/Source/Core/Core/Slippi/SlippiMatchmaking.cpp).

The matchmaking and gameplay clients reuse a local UDP port to support NAT
hole punching. Assignment handling chooses between external and LAN endpoints.
This direct-connection mechanism is distinct from the proposed browser
WebRTC/STUN/TURN path below; it needs its own cross-network validation.

Consequently, an initial replacement needs session identity, direct-code pairing,
version/rules compatibility, endpoint discovery, assignment and cleanup. Unranked
queues add region/latency policy. A public service adds operational work such as
rate limits, abuse handling, monitoring and recovery.

Gameplay is exchanged between clients. Input and acknowledgement messages use an
unsequenced ENet channel; other control traffic uses reliable delivery. The
sender includes input history and a finalized-frame checksum. These are compact
game records, not streamed rendered frames. A relay would forward packets; it
would not need to run a Melee simulation. This inference follows the public
[send and receive paths](https://github.com/project-slippi/Ishiiruka/blob/60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2/Source/Core/Core/Slippi/SlippiNetplay.cpp).

Hosting compute should therefore be relatively modest at small scale. Geography,
relay bandwidth, connection reliability and ongoing operations matter more than
GPU capacity. No concurrency benchmark or hosting quote was produced here.

## Browser transport is an additional project

An ordinary website cannot open arbitrary UDP sockets, as documented by
[Chrome's Direct Sockets guide](https://developer.chrome.com/docs/iwa/direct-sockets).
Its alternative requires an Isolated Web App, which is a different distribution
model from visiting webmelee.gg. Compiling ENet to Wasm alone does not remove the
browser restriction.

For browser-to-browser play, WebRTC data channels offer peer transport, with
signaling and STUN/TURN infrastructure. They support configurable ordering and
reliability, but their protocol is SCTP/DTLS, not ENet. See the
[WebRTC specification](https://www.w3.org/TR/webrtc/#peer-to-peer-data-api).
TURN is therefore not a generic converter between WebRTC and Slippi ENet.

For browser-to-desktop cross-play, a plausible design is a regional gateway:
the browser uses WebRTC or WebTransport datagrams to reach it, and the gateway
speaks ENet to the desktop client. [WebTransport](https://www.w3.org/TR/webtransport/)
provides browser-to-server transport, not a raw UDP socket. This gateway design
is an inference, not a tested implementation. It must preserve message ordering
where required, input timing, retransmission behavior and meaningful latency
measurement across both legs; acknowledging inputs early at the gateway could
otherwise hide their true delivery delay.

A gateway adds a network hop. Regional placement can reduce the penalty, but
only measured tail latency and loss tests can establish whether it is acceptable.
A native helper on the browser player's machine is another option, trading
installation friction for avoiding a remote relay detour. A desktop fork with
WebRTC support is a third option with its own maintenance cost.

## Independent service versus official Slippi

The inspected [endpoint configuration](https://github.com/project-slippi/Ishiiruka/blob/60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2/Source/Core/Core/Slippi/SlippiMatchmaking.h)
hardcodes production/development matchmaking hosts. A Slippi desktop build
adapted to our endpoint and identity service is the straightforward independent
network target. Account and reporting endpoints also need replacement or an
explicit private-mode implementation; changing only the matchmaking hostname
does not replace the whole service.

This would create a separate player pool. It would not inherit official users,
queues, ratings, subscriptions or permission to participate in official ranked.
An unchanged, normally configured Slippi installation would keep using Slippi's
service. A gateway may appear as an ordinary ENet peer at the gameplay boundary,
but it does not by itself solve discovery or official-service access.

For our own service, we can issue our own identities and credentials. No Slippi
production secret is inherently required to operate an independent network.
The reviewed connection paths do not establish a proprietary cryptographic
barrier to that architecture. This is not a proof that official infrastructure
will accept a third-party client: its private validation, policy and future
changes are outside this inspection.

Ranked functionality is larger in scope. The public Rust code sends game/status
reports and consumes server results. We can create our own rating system, but
that does not reproduce Slippi's private matchmaking policy, result adjudication
or authoritative rating history. See the [rank result client](https://github.com/project-slippi/slippi-rust-extensions/blob/c7888e0f4dec0054bd298bbc71c5b2c6636599b7/user/src/rank_fetcher/network.rs).

## Effort estimate

These are judgment-based planning ranges for one experienced engineer familiar
with the relevant networking stack. They are alternative total infrastructure
scopes, not additive milestones or measured delivery commitments. They exclude
achieving game accuracy and implementing the port's rollback engine.

| Infrastructure scope | Estimated engineering effort |
| --- | --- |
| Controlled same-machine proof: private direct pairing and two endpoint-adapted headless Slippi clients | 1–3 engineer-weeks |
| Small independent community service: accounts, direct/unranked pairing, deployment and basic operational reliability | 4–8 engineer-weeks |
| Browser-to-desktop service with a gateway, client integration, NAT/loss testing and useful diagnostics | 8–16 engineer-weeks |
| Mature ranked service with reporting, moderation, regional operations and release maintenance | 6–12+ engineer-months, plus ongoing operation |

The initial prototype is substantially smaller than recreating the complete
Slippi product. Ordinary server deployment is a small part of these estimates;
reliability and integration dominate.

Even after retail and Slippi gameplay equivalence, Melee Web still needs correct
snapshot/restore, input prediction, bounded re-simulation, frame synchronization,
checksum compatibility and rollback-aware sound/presentation. Slippi's
[rollback engine loop](https://github.com/project-slippi/slippi-ssbm-asm/blob/fcf47f10dc244152c2ebaa3a9dec142ea42243b7/Online/Core/LoopEngineForRollback.asm)
explicitly handles sound reconciliation as well as repeated simulation. Emulator
memory snapshots cannot simply be transplanted into our source runtime. Reserve
a separate, potentially multi-month client effort; a tighter estimate needs an
actual restore/re-simulate benchmark and state-ownership inventory.

## First online infrastructure milestone: two local headless clients

Run our matchmaker and two adapted desktop Slippi processes on the same
development machine, controlled by one bounded command. This is the first
online infrastructure milestone, separate from the port's active local-game
acceptance milestone. It can isolate server feasibility using existing Slippi
gameplay and rollback before integrating Melee Web.

The initial implementation should provide:

1. **A pinned native headless client.** Select and build a Slippi client/mod
   bundle for this machine. Verify that the chosen no-GUI platform creates no
   visible windows, takes no focus and emits no audible output. Suppressing
   presentation must preserve game update/draw callbacks and emulated scheduling.
   Record the video/audio profile; this run makes no pixel or PCM claim. Preserve
   upstream checkouts and explain the adaptation in downstream patches.
2. **Two isolated instances.** Give each client its own user/configuration and
   save directories, test identity, input channel, logs, replay output and UDP
   port. The owned disc may be shared read-only. Keep all mutable test state in
   ignored local directories; leave personal Slippi profiles untouched.
3. **Our actual local service.** Exercise private direct-code pairing through
   the replacement matchmaking protocol and real ENet peer connections over
   loopback. Use independent test credentials and explicit local service
   endpoints, with no dependency on official accounts or production matchmaking.
   Keep ordinary match initialization and the pinned gameplay modifications.
4. **Automated gameplay.** Drive each local player's ordinary controller input
   through Slippi's menus into a four-stock Mario/Final Destination match. Finish
   the match and a rematch. Do not inject expected game state or add gameplay
   modifications merely to make the harness advance.
5. **Observed rollback and synchronization.** Run a baseline and a bounded
   impairment case using a local packet proxy, without changing machine-wide
   network settings. Delay selected inputs enough to cause prediction correction;
   retain evidence that restore/re-simulation actually occurred. Compare the
   finalized input timeline and declared game-state fields on both clients and
   against the baseline under the same initial conditions and scripted inputs.
   Record frame, RNG, fighter state, stocks and outcome at explicit boundaries;
   agreement of sparse network checksums alone is insufficient.
6. **A repeatable pass/fail report.** Pin source, patches, binaries, codeset,
   configuration, input scripts and fault schedule. Retain logs, replays and
   first-divergence diagnostics locally. Bound startup and every run phase,
   exercise disconnect and failed-pairing paths, and clean up only owned
   processes. A timeout fails at its boundary; it is not silently retried.

The build and boot check is the first checkpoint within this milestone. No
adapted Slippi binary has yet been built or launched by this research, and two
instances sustaining the required cadence on this machine remains unmeasured.
The repository's [reference Dolphin builder](../scripts/build_reference_dolphin.py)
already describes a headless build route, but that separately pinned vanilla
reference is not evidence that a selected Slippi build passes this gate.

Source inspection favors the newer `project-slippi/dolphin` fork for this Mac.
Its [no-GUI build](https://github.com/project-slippi/dolphin/blob/41a7a3a110ed52999486ae1901c8fbb9a63d4f13/Source/Core/DolphinNoGUI/CMakeLists.txt)
defines the `dolphin-nogui` target, emits `dolphin-emu-nogui`, and links the Slippi
Rust extensions on Apple as well as other platforms. Its
[platform selection](https://github.com/project-slippi/dolphin/blob/41a7a3a110ed52999486ae1901c8fbb9a63d4f13/Source/Core/DolphinNoGUI/MainNoGUI.cpp)
requires explicit `-p headless`: the default Apple platform is `macos`, which is
windowed. The older pinned Ishiiruka
[no-GUI target](https://github.com/project-slippi/Ishiiruka/blob/60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2/Source/Core/DolphinWX/CMakeLists.txt)
is gated on X11/headless configuration, so its desktop Mac build is not an
interchangeable starting point. These are source findings, not tested commands.

The newer fork's [Pipes controller backend](https://github.com/project-slippi/dolphin/blob/41a7a3a110ed52999486ae1901c8fbb9a63d4f13/Source/Core/InputCommon/ControllerInterface/Pipes/Pipes.cpp)
is a candidate scripted-input route. Its compile-time enablement and actual
per-frame input delivery on this machine still need verification. Headless mode
cannot be assumed to accept desktop keyboard automation. Prove the input bridge
before attempting timed pairing or full matches.

This milestone establishes local functional evidence only. Loopback does not
exercise NAT hole punching, Internet path selection or geographic latency;
same-host CPU contention also differs from two players on separate machines.
Cross-network operation and browser interoperability remain later gates. A
passing headless pair does not admit Melee Web gameplay or foreground timing.

## Smallest useful validation sequence

1. Complete the same-machine headless pair above, including observed rollback
   and a clean match/rematch outcome.
2. Run the adapted desktop pair across distinct machines and networks. Exercise
   NAT/connection failures and measure delivery under real network conditions.
3. Test the browser/ENet gateway with deterministic packet workloads, including
   delay, jitter, loss, reordering and disconnects. Measure full-path input
   arrival, not merely gateway acknowledgements.
4. Integrate a pinned Slippi-compatible port profile and compare complete matches
   against the pinned desktop reference under forced rollback. Diagnose the
   first divergence, including audio/presentation effects and restore history.
5. Expand to queues and public operations after those boundaries pass.

The decisive first experiment can happen independently of achieving complete
Melee Web accuracy. Architectural confidence is high that a basic replacement
service is achievable; browser cross-play quality remains conditional on
engineering and measured performance. This confidence is an assessment from
source inspection, not a demonstrated interoperability result.

## Inspection receipt

Public source trees and selected files were fetched read-only at these commits:

- Ishiiruka: `60f7b63496fb6ec7b9180a04f16f3edc0ad89fe2`.
- Mainline Dolphin fork: `41a7a3a110ed52999486ae1901c8fbb9a63d4f13`;
  its matchmaking header also uses the production/development Slippi endpoints.
- Slippi Rust Extensions: `c7888e0f4dec0054bd298bbc71c5b2c6636599b7`.
- Slippi SSBM ASM: `fcf47f10dc244152c2ebaa3a9dec142ea42243b7`.

Local, ignored inspection inputs and SHA-256 file identities are retained in
`work/slippi-hosting-research/source-manifest.json`. The linked commit URLs are
the portable evidence. Branch heads are research snapshots, not a tested
compatible release bundle. No dependency or executable behavior changed.
