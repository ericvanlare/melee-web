# Project direction: accurate Melee in the browser, then compatible online play

Current thinking as of 2026-09-26. This document connects the overall product
goal with the accuracy, compatibility, rollback and infrastructure projects
needed to reach it. It records direction and hypotheses; it does not admit a
feature or replace the execution order in the [roadmap](ROADMAP.md).

## The overarching project

Build the complete original Melee experience as compiled source running
accurately and performantly in a desktop browser, without a shipped PowerPC
interpreter or JIT. Players supply their own supported disc image locally. The
original game logic, menus, graphics, audio, input behavior and scene lifecycle
are the reference. A convenient browser entry point and a reliable local game
are useful products in their own right.

The longer-term ambition is to extend that foundation into compatible rollback
play: between browser players, and eventually between a browser player and a
player using Slippi's desktop client. Using Slippi's existing services is a
potential route. Operating an independent service is a credible alternative if
needed. These routes share substantial client work but have different account,
matchmaking, transport and community requirements.

The public alpha has limited playable integration. The
[recorded-session comparison](RECORDED_SESSION_STATE.md) establishes agreement
for one declared session and its compared state fields. It does not establish
general game equivalence, pixels, PCM, live-input accuracy or foreground timing.
[STATUS.md](../STATUS.md) remains the current evidence index; measurements and
changing completion claims belong there and in its scoped receipts.

## The connected projects

| Project | Intended result | Dependency or acceptance boundary |
| --- | --- | --- |
| Reliable local game | Ordinary players can complete repeated matches through the original menus, with working controls, sound and stable performance. | The active local milestone and independent gates in the roadmap and accuracy contract. |
| Complete vanilla source port | The original roster, stages, rules, items, CPU, modes, saves and full scene lifecycle work through shared source services. | Incremental admission through the full-game inventory; breadth alone is not accuracy. |
| Accuracy and reference tooling | Reproducible original-game comparisons expose the first divergence and prevent regressions. | Independent references, declared input/state boundaries and retained evidence; supports every runtime project. |
| Slippi compatibility profile | A versioned game profile reproduces the behavior of a selected Slippi release and its modifications. | Vanilla foundation plus an audited set of game patches, settings and emulator-facing behavior. |
| Rollback in the source runtime | Predict, restore and re-simulate without losing correctness or responsiveness. | Complete state ownership, repeatable restore and enough measured execution headroom. |
| Browser networking and cross-play | Inputs reach browser or desktop peers with correct protocol semantics and acceptable latency. | Transport adaptation, synchronization and loss testing; actual play also needs the compatible rollback runtime. |
| Matchmaking and service integration | Players discover opponents, authenticate and establish compatible sessions. | Either a supported integration with official services or our own identities, pairing and infrastructure. |
| Public online operations | An online community can rely on the service over time. | Proven private play first; deployment, diagnostics, abuse handling, updates and optional ranked systems follow. |

The [full-game workflow](FULL_GAME_PORT.md) owns offline feature scope. The
[accuracy contract](ACCURACY_CONTRACT.md) and
[performance and accuracy playbook](PERFORMANCE_AND_ACCURACY.md) own runtime
requirements. The [self-hosting research](SLIPPI_SELF_HOSTING_RESEARCH.md) owns
the inspected Slippi sources, transport findings and infrastructure estimates.
This document provides the relationship between those authorities.

## What 1:1 accuracy means in this plan

Vanilla retail behavior is the first target. Preserve source identities,
authored bounds, arithmetic and float bits, RNG consumption, input sampling,
object/process order and lifecycle. Graphics, audio, physical input and live
performance each need their own evidence. Smooth output or a matching subset
of state cannot stand in for the complete set of gates.

Reference tooling is a continuing part of the product effort. Original captures,
whole-session comparison and first-divergence reproducers make each new fighter,
stage, service or optimization reviewable. Slippi replays provide useful input
workloads, but their recorded state belongs to their original executable and
modifications. The [replay validation architecture](SLIPPI_REPLAY_VALIDATION.md)
requires independent vanilla references for vanilla claims. Recorded state must
never be injected into the running port to conceal divergence.

Slippi compatibility is a second explicit profile. Pin a compatible release
bundle, including applicable game modifications, settings and protocol behavior;
do not assume independently fetched repository heads form that bundle. Preserve
the vanilla profile and its regressions when adding the Slippi profile. Compare
the latter against the selected Slippi executable as well as retaining the
vanilla baseline. A replay parser or a successful server connection does not
establish this profile's gameplay compatibility.

The intended sequence is vanilla accuracy followed by Slippi compatibility.
Small service or transport experiments can reduce uncertainty earlier because
they can use existing desktop clients or synthetic packet workloads. They do
not promote the port's accuracy or supersede the current local milestone.

## Rollback is a separate runtime capability

Matching a forward simulation does not automatically provide a correct rewind.
The port needs a defined state boundary covering all simulation-relevant mutable
state, allocation history, identities and pending work. It must restore an
earlier frame, apply the real input that replaces a prediction, and re-simulate
to the present with the same result as the reference.

Our internal snapshot representation can differ from Dolphin's. The behavior
at the game and network boundaries must agree. Source ownership, renderer
resources and audio output need explicit treatment so restoration does not
leave stale handles, duplicate irreversible effects or replay incorrect sound.
These changes must preserve the compiled-source architecture.

Acceptance needs both deterministic restore tests and forced-rollback matches
against the pinned reference, including repeated matches and teardown. Measure
state capture, restore and re-simulation under the selected rollback window,
then assess the resulting live input, audio and presentation behavior. Existing
forward-play timing cannot establish the extra headroom by itself.

## Three online paths

| Path | What it would provide | What remains to establish |
| --- | --- | --- |
| Browser to browser on our service | Independent online play using a common browser transport and our own pairing. | Correct rollback, signaling, NAT traversal/relay fallback and live quality. This need not speak Slippi ENet unless desktop interoperability is also required. |
| Browser to Slippi desktop on our service | Cross-play using the Slippi-compatible profile and desktop clients adapted to our service endpoints and identity system. | Browser/ENet bridge or another transport adaptation, compatible session setup and complete reference matches. Creates a separate player pool. |
| Browser to existing Slippi players through official services | Access to opponents who remain on the official network, subject to that service's support and requirements. | Maintainer coordination, supported authentication/integration, service acceptance, transport and gameplay compatibility. Public source inspection does not prove this route. |

The [source research](SLIPPI_SELF_HOSTING_RESEARCH.md) supports the feasibility
of replacing matchmaking: the production server is private, while the public
clients expose its contracts and perform gameplay peer to peer. An independent
backend can use its own accounts and credentials. It would not inherit official
users, rankings or access to official queues. Normal desktop installations keep
using their configured Slippi services until deliberately adapted.

Cryptographic feasibility and official-service acceptance are separate
questions. We found no inherent need for a Slippi production secret to operate
our own network. The source inspection does not establish what the private
official backend will accept or whether additional client checks apply.
Official integration remains a collaboration and validation question as well
as an implementation question.

An ordinary webpage cannot open the raw UDP sockets used by Slippi ENet.
Browser-to-browser WebRTC is viable as a candidate transport; a TURN server
alone does not convert it to ENet. For desktop cross-play, the current candidate
is a regional gateway speaking browser transport on one side and ENet on the
other. Alternatives include a local native helper or a desktop client with
browser-compatible transport support. The research links the relevant browser
specifications and public client code.

Keep the website entry point as the preferred browser experience. Choose among
the transport candidates only after measuring full-path latency, jitter, packet
loss and synchronization. A remote gateway preserves that entry point but adds
a hop and relay operation. A helper changes installation requirements; a
desktop transport fork adds distribution and maintenance work. Server placement
cannot compensate for an incorrect input or acknowledgement protocol.

## Sequencing and decision gates

The roadmap continues to order delivery: establish reliable local play, expand
and validate the vanilla game, then admit later compatibility and online scope.
Within that direction, these are the useful decision gates:

1. **Local acceptance.** Complete the original CSS → SSS → four-stock Mario/Final
   Destination → CSS boundary and the roadmap's required repeated-session,
   controller, audio and timing work. Keep current implementation and acceptance
   distinct; use the authoritative documents for the exact active scope.
2. **Vanilla breadth and equivalence.** Expand the full-game inventory through
   shared services and independent original comparisons. Maintain separate
   graphics, PCM, input and performance evidence.
3. **Slippi profile.** Inventory and pin the release bundle, implement its
   behavioral differences, and demonstrate reference agreement before claiming
   compatible gameplay.
4. **Rollback capability.** Establish complete restore semantics and measured
   re-simulation capacity. Offline forced-delay tests should expose state bugs
   before Internet conditions complicate diagnosis.
5. **Private online proof.** Complete matches and rematches with retained
   compatibility, state and timing evidence under controlled network faults.
6. **Service choice and public beta.** Use measured transport results and the
   outcome of official-service discussions to select a supported deployment.
   An independent service remains an available direction if official integration
   is unavailable. Expand population and regions only with operational evidence.
7. **Ranked and other online features.** Add them when reliable play warrants the
   extra product and service scope. Our own ratings would be a separate system;
   public client code does not reproduce Slippi's authoritative rating history.

These gates express dependencies, not a claim that each project must be worked
on serially. Bounded research can proceed independently; integration requires
the relevant prerequisites. Evidence continues to use the playbook labels:
`Compiled`, `Source identified`, `Native traced`, `Retail compared`,
`Browser exercised`, `Performance passed` and `Admitted`, each with its scope.

## Effort, uncertainty and the next experiment

The dated [infrastructure estimates](SLIPPI_SELF_HOSTING_RESEARCH.md#effort-estimate)
range from weeks for a controlled desktop pairing proof to months for reliable
browser cross-play infrastructure, with substantially more work for a mature
ranked service. They exclude game accuracy and the port's rollback engine.
There is no combined delivery date yet. Those larger runtime efforts need
state-ownership analysis, reference coverage and restore/re-simulation
measurements before a useful schedule can be made.

The smallest proposed experiment is a minimal independent matchmaker connecting
two endpoint-adapted desktop Slippi clients. Use a pinned client/mod bundle and
independent test identities. Success means complete matches and rematches across
distinct networks, retained connection logs and explicit failures for unsupported
conditions. It would establish a bounded server result without depending on
Melee Web's unfinished gameplay work.

Follow it with a browser/ENet transport probe measuring actual input arrival
under delay, loss, reordering and disconnects. Only then integrate the port and
claim cross-play after full reference matches with forced rollback. Neither a
successful pairing nor matching checksums alone proves general game accuracy.

The decisions still open are the exact Slippi release target, snapshot design,
transport choice, acceptable measured latency, official-service integration,
supported launch regions and whether ranked belongs in the first public online
release. This document records those questions and the proposed experiments; it
does not start those implementations or change current accuracy requirements.
