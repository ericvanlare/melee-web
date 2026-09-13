# WebMelee shell release review

Research date: September 12–13, 2026. Prepared for operator and qualified counsel
review. This is a technical inventory and issue assessment, not a legal opinion,
trademark clearance or assurance of legal protection.

## Candidate and scope

The deployment branch is stacked on PR #10, pushed commit `6d5ce786d4dfc3ad383c1e6b4002ff1b63afad22`.
PR #10 targets `port/fighter-runtime`; PR #11 targets `main`. Neither is merged,
rebased or rewritten by this launch. PR #11's newer runtime commits and the CPU
track's uncommitted changes are not inputs. Their descriptions/checks report
unresolved CI, source validation and public gameplay gates. The launch does not
close issues #2, #4 or #5 or supersede the accuracy contract.

**Only the public shell is a distribution candidate.** Its exact input inventory
is six original HTML pages, one original stylesheet and one original fullscreen
script under `web/public/`. It has no runtime dependency, iframe, native code,
Wasm, disc parser, file input, account, upload, backend or game.

The source templates for Terms, Privacy, Copyright/contact and Notices are complete
except operator identity and a working rights/privacy email. Those facts must be
provided by the operator, not inferred from registrar details or Git metadata.
Do not publish an invented `legal@webmelee.gg` address. No postal address, business
entity or choice of jurisdiction has been invented. Counsel should determine
whether additional identity/address disclosures are required for the operator's
actual location, audience and service. Draft preview placeholders are not final
operator contact details.

## Artifact inventory and technical enforcement

`scripts/build_public.py` reads an explicit source allowlist and generates a fresh
output. It never copies `web/`, `build/`, test data, upstream sources or local work.
CSS/JS filenames contain content hashes. `scripts/audit_public.py` binds the complete
output to an external SHA-256/size/path manifest, rejects unknown files, unsafe
paths/symlinks, prohibited formats and suspect content. The external manifest is
intentionally outside the upload directory, so every uploaded byte can be hashed
without a self-hash exception. Cloudflare consumes `_headers` and `_redirects` as
configuration rather than serving them as assets.

No game executable, recovered source, original SDK code, disc image, extracted
archive, sound, font, icon, screenshot, recording, savestate, memory card, replay,
trace, debugger artifact, test report, pipeline cache or compiled game binary is
approved for this build. Binary formats including Wasm are rejected altogether.
The complete output is inspectable UTF-8 text. Original CSS has no image/font URLs;
HTML uses only an empty data favicon. System fonts are requested from the device,
not redistributed. There are no shipped npm dependencies. Build/test/deployment
tools remain outside the upload tree.

A signature scanner cannot establish copyright ownership or detect every encoded
payload. The defense is the small reviewed source inventory, exact regenerated
bytes and manifest equality, plus format/content rejection and browser/network
verification. A future allowlist expansion requires renewed rights and technical
review; passing this shell's scan does not authorize a game executable.

## Local processing and privacy claims

| Surface | This public shell | Development player / future integration |
| --- | --- | --- |
| Disc selection | Unavailable; no file input or reader | `DiscImage` reads bounded `Blob.slice()` ranges |
| Game data | None supplied, read or derived | Disc DOL/FST validation, archive extraction, original font extraction and native in-memory transfers |
| Network | Initial public page/CSS/JS GETs; application connection APIs blocked by CSP | Development host includes explicit evidence POST controls; cannot be described as having no upload capability |
| Filenames and hashes | No access to either | Disc/DOL validation and asset names exist locally; review error and evidence paths before publication |
| localStorage | Unused | Prototype stores keyboard preferences |
| IndexedDB | Unused | Optional IDBFS renderer cache; provenance of cached shader/pipeline content needs review |
| Cache Storage / service worker | Unused | Reaudit actual integrated loader/module graph |
| HTTP cache/history | Public shell resources/URLs may remain | Distinct from application storage |
| Hosting | Cloudflare receives IP, URLs and request headers | Provider processing is separate from application telemetry |

The truthful current statement is: **this shell cannot select or read a disc and
has no upload capability.** Do not advertise a verified public disc import when
none exists. Disc-selection/preparation upload tests are unavailable for this
build because the operation is deliberately absent. Browser checks instead verify
that absence, the entire request inventory, no application storage and blocked
network APIs. Future preparation must be checked against a genuine authorized
local disc, capturing request URLs/methods/bodies plus WebSocket/beacon attempts
across startup, import, preparation, failure, pause, eject and reload. Retain that
sensitive evidence locally; publish only sanitized results. Test cache contents
and reload/clear behavior separately. Avoid claiming that browser or host-level
crash collection can never occur.

## Copyright, source licensing and circumvention

[17 USC 106](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section106&num=0&edition=prelim)
reserves reproduction, adaptation, distribution and other rights. Keeping retail
assets local reduces server distribution but does not resolve rights in recovered
game code compiled into a delivered executable. An open GitHub repository is not
a license. The pinned doldecomp Melee tree has no identified repository-wide
license. Project patches and recovered declarations cannot be presumed original
or independently licensable.

[17 USC 107](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section107&num=0&edition=prelim)
requires a fact-specific four-factor fair-use analysis. A noncommercial label or
disclaimer does not decide it. [Section 117](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section117&num=0&edition=prelim)
addresses limited essential-step/archival copying by owners of program copies;
it is not general permission to distribute a game. The disc acknowledgement
therefore asks for legal authority rather than treating physical ownership as
universal authorization.

[17 USC 1201](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section1201&num=0&edition=prelim)
raises a separate access-control/circumvention inquiry. Counsel must evaluate
actual dumping/access instructions, access controls and any claimed exception.
Conditional research/preservation exceptions are not a general browser-game
release permission. This shell provides neither game files nor circumvention
instructions.

The development runtime contains GPL-2.0-or-later Dolphin adaptations, including
audio resampling and the free DSP-coefficient generator, as well as MIT Aurora
and B0XX-derived components and the Emscripten/toolchain dependency graph. GPL
source and distribution duties must be reviewed for the combined program;
complying with GPL does not resolve Nintendo rights. The public shell excludes
all of these. See `THIRD_PARTY.md` for the development/source boundary.

## Naming and marks

`WebMelee`, `webmelee.gg` and `Melee` may function as source identifiers for game
software. Adding “Web” or `.gg` does not establish distinctiveness or eliminate
confusion. Nintendo, GameCube and Super Smash Bros. Melee are descriptive
references here, without logos or imitated branding. The independent-project
notice is factual context, not permission or an infringement defense by itself.

[USPTO likelihood-of-confusion guidance](https://www.uspto.gov/trademarks/search/likelihood-confusion)
considers resemblance and related goods/services; the
[USPTO trademark process](https://www.uspto.gov/trademarks/basics/trademark-process)
explains that domain registration does not confer trademark rights. Counsel
should conduct live federal/state/common-law, game/software marketplace and
domain-dispute searches for all relevant names and variants. No comprehensive
trademark clearance was performed. Search-index results or an absent exact-word
hit are not a clearance conclusion. [15 USC 1125](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title15-section1125&num=0&edition=prelim)
also addresses false designation and, under subsection (d), bad-faith domain
registration/use. No bad-faith or liability conclusion is drawn here.

## Copyright contact and DMCA agent

This build distributes the operator's static shell and accepts no user content.
[17 USC 512(c)](https://uscode.house.gov/view.xhtml?req=granuleid:USC-prelim-title17-section512&num=0&edition=prelim)
addresses storage at users' direction. There is no identified user-storage
feature requiring a section 512(c) safe-harbor launch workflow here. That is a
service-model observation, not an opinion that section 512 can never apply.
A public copyright contact remains necessary for this launch. Merely listing an
email does not register a designated agent or confer safe-harbor protection for
the operator's own distributed materials.

Before future hosting of replays, screenshots, custom fighters or other user
content, counsel must reassess applicable safe harbors, public/registered agent
information, compliant notices/counter-notices, repeat-infringer policy and
operational response. The Copyright Office provides
[service-provider guidance](https://www.copyright.gov/onlinesp/), the
[agent FAQ](https://www.copyright.gov/dmca-directory/faq.html) and
[37 CFR 201.38](https://www.copyright.gov/title37/201/37cfr201-38.html).
No registration has been submitted and none is represented on the site.

## Product references, inspected directly

The research checked the public HTTP sites and their delivered client code/routes.
[smash.fun](https://smash.fun/) was reachable and described a fan browser project;
its client contained ROM-validation and account/fighter API references. The obvious
`/privacy`, `/terms`, `/dmca`, `/copyright` and `/legal` paths returned 404 at the
check. This limited route inspection does not prove an absence of notices
elsewhere and no claim about actual ROM upload was established.

[Slippi Terms](https://slippi.gg/tos) and [Privacy](https://slippi.gg/privacy) were
public React routes with December 7, 2022 update dates. Their notices address an
operator, account/service conduct, rights reporting, Nintendo non-affiliation
and data processing. Its account/network service differs from this static shell.
These were structural product references only. No wording was copied and their
approach is not evidence of completeness or legal clearance for WebMelee.

## Questions reserved for counsel and operator

1. Who is the actual operator, what public contact receives mail, where does the
   operator operate and which audiences/jurisdictions are intended? Are postal
   address, statutory notices, consumer or privacy disclosures required?
2. Is the name/domain acceptable after trademark clearance? Are the intended
   references and presentation sufficiently clear about origin?
3. What rights basis permits distribution of recovered Melee/source/SDK material,
   patches and a combined compiled executable? Which components must remain
   excluded regardless of local asset processing?
4. What GPL/corresponding-source and transitive attribution obligations apply to
   each future artifact? Is source publication possible under the actual rights?
5. Do proposed disc-access instructions implicate access controls or local law?
6. Are Terms assent, warranty wording and correspondence retention appropriate?
7. Before UGC/network play, what changes to privacy, safety, notices, agent
   registration and actual response procedures are needed?

The shell can proceed only with truthful contact information, its exact artifact
and behavior checks, and explicit limitations. Gameplay remains separately gated.

## Additional source-audit detail

The build also compiles recovered **original platform SDK** AX/AXFX sources under
Melee's `extern/dolphin`; that name must not be confused with Dolphin Emulator.
No per-file license header was identified. This is an additional unresolved
rights boundary. `THIRD_PARTY.md` now records these files and the audited Aurora
external versions/license sources. The future runtime still needs an actual
linked-artifact SBOM and complete license texts, beyond this source inventory.

Development disc validation verifies the DOL, not every selected archive's hash.
Thus it is not proof that all assets match an unmodified retail disc. Native
Unload tears down scene owners but retains the imported file map until a full
page/iframe reset; prototype Eject removes the iframe. Disclosures must distinguish
scene unload from data retirement. The renderer database schema stores config
BLOBs and pipeline hashes; no filename/disc column was found, but that alone is
not a legal assessment of the cached descriptors' provenance. The shell has none
of these storage paths.
