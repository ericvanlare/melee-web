# H1 headless natural route completed once without a timing pause

**Source identified / Compiled / Browser exercised**. The
[portable receipt](../evidence/h1-natural-supported-route-v1.json) records the
run `h1-natural-route-eea23b7-20261006-204900-bdd5a152` and its source, build,
tool, input, owner, inventory and screenshot identities.

The Release producer was candidate commit `eea23b7905d1a3ce03c3a1e554aa8d4135e2a0cc`
(tree `f847427a9e0dba6ac6929ed4b8b0c62607889126`), whose tree is identical to
merged main `e706a364f29c9279467b6e0540c628f9e6bbd16c`. The run used installed
headless Chrome 154.0.8037.98, a fresh profile, and the original disc identified
by SHA-256 in the receipt. All 32 local Release artifact identities matched
independent HTTP inventories before and after the capture.

One four-stock match of four Mario CPU9 players on Final Destination followed
the original CSS → SSS → gameplay → Results → CSS route. The match ended at
source frame 14533, reached Results, accepted the gated P1 keyboard Enter after
the CPU pages advanced, and returned to CSS. The harness stopped on any timing
pause and did not automatically resume. It recorded zero timing-pause receipts,
zero source-timing disruptions, zero page errors or crashes, and zero native
command errors. Its incident log retained 16 reason-7 render-preparation
observations with no dropped incidents; these were not timing-guard pauses.
Callback samples were bounded, with 16,722 samples dropped, so this run is not
performance evidence.

The named headless candidate/profile met H1's natural-route no-pause outcome
once. This permits bounded A3 relay engineering under D1, but does not diagnose
the historical pause in #116 or establish general gameplay headroom. The host
was uncontrolled; one preexisting unrelated Chrome identity was left untouched
and no cause is inferred. The run makes no claim about retail comparison,
exact-pixel equivalence, foreground timing, physical input, uninterrupted
audible output, Internet play, competitive acceptance or performance. The #84
foreground, three-match and audio gates remain open.

The capture and owner commands both exited zero, and owned browser/server
cleanup and pre/post inventories passed. Separately, the one-shot outer
controller exited 2 after its post-capture marker handshake received a blank
line instead of the required release token. After reviewing the report and
images, the exact marker identity was checked and only that marker was removed;
the passing run's fresh profile was removed after process cleanup verification.
