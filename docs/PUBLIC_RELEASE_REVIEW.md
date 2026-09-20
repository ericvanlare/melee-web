# WebMelee public player release review

Research date: September 12, 2026. Prepared for operator and qualified counsel
review. This is a technical inventory and issue assessment, not a legal
opinion, trademark clearance or assurance of legal protection.

## September 19 audio implementation and preview update

The current resampler and coefficient generator have replacement
implementations; see [their evidence and provenance](AUDIO_REPLACEMENT_EVIDENCE.md).
The numerical coefficient table is deliberately unchanged. The [PR #42
integration receipt](evidence/audio-main-integration-v1.json) records bounded
browser-exercised audio through CSS/SSS and a Mario/Final Destination scenario;
it does not establish full-match performance, original PCM or hardware
fidelity, or licensing clearance.

The operator has authorized a separate production-audio candidate path,
described in [Production audio](AUDIO_PRODUCTION.md). This review update does
defines the release boundary; completed audits and deployments are recorded
with the corresponding release. The GPL audio inventory below
describes the September 12 audit and historical artifacts, and must not be read
as a new license grant for retained numerical data. The separate Dolphin
observer retains its existing GPL notices.

## Operator-approved alpha posture

The operator is **NaiadAI, LLC** and the approved public contact is
**legal@webmelee.gg**. The operator explicitly accepts the unresolved
recovered/decompiled-code risk for the initial alpha, using smash.fun as a
rough product-risk reference. That acceptance is not a legal comparison,
clearance or evidence of another service's rights. Do not re-open the accepted
risk posture as an automatic deployment-approval gate.

The operator prohibits distributing historical GPL-derived audio code without
meeting its obligations. The silent rollback profile has no audio transport or
generated replacement DSP coefficient bytes. The authorized audio candidate uses the
replacement implementations and retains the coefficient table's historical
provenance and GPL notice; its exact producer identity, compile/link closure,
JS graph and final artifact audit must establish the package contents. This
authorization does not resolve the retained-data licensing question, make a
legal-clearance claim or change repository visibility.

Forwarding to the intended inbox must be tested and the final candidate audit
must pass before public-domain activation. The approved address alone does not
establish receipt. The existing opcode-63 CPU-action abort remains an explicit
known limitation; no wider stability or full-match claim follows from this
release posture.

## Candidate and scope

This review covers the current `web/player` loader, the silent Release
`runtime-public` target, the authorized `runtime-audio-preview` Release target
and the legal pages in `web/public/`. The final artifact identity, runtime hash
and deployed URL must come from the selected packager's fresh manifest. A
development runtime, diagnostics page and evidence server are outside either
public player graph.

The silent `player` profile serves the reviewed legal pages plus a compiled
Emscripten JavaScript/Wasm/data runtime and the small JavaScript modules needed
for local disc reads and input; public audio output is disabled. The separate
`audio-player` profile adds the reviewed browser audio transport and replacement
audio modules, with the exact inventory bound to
`build/runtime-audio-preview-identity.json` and
`melee-web-audio-player-package-v1`. Neither profile serves a retail disc image,
extracted game archive, standalone game asset or repository source checkout.
The compiled programs are nevertheless built from recovered Melee/HSD and
original platform source, so excluding source files and retail assets from the
upload does not resolve the rights in the executable.

The legal templates retain build placeholders. Production packaging substitutes
NaiadAI, LLC and legal@webmelee.gg, as expressly supplied by the operator. The
mail route passed actual receipt tests before the nameserver change and after
DNS migration, before domain activation. The exact alpha is now live at
[webmelee.gg](https://webmelee.gg/); its scoped artifact, browser, DNS, HTTPS and
redirect evidence is recorded in [PUBLIC_ALPHA_VALIDATION.md](PUBLIC_ALPHA_VALIDATION.md).

## Artifact and seed facts

The Release target preloads `web/initial_pipeline_cache.db.gz.b64` as
`/initial_pipeline_cache.db`. Decoding the checked-in source produces a
2,621,440-byte SQLite database with SHA-256
`4bdb7c4a3e906d907d066f0c65041d7eb3472dc25c9d8c98be6e4946fdce560f`. Its
schema has one `aurora_schema` row, one shader row and 627 pipeline rows. The
current release preserves all 626 previous records, including the 78 added
for Link/Young Link, and appends two portable configurations recovered from the
failed Marth/Battlefield holdout's saved pipeline DB/WAL. The
[current holdout evidence](CURRENT_RUNTIME_HOLDOUTS_20260919.md) records that
failure, exact descriptor identities and preservation checks. The
[Link descriptor ledger](evidence/link-gpu-compilation-v1.json) and
[Roy/Doc ledger](evidence/roy-doc-public-preparation-v1.json) remain historical
evidence. No raw draw provenance, IndexedDB/profile pages, or Dawn driver cache
is shipped. The deployed release is identified by the
[deployment receipt](evidence/public-marth-pipeline-release-v1.json).

The seed contains binary renderer configuration blobs and hashes rather than
file names. Inspection found no raw texture, model, audio or disc bytes. That
observation is a content fact, not a rights conclusion: the seed is generated
metadata tied to the game renderer and must be included in the artifact
manifest, SBOM and rights review. Do not describe it as unrelated to the game.

The public loader does not load `runtime-cache.js`, mount IDBFS or call a cache
persistence hook. Aurora's `/melee-render-cache` path is page-local in this
profile. The native code and linker still include cache support for renderer
operation, and browsers or graphics drivers may maintain their own caches. A
future change that mounts IDBFS or distributes cache files requires a new
privacy and notice review.

## Local processing and network behavior

| Surface | Current public player | Boundary or verification limit |
| --- | --- | --- |
| Disc selection | Browser file input accepts `.iso`, `.gcm` and `.ciso`; the player asks for an unmodified USA 1.02 copy | RVZ is rejected; the implementation reads bounded `Blob.slice()` ranges and validates the GameCube image, FST and DOL SHA-1 `08e0bf20134dfcb260699671004527b2d6bb1a45`; this does not establish rights in the selected copy or prove every archive byte is original |
| Selected data | The local session reads a complete menu or selected-match scope; source owners and decoded caches close before outgoing file bytes retire | No retail data is included by the site; the scene-loading checkpoint keeps functional file retirement separate from Wasm capacity and total browser memory |
| Filename and hash | The browser file name and local DOL hash are used for local validation/status | The reviewed source path has no code that sends those values to an application endpoint; retain request-trace evidence for the deployed build |
| Application network | Same-origin requests load the player modules and compiled runtime; no application account, analytics or upload path is present in the reviewed graph | Cloudflare and browser networking remain outside application code; verify methods, URLs, bodies, WebSocket and beacon activity on the deployed artifact |
| Preference storage | Keyboard layout and enablement choices use localStorage key `melee-prototype-keyboard-v1` | Storage may be unavailable; no preference should be described as server-side |
| Renderer storage | Native Aurora cache files use the page's in-memory filesystem; public JS does not mount IDBFS | Browser HTTP cache, history and graphics-driver caches are uncontrolled and are separate from application memory |
| Eject/reload | Eject unloads native state, calls `player.destroy()` and reloads the document | The full reload retires the page/Wasm/native heap, imported archive bytes, decoded assets; it does not clear keyboard preferences or browser/driver caches |
| Diagnostics | Development `runtime.html`, evidence routes and cache export controls are not in the player graph | The public packager rejects diagnostic/evidence modules; verify the runtime identity and output manifest |

The selected file and derived data are processed locally in the reviewed source
path. This is not a promise about host-level crash collection, browser logging,
extensions or provider processing. The public player has no user-content storage
area and no account or online-match service.

## Copyright, source licensing and circumvention

[17 USC 106](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section106&num=0&edition=prelim)
reserves reproduction, adaptation, distribution and other rights. Keeping a
retail copy on the visitor's device reduces server distribution of that copy,
but does not resolve rights in recovered game code, original platform code,
compiled adaptations or generated renderer metadata shipped by the operator.
The pinned `doldecomp/melee` root has no identified repository-wide license.
Public availability of a repository is not a license.

[17 USC 107](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section107&num=0&edition=prelim)
requires a fact-specific fair-use analysis. A fan-project label, noncommercial
intent or Nintendo non-affiliation statement does not decide that analysis.
[Section 117](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section117&num=0&edition=prelim)
addresses limited essential-step and archival copying by owners of program
copies; it is not a general permission to distribute a game or compiled port.

[17 USC 1201](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section1201&num=0&edition=prelim)
raises a separate access-control and circumvention inquiry. The player asks the
visitor to select a copy, provides no disc image, and does not publish
circumvention instructions. Counsel must evaluate the actual dumping/access
workflow, local law and any claimed exception.

The compiled source/dependency inventory includes these rights boundaries. The
GPL audio row describes historical implementations and retained data; the
authorized audio package preserves that provenance notice while using the
replacement implementation:

- Recovered Melee/HSD code at commit
  `b43912cc78606f96c9569f5d6229bc9d7e265ea5` has no identified root license.
- Original platform AX/AXFX units under Melee's `extern/dolphin` path have no
  per-file license header identified. The directory name does not make them
  Dolphin Emulator code or GPL code; their provenance and rights remain open.
- The historical `src/gameplay_audio_resample.c/.h` and free DSP coefficient
  generator retain GPL-2.0-or-later provenance from Dolphin revision
  `a2efdf1197be8132674b90fe9cf4761df39752ed`. The silent public alpha excludes
  those historical implementations and generated coefficient bytes. The
  authorized audio package uses replacement implementations, but retains the
  coefficient table's numerical provenance and the historical GPL notice in
  [`docs/licenses/dolphin-gpl-2.0-or-later.txt`](licenses/dolphin-gpl-2.0-or-later.txt).
  This is provenance disclosure, not a legal-clearance conclusion. GPL
  compliance does not resolve rights in recovered Melee or platform code.
- Aurora at commit `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca` is MIT. The
  B0XX-derived keyboard mapping retains its MIT notice at
  [`licenses/b0xx-ahk.txt`](../licenses/b0xx-ahk.txt); both are included in the
  runtime aggregate.
- The Emscripten 6.0.9 toolchain and `emdawnwebgpu` port carry MIT/NCSA and
  package-specific notices. Aurora's active graph also includes SDL, Abseil,
  fmt, xxHash, zlib-ng, libpng, FreeType, Dear ImGui, SQLite and Tracy. The
  verbatim texts copied from the actual pinned dependency trees, their source
  paths and the known local adaptations are collected in
  [`docs/licenses/runtime-third-party.txt`](licenses/runtime-third-party.txt),
  with the inventory and unresolved source correspondence recorded in
  [`docs/licenses/PLAYABLE_RUNTIME_NOTICES.md`](licenses/PLAYABLE_RUNTIME_NOTICES.md).

The aggregate is configured notice delivery and intentionally errs on inclusion;
it is not an exact post-link SBOM. It does not establish that the combined
executable may be distributed, provide corresponding source for GPL-covered
adaptations, or resolve rights in recovered code or the generated pipeline seed.

## GPL corresponding-source delivery when covered code is distributed

This applies to artifacts that include GPL-covered code, not automatically to
every future audio implementation or to the excluded sources in the silent alpha. Any failure to establish their
exclusion blocks the alpha artifact.


At the September 12 audit, the worktree contained project source, Dolphin-derived source
identifiers and reproducibility scripts, but it does not configure a public
corresponding-source route, a downloadable source bundle tied to a player
release, or a written GPL offer. No such commitment should be implied by this
review.

Before distributing a player that contains GPL-covered code, the operator and
counsel must choose and verify a compliant path. One possible path is a
release-specific source bundle or source URL containing the GPL-covered source,
the project changes, the build scripts and the exact dependency/source
correspondence needed to modify the distributed work, together with the GPL
text. Another possible path is a valid written offer under the applicable GPL
version and distribution terms. The repository currently implements neither
path as a public service. Publishing recovered Melee or original SDK source can
raise separate rights issues, so a source bundle cannot be added by assumption.

## Naming and marks

`WebMelee`, `webmelee.gg` and `Melee` may function as source identifiers for
game software. Adding “Web” or `.gg` does not establish distinctiveness or
eliminate confusion. Nintendo, GameCube and Super Smash Bros. Melee are
references to their respective rights holders and the intended game/platform;
the site uses no Nintendo logos or promotional assets in the reviewed public
artifact.

[USPTO likelihood-of-confusion guidance](https://www.uspto.gov/trademarks/search/likelihood-confusion)
considers resemblance and related goods/services. The
[USPTO trademark process](https://www.uspto.gov/trademarks/basics/trademark-process)
explains that domain registration does not confer trademark rights. Counsel
should conduct live federal, state, common-law, software-marketplace and
domain-dispute searches for the names and variants. No comprehensive clearance
was performed. [15 USC 1125](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title15-section1125&num=0&edition=prelim)
also addresses false designation and bad-faith domain registration/use. No
bad-faith or liability conclusion is drawn here.

## Copyright contact and DMCA distinction

The player serves operator-selected static files and a compiled runtime. A
visitor's locally selected disc is processed in the visitor's browser and is
not stored on the site's server. The reviewed service has no hosted user-content
area. [17 USC 512(c)](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section512&num=0&edition=prelim)
addresses storage at a user's direction; whether any statutory safe harbor
applies to a particular service or claim is not decided here.

The public contact page is a general reporting channel. It does not represent
that a designated agent has been registered with the Copyright Office, that
safe-harbor eligibility exists, or that every statutory notice/counter-notice
workflow applies. Before hosting replays, screenshots, custom fighters or any
other user content, counsel must revisit agent registration, public agent
information, compliant notices and counter-notices, repeat-infringer policy and
operational response. See the [Copyright Office service-provider guidance](https://www.copyright.gov/onlinesp/),
[agent FAQ](https://www.copyright.gov/dmca-directory/faq.html) and
[37 CFR 201.38](https://www.copyright.gov/title37/201/37cfr201-38.html).

## Product references inspected directly

The public HTTP site [smash.fun](https://smash.fun/) was reachable during the
research check and presented a fan browser project. Its delivered client and
visible routes contained ROM-validation and account/fighter API references;
obvious `/privacy`, `/terms`, `/dmca`, `/copyright` and `/legal` paths returned
404 at that check. This limited route inspection does not establish its full
service model and no wording was copied.

[Slippi Terms](https://slippi.gg/tos) and [Slippi Privacy](https://slippi.gg/privacy)
were public React routes with December 7, 2022 update dates. They describe a
different account/network service, operator, rights reporting and data
processing model. They were structural references only and are not evidence of
WebMelee clearance or completeness.

## Release evidence and remaining legal questions

1. Operator identity and contact spelling are supplied. Incoming delivery passed
   before cutover and after DNS migration; retain the private receipts.
   Address, consumer, privacy or jurisdiction
   questions remain review topics, not invented operator facts.
2. The exact public manifest, native identity and compile/link exclusions passed
   audit. Final apex resources match the manifest; development runtime,
   evidence and diagnostic paths return 404. Future candidates need new audits.
3. The operator accepts unresolved recovered Melee/HSD and original SDK
   distribution risk for the alpha. No permission is inferred from a DOL hash,
   local disc processing or an open repository.
4. GPL audio implementation exclusion is verified in the exact silent alpha. The
   authorized audio candidate uses replacement implementations and retains the
   historical coefficient provenance and notice; its own package, hosted HTTP,
   browser and deployment receipts must be retained before treating production
   audio as deployed. No legal-clearance conclusion follows from those receipts.
5. Retain required notices and check the actual native input inventory against
   the selected licenses for Aurora, Emscripten/Dawn and active dependencies.
   [`docs/licenses/runtime-third-party.txt`](licenses/runtime-third-party.txt)
   provides the configured notice texts. A complete post-link SBOM remains a
   follow-up; permissive licenses are not described as requiring GPL-style
   corresponding-source publication.
6. Decide whether the preloaded 628-record Aurora seed can be distributed and
   retain its exact provenance, hash and notice treatment.
7. Have counsel review the disc-access workflow, access controls and applicable
   Section 1201/local-law questions.
8. Complete trademark/domain searches for WebMelee, webmelee.gg, Melee and
   related references before treating the name as cleared.
9. The final apex browser/network checks passed after disabling RUM injection,
   email rewriting and active NEL response policies. Hosting-provider terms and
   operational retention assumptions remain distinct from application behavior.
   Revisit this review before any account, online match,
   analytics or hosted user-content feature.
10. The production audio path is authorized but has no new release receipt in
    this review. Keep the previously verified silent deployment available as
    rollback until the exact audio package and same-byte promotion are recorded.

The supplied operator facts and accepted alpha risk posture are recorded above.
Artifact verification and tested mail forwarding passed before activation and
remain requirements for future changes. This assessment does not represent
the remaining legal questions as resolved or require a new risk waiver already
supplied by the operator.
