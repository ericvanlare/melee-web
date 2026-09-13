# Initial public alpha: silent release candidate

Measured September 12–13, 2026 with Chrome 153.0.8010.36 on macOS.
NaiadAI, LLC is the operator; legal@webmelee.gg is the approved public address.
The operator accepts the unresolved recovered-code distribution risk for this
alpha. Mail forwarding must be tested before the public domain is activated.
This record is scoped release evidence, not accuracy or stability admission.

## Exact candidate

The candidate is built with `runtime-public --configuration Release` in the
separate `build/browser-public-release` directory, then packaged with the
`player` profile in production mode. The native gameplay checkpoint is the
pushed PR10 revision `9f40c504685f29bf06e4317dac26ebbd9a625a6b`; this change
adds the explicit silent policy and public packaging without changing its
fighter command guard.

- Output: `build/alpha-production`; manifest beside it, outside the upload.
- Runtime graph: `runtime/9ab57aebafccdd4d`.
- Complete output: **20 files, 14,556,657 bytes**, including hosting configuration.
- Manifest SHA-256: `833c15bd41513fb4ad9085d91e40288494a5efaa301119e12d3637a52235f94c`.
- Native identity SHA-256: `9ad29559f5d59f768076790598070b31960a5f708dbdf3d7548e02991daf6186`.
- Full notice SHA-256: `e7204d163d1411f3692210d5643bb63dcbebf550a06141bbc6b9d28b76005869`.

| Native file | Bytes | SHA-256 |
| --- | ---: | --- |
| `gameplay_public.js` | 282,317 | `f5faa1845ac0a7a65f21f1577bb889d6df416497604c4af9e987ff012aaf52dc` |
| `gameplay_public.wasm` | 11,936,781 | `a18a3247f047f5e2d2d0be3846fe3afea48205a4c45356b81c8fa61f971fc6db` |
| `gameplay_public.data` | 2,113,536 | `cdf157ee0f1850884f07a71165fd2192177acb23c2c777313b719e70ea67546f` |

Every file fits Pages' 25 MiB per-file limit. The `.data` is the reviewed Aurora
pipeline seed, not a retail archive. The manifest hashes every uploaded path;
its native identity binds source trees, toolchain, compiled exports and seed.
An audited maintenance fallback is retained separately: 12 files, 179,718 bytes.

## GPL audio exclusion and regression

The alpha excludes `gameplay_audio_resample.c/.h`, `dsp-coefficients.mjs`, the
generated `dsp_coef.bin`, and the development browser audio modules. The shared
owner accepts a development audio adapter; production supplies none. Normal
development loads its original coefficient generator and audio transport.

The producer records a v2 disabled-audio policy and hashes the actual Ninja
public target/archive statements and compile command. The audit rechecks those
files and the durable Ninja dependency database. The public source archive omits
the resampler object; `gameplay_audio.c` depends on the independent timing-only
header, not the GPL resampler header. The native public file importer rejects
coefficient input. The source-bound JavaScript allowlist excludes the generator,
worklet, ring and development wrappers. A rewritten manifest or a legacy v1
identity cannot authorize an audio-enabled artifact.

The public branch retains source SEM/AX callback cadence and advances sample
cursors through existing finite sample, loop and HPS handoff handling. It emits
no PCM and creates no browser audio context. It skips envelope ramps, mixing,
ITD history and effects. The small clock test covers half/increased/zero rates,
nearest-selector behavior and fractional partition invariance; it does not
establish complete bank/stream lifetime or audio-state equivalence. No
independently validated audio replacement was claimed. Full fidelity remains
on the [roadmap](ROADMAP.md).

The reviewed dependency notices explicitly select the FreeType License (FTL),
include its attribution, and record excluded development GPL sources separately.
The original SDK directory named `dolphin` is distinct from Dolphin Emulator;
its unresolved distribution basis remains part of the operator's accepted risk.

## Automated validation

Both the public Release native target and the ordinary development runtime
compile successfully. The 36 focused public build/audit/HTTP tests pass under
normal Python and `python -O`, including stale source/proof rejection, exact
manifest enforcement, legacy/audio-enabled identity rejection and forbidden
audio routes. The new silent-clock C test runs through normal test discovery.
**642 tests ran, 50 skipped, with no failures** in final unittest discovery.
The skips require optional owned reference assets, traces or tools. The same
36 public release tests also pass in a fresh tracked-files checkout without
installed native dependencies or existing build output.

## Browser and gameplay evidence

The silent public entry passed **10 actual browser checks** through local
Wrangler Pages: direct isolated WebGPU/Wasm startup, control preferences,
invalid-disc retry and acknowledgement, narrow widths/fullscreen, owned-disc
import and CSS, pause/resume, ordinary-keyboard SSS/cancel, Eject/reload and a
second import/launch, legal pages/full notices, and storage/network inventory.
No page or CSP errors, Web Audio contexts, uploads or WebSockets were observed.
Only the keyboard preference key persists; no IndexedDB, Cache Storage,
service worker or sessionStorage was observed.

A separate ordinary-keyboard run entered a four-stock Mario versus CPU Mario
match on Final Destination, then reproduced the existing native failure:

```
Unsupported native fighter command opcode 63
Bound fighter 0: motion 12, animation 2
Bound fighter 1: motion 264, animation 239
Aborted()
```

The public restart error appeared. This is a retained failure, not a full-match
pass. The disc dialog and About page disclose it. No command guard was weakened,
no native gameplay state was injected, and no diagnostic export was added to the
public binary; the local diagnostic run only observed stderr.

The unchanged development/prototype browser test passed its first **13 checks**,
including owned-disc loading, menus and autonomous CPU gameplay, then failed
No Contest with `CSS exited with unsupported pending scene 0`. The same test
against the earlier pre-audio-split development bundle reproduced the same
failure after the same 13 checks. Neither run is counted as a complete browser
regression pass. Existing accuracy and gameplay tests remain in place; no new
retail reference comparison, PCM, physical-controller, full-match, multiplayer
or performance admission is claimed.

Raw logs and game screenshots stay in ignored local directories. They are not
committed or uploaded. Only original empty-player UI screenshots may be shared.

## Hosted verification and launch gate

The exact candidate is deployed to
[8e2cdf90.webmelee.pages.dev](https://8e2cdf90.webmelee.pages.dev), with the
[staging alias](https://staging.webmelee.pages.dev). It is non-indexed. The
staging upload records implementation commit `f56dab9`; later release-record
updates do not change its public bytes.

Hosted HTTP verification passes for all **18 served files and five HTML
aliases**, comparing bytes to the final manifest, and **32 absent routes**,
including root and runtime-scoped development audio paths. `_headers` and
`_redirects` are consumed hosting configuration and return 404. Security,
isolation, MIME and caching checks pass; unversioned notices revalidate.
Cloudflare initially rejected Python's default user agent with error 1010;
the verifier now identifies itself as `WebMelee-Release-Audit/1.0`. Normal
Chrome and that explicit audit client both succeed.

The hosted public browser repeat passes all **10 checks**. Its request inventory
contains only same-origin static GET requests; no browser audio contexts,
page/CSP errors or application uploads were observed. The hosted runtime hash
matches the locally tested silent CPU-match artifact exactly.

The email setup task verified actual delivery to the intended Workspace inbox
before cutover (September 12, 2026, 22:02 Pacific). The original mail gate is
closed. The verified MX/SPF/Google domain TXT were copied into Cloudflare and
queried successfully at both assigned nameservers. Namecheap then saved the
Cloudflare delegation. The same exact files were deployed to the production
Pages environment at [928714aa.webmelee.pages.dev](https://928714aa.webmelee.pages.dev);
its default production hostname also passes all 18-file/five-alias/32-404 HTTP
checks.

Registry propagation, apex/www attachment, HTTPS, redirects and a post-migration
receipt test remain in progress. The registrar's saved values alone are not
proof of propagated DNS or a finished public launch.

A 15-minute external watch ended at September 13, 05:29 UTC with both queried
`.gg` authorities and public resolvers still returning the old registrar
nameservers. Public Google MX/SPF records were correct. GitHub's public-shell
job passed; browser-build remained pending and public-player CI was skipped.
Those CI states are separate from the measured local and hosted checks above.
