# UCF 0.84 shield-drop component

**Source identified / Native and Node/Wasm component / Live profile acceptance unproved**

[Milestone #337](https://github.com/ericvanlare/melee-web/issues/337) adds plain
UCF shield drop to the private diagnostic profile under [mode identity #9](https://github.com/ericvanlare/melee-web/issues/9).
This is one behavior in **Slippi-compatible rules (unofficial)**. The project is
not affiliated with or endorsed by Project Slippi. It is not a complete UCF
profile, an available player mode or browser rollback acceptance.

## Source boundary

The independently reviewed contract maps the pinned Shield Drop patch to both
boolean callers of the original neutral-escape transition. Their existing
main-stick, Y-timer, held-button and C-stick entry conditions remain unchanged.
The additional predicate uses ordered C-stick and main-stick comparisons, the
unsigned X-timer byte against the signed common window, a valid platform floor,
and the signed quantized-radius result. Only when every guard passes does the
caller return false before the transition. Otherwise the original Yoshi/default
transition and subsequent cleanup retain their order.

The helper shares only the reviewed axis quantization and radius calculation
with DBOOC. It does not call the DBOOC behavior with fabricated inputs. The fixed
private identity is `slippi-render-dependencies-dbooc-shield-drop-v5`; earlier
captures retain their historical identities and do not validate this addition.
All derived behavior and its tests remain in the isolated GPL scope.
The [structured receipt](../evidence/ucf-shield-drop-v1.json) binds source,
reviews, failures and validation identities.

## Validation and retained failures

The initial native helper attempts imported incompatible full SDK headers into
a standalone scalar probe and failed before behavior execution. Both are
retained. Reduced review separates scalar code from the production typed
adapter and checks source layouts with the actual Wasm include policy.
A subsequent extraction failure is also retained. The final native UBSan and
Node/Wasm fixture passes 26 asserted scalar scenarios, 156 actual prepared
original/profile/omitted caller checks, and 20 shared-radius trace checks.
The actual prepared Escape translation unit and source type/offset guards
compile for Wasm. With the profile disabled, preprocessed caller bodies match
the ordinary source exactly. The existing 386-row DBOOC output remains
byte-identical to v4. Services and Fighter storage in the caller reducer are
synthetic; the separate compile guards bind the real source layout.

Root review found three missing scalar assertions in the initial fixture; all
26 are now unconditional. That correction leaves implementation and output
unchanged. On implementation commit `9ae4d794`, both the private-profile and
ordinary-runtime Release builds pass. Full regression passes 2,426 tests with
166 skips; 18 affected runtime/build artifacts are retained by hash. Exact-head
CI and integration remain separate. No new browser or reference capture was run.

## Reference configuration and remaining gates

The reference harness previously copied the entire bundled game-settings file
into a fresh local profile. The pinned client appends global and local Gecko
code lists, so that copy would duplicate the selected patch bundle. The repair
keeps the validated bundle in its existing location and leaves the local
GameSettings directory empty. Focused configuration controls and independent
review pass; this does not imply the reference client has executed.

The process-local default-deny fixture passes its declared synthetic network,
IPC, file and execution controls. It is a prerequisite only: no Slippi client
has been admitted or launched, and no official service has been contacted.
A known prior client artifact differs from the current pinned binary/resource
identity and is not accepted as the reference. Exact client lineage and its
restricted offline initialization remain separate work.

Shared Pad Buffer/Cardinals history, its snapshot/reset ownership, extended
shield drop, other UCF behaviors, live profile activation, matched patched-game
execution and full rollback remain open. The component assumes nearest-even
arithmetic and masked exceptions; FPSCR/traps, non-default rounding and NaN
payload fidelity are not established.
