# Earlier audio-enabled integration evidence

This records the pre-alpha v1 candidate, which was never published. The initial
public alpha uses a later, explicitly audio-disabled profile to exclude the
GPL-derived audio implementations. See `PUBLIC_ALPHA_VALIDATION.md` for that
candidate; do not treat the audio results below as silent-alpha evidence.

Measured September 12, 2026 on macOS with headed Chrome 153.0.8010.36 and
Playwright 1.58.2. This is interface, release-boundary and scoped browser evidence.
It is not retail equivalence, PCM, physical-controller, full-match or performance
admission. No fresh reference holdouts were used.

## Candidate identity

The native source checkpoint inherited from PR10 is
`9f40c504685f29bf06e4317dac26ebbd9a625a6b`. This branch adds the shared browser
owner and public packaging; it does not author native gameplay changes.
The player graph is `runtime/a5d5f3613364650d`. Its hash covers every native,
JavaScript, audio and player-style byte in that directory. Legal HTML and its
separate hashed stylesheet are outside that graph and must be checked through
the complete release manifest.

The three Release public native files total 14,345,868 bytes. The largest file
is the 11,949,943-byte Wasm program. All assets fit Pages' 25 MiB per-file limit.
The native identity sidecar SHA-256 is
`a104a7f478d92c17680be33f352fae901656fac3176b32d5ff42edb8b4a3bb74`.
It records compiled exports, source/build inputs and prepared source, toolchain,
pipeline seed and native file hashes. The full release inventory has 22 files;
use its generated manifest for current legal text and total byte size.

## Passed checks

- Full unittest discovery: **633 tests, 50 skipped**, no failures. Skips require
  optional owned assets, traces or tools not installed in this isolated worktree.
  The final 30 public release checks pass in a fresh tracked-files checkout
  without installed dependencies or existing native build outputs. They include
  source/toolchain/prepared-patch/seed drift rejection in both build and audit.
- Release `runtime-public` and development `gameplay_menu_browser` both compile.
  The public output has the 16 required lifecycle/input JS bindings, actual Wasm
  export verification, no public diagnostic/replay/raw-PAD/memory bindings, no
  profiling names and no dynamic JavaScript execution. The compiled artifacts
  were scanned for local checkout paths.
- The existing development/prototype browser regression passes **17 checks**:
  keyboard layouts and focus, invalid disc/retry, responsive widths/fullscreen,
  real local disc preparation, original CSS/SSS, Mario CPU gameplay on Final
  Destination, ordinary-keyboard No Contest back to CSS, Eject and a separate
  development-page CPU launch. No page/HTTP errors, uploads or timing resumes.
- The actual public entry through Wrangler Pages passes **10 checks**: direct
  isolated WebGPU/Wasm startup, controls and persistence, acknowledgement and
  invalid-disc retry, 320/390/768 widths and fullscreen, owned-disc import and
  original CSS/SSS/cancellation, native pause/resume, Eject/reload and a second
  import/launch, readable legal pages/full notices, storage and request inventory.
- The public browser session records **63 requests**, all same-origin static
  GETs without bodies or query strings. No WebSockets or CSP/page errors were
  observed. Only `melee-prototype-keyboard-v1` exists in localStorage; cookies,
  sessionStorage, IndexedDB, Cache Storage and service workers are absent.
- CDP observes real 32000 Hz Web Audio contexts starting and retiring. This does
  not establish acoustic output or reference PCM equality.
- Real Pages HTTP verification compares **21 resources** byte-for-byte against
  the manifest, including legal aliases, module/Wasm/data MIME, isolation, CSP,
  cache behavior and **19 absent developer/unknown routes**. Wrangler's reserved
  configuration-path ENOTDIR/502 exception is recorded only on loopback and is
  not accepted for a hosted deployment.

The small owner test also checks separate native focus/visibility arguments,
synchronous command drainage, disc replacement failure with Eject still
available, audio acknowledgement, the void native pause API and reload-only
heap retirement. Browser evidence corrected a test that sent SSS Cancel during
scene preparation; the test now waits for active native state and the source
menu's entry animation.

## Retained longer-play failure

A separate check of the exact public graph enters original CSS/SSS, selects
Final Destination with ordinary B0XX keyboard events, and visibly runs a
four-stock Mario versus CPU Mario match. A longer run stops in native code.
A local diagnostic repeat observing stderr reports:

```
Unsupported native fighter command opcode 63
Bound fighter 0: motion 0, animation -1
Bound fighter 1: motion 264, animation 239
```

The handler in `src/gameplay_action_store.c/.cpp/.h`, the native browser owner
and reviewed gameplay patch are byte-identical to `9f40c50`. Opcode 63 is the
intentional sentinel installed for action rows outside the hydrated command
inventory. The native `melee_web_command_require_supported` guard aborts on it;
this is an existing unsupported engine boundary, not a missing public JS export.

The public error/restart dialog appears. The opcode failure is not waived or
counted as a complete-match pass. No native command was stubbed, no gameplay
state was injected and no diagnostic exports were restored in production.
The check changes only a local stderr observer for diagnosis. The public About
page discloses that some CPU actions can stop the player.

An extended development run with ordinary movement/attack/jump and a 30-second
CPU interval does not reproduce that abort, but its No Contest sequence returns
through CSS and then exits with unsupported pending scene 0. It is retained as
a failed extended sequence, not a passing repeat-loop result. Different live
input timing and CPU trajectories prevent treating this as a controlled native
regression comparison. The short 17-check regression above remains separately
scoped and successful.

Raw local logs, manifests and game screenshots stay in ignored `work/` and
`build/`. Only original shell screenshots are checked into documentation.
No game imagery or user-selected bytes are included in the deployment bundle.

## Publication status at this earlier checkpoint

The following status describes only the earlier v1 checkpoint. See
[the alpha record](PUBLIC_ALPHA_VALIDATION.md) for the operator decision and
current candidate.

No Pages deployment, custom domain or DNS cutover had been completed. The Pages
project and zone are prepared. Public operator/contact facts are still missing;
GPL corresponding-source delivery and the recovered-code/seed distribution
basis remain documented release decisions. Full third-party notice texts are
included, but notices alone do not complete source delivery or clear rights.
See [release review](PUBLIC_RELEASE_REVIEW.md) and
[deployment runbook](PUBLIC_DEPLOYMENT.md).
