# Publication provenance assessment: D1–D4

Assessment date: 2026-09-20. Reviewed tree: `7930221343adab157a2283adc76fab2aec0407a2`
(`codex/public-repository-readiness`). This is an engineering provenance and
publication-risk assessment. It is not legal clearance, an ownership opinion,
or a grant of permission from Nintendo, doldecomp, Dolphin contributors,
Aurora contributors, Andrew Church, SDL, libusb, or any other party.

The project owner has accepted the documented publication risk and declined
external contributor or counsel outreach. That choice removes outreach as a
workflow gate for this repository decision. It does not turn risk acceptance
into ownership, permission, a public-domain dedication, or a license. The
operator should publish the boundaries below as facts and avoid describing the
combined game, recovered source, renderer data, or current audio as cleared.

## Delta note: source and observer additions through September 25, 2026

This note supplements the historical September 20 assessment; it does not
change that assessment's reviewed tree or convert its risk treatment into
clearance. The subsequent source delta was reviewed through main commit
`60372b468dbe98f2b73bcc690a4ed35273eead91`, from the bounded provenance base
`836a11f90c7259e2b8348deaaa304b89d21247c4`.

The delta adds a downstream allocation observer and two overlays for Dolphin
commit `c77bbaa0f372c3f72281602a8b087206706542cb`:

| Added path family | Delta treatment |
| --- | --- |
| `reference-capture/dolphin/source/Core/PowerPC/ReferenceAllocationObserver.cpp`, `.h`, `ReferenceAllocationProfile.h` | Keep the observer under its GPL-2.0-or-later reference boundary. The profile is generated identity metadata and hashes only; it does not embed original executable bodies or grant rights in its DOL/source inputs. |
| `reference-capture/dolphin/patches/0003-allocation-observer.patch`, `0004-allocation-followed-returns.patch` | Preserve as downstream Dolphin overlays with the pinned commit, patch hashes and corresponding-source manifest. |
| `patches/melee-source-ar-init-cxx.patch`, `source-ai-callback-stack.patch`, `source-ax-startup-alignment.patch`, `source-ax-startup-services.patch` | Preserve as downstream transforms against pinned Melee revision `b43912cc78606f96c9569f5d6229bc9d7e265ea5` and its original SDK/platform units. Patch authorship does not relicense recovered context. |
| `src/source_*`, `tests/source_*_oracle_include/**`, source oracle programs, and allocation/source-startup scripts and tools | Treat as mixed project orchestration, source-bound adapters, source-derived declarations and generated observations. Their fixture scope does not support a directory-wide originality or MIT assertion. |

The delta adds no tracked disc, DOL, SRAM, audio, extracted asset or game
executable. Existing D1–D4 dispositions therefore remain in force: no new
rights conclusion, ownership assertion or blanket license is made for the
source-derived fixtures, generated identity data, current audio or recovered
Melee/SDK material.

## Disposition

| Decision | Defensible publication treatment at this review | Status and remaining risk |
| --- | --- | --- |
| D1 — new project work | Apply the owner’s scoped MIT decision only to an explicit allowlist. The initial five reviewed paths are `scripts/browser_tools.mjs`, `scripts/serve.py`, `scripts/ci_verify.py`, `scripts/ci_aggregate.py`, and `scripts/ci_report.py`. The root publication guard/history work may be added only through the same exact-path scope record. | Proceed when the root scope file and MIT text identify every covered path. Git history for the five initial paths is attributable to Eric Van Lare identities; that supports provenance, but authorship history alone is not an assignment or third-party-rights finding. Do not grant `src/`, `web/`, `tests/`, the combined executable, recovered code, current audio, or generated data by directory pattern. |
| D2 — recovered Melee/SDK source | Publish the recovered-source boundary only as an explicitly risk-accepted inclusion. Preserve the pinned source identity, downstream patch identity, upstream notices, and the statement that no repository-wide Melee license was located. | The owner’s acceptance supports proceeding with the repository decision; it does not establish permission. The pinned tree has tool-specific licenses but no root `LICENSE`/`COPYING` identified in its recursive tree. Keep recovered Melee/HSD, MSL, and `extern/dolphin` platform units outside any project MIT grant. |
| D3 — current coefficient/replacement and historical audio | Keep the current replacement implementation and generated behavior for engineering continuity. Do not put current audio files or retained coefficient data in the initial MIT scope. Keep the historical GPL source, notices, and provenance available, and describe the current audio-enabled package as carrying unresolved provenance risk. | The current C/JS implementations are materially rewritten, but the actual process included prior-source inspection by the specification author and is not a formal clean-room record. Twenty compatibility words and the selected filter parameters remain deliberately retained from Dolphin’s replacement DROM. Same-output tests establish regression behavior, not licensing independence or original-hardware fidelity. |
| D4 — generated/reference material | Keep reproducible source and non-sensitive metadata reviewable with explicit generated-data labels. Retain the Andrew Church attribution and the separate native-probe license texts added under `docs/licenses/`. Treat the pipeline seed as generated renderer metadata tied to the game path. | The `fres` provenance is strengthened by the primary source’s “No copyright is claimed” notice. The native builder still does not validate the libusb source checkout against its pinned revision. The checked-in pipeline-preparation header is bound to an older seed than the current checked-in seed; regenerate from certified inputs before enabling that path or label it as stale development metadata. |

## D1: scoped project license

The five initial candidates were read as complete current implementations. The
current Git blobs are:

| Path | Git blob at the reviewed tree |
| --- | --- |
| `scripts/browser_tools.mjs` | `bc7ad0dbc57bc550bd97d1e3b2ed29918fd0d9d9` |
| `scripts/serve.py` | `6330486e6ebbc2e406e54bc73fcf84a5cfa1de1b` |
| `scripts/ci_verify.py` | `e0cbb85f30888f1fa5794e506e56630b98eb9860` |
| `scripts/ci_aggregate.py` | `14126f0209538d634de721a1ff0d5095d3b646f6` |
| `scripts/ci_report.py` | `2529a05b98bddcd69e587ad1b09047c9d87813b2` |

`git shortlog` for those paths shows the repository owner’s Git identities;
no separate contributor identity was found in that narrow history. This is useful evidence for the
owner’s scope decision, but it is not a rights assignment and does not cover
the rest of the repository. The root should put the standard MIT text and an
exact path allowlist in the publication scope document, while retaining the
upstream notices listed by [THIRD_PARTY.md](../THIRD_PARTY.md).

The [U.S. Copyright Office computer-program circular](https://www.copyright.gov/circs/circ61.pdf)
distinguishes new authorship from preexisting, public-domain, and third-party
source material. That distinction supports the path-level scope here; it does
not decide the status of any individual recovered or translated routine.

## D2: recovered Melee and platform source

The selected recovered source is pinned to
[`doldecomp/melee` commit `b43912cc78606f96c9569f5d6229bc9d7e265ea5`](https://github.com/doldecomp/melee/tree/b43912cc78606f96c9569f5d6229bc9d7e265ea5).
The repository’s [recursive tree API response](https://api.github.com/repos/doldecomp/melee/git/trees/b43912cc78606f96c9569f5d6229bc9d7e265ea5?recursive=1)
contains licenses for selected tools but no root `LICENSE`, `COPYING`, or
repository-wide grant. A representative compiled platform file is
[`extern/dolphin/src/dolphin/os/OSAlloc.c`](https://github.com/doldecomp/melee/blob/b43912cc78606f96c9569f5d6229bc9d7e265ea5/extern/dolphin/src/dolphin/os/OSAlloc.c);
the directory name does not make it part of the separate Dolphin Emulator
project or supply a license for the recovered GameCube SDK code.

The tracked patch and source-preparation boundary remains:

| Item | Current repository identity | Treatment |
| --- | --- | --- |
| `patches/melee-gameplay.patch` | Git blob `b5ad1725a9e6c3fb1d0f907436aa129ed5f2cb90`; SHA-256 `c272d6fa344e74c6bfa81e0cb28a69aa5fbce1e6637879e56f635e6a0ef8587c` in the inventory receipt | Preserve the target commit and patch context. A downstream patch does not relicense its recovered context. |
| `scripts/gameplay_sources.py` | Git blob `440b72ffadbea3dee5095dc8a485bfcf885bca1d`; SHA-256 `78ee8e9dabbbce27e104f7be76f46bd967d406998366d7e5f635b5593730fd7e` | Project orchestration can be reviewed separately; it does not grant rights in generated recovered source. |
| `cmake/FighterRuntime.cmake` | Git blob `c244a52c885474c1138288f625590e4aaa8f8e6d`; SHA-256 `03783dd6a6b76f42be39c4d65a4baeb3dab0d6aa7f86e3df1774537e61d365ca` | Retain the explicit source list and unresolved Melee/SDK boundary. |

The defensible wording is that the owner has chosen to publish a repository
that contains recovered source and accepts the associated unresolved rights
risk. It is not that the source is authorized by its public availability, that
the operator owns it, or that a root MIT license covers it. Disc images,
extracted assets, audio, textures, and game executables remain outside Git as
the repository states.

## D3: current audio and GPL history

The current files at this tree are:

| Path | Git blob | SHA-256 |
| --- | --- | --- |
| `src/gameplay_audio_resample.c` | `bcad9a6fa582e390c7065767510af7c763d0ff64` | `7114bb1e0edc4fa3d6ecfdf1eb646070eff4655682d8f9bec3bbd8250245a2b3` |
| `src/gameplay_audio_resample.h` | `a5848e8c1f55a31e84fc673d3cc7636e9f441fc7` | `c44691cfdb5dda8931ae55749f75edd32403078d261f0a4f9d58c55a50126fcb` |
| `web/dsp-coefficients.mjs` | `a00f246da935fb883e85d32bbfc59f273807f4e6` | `13cbc7d48e7e26f3093a4f6e4f0d3763b56288668ddf54b523b283b6b44f86d1` |

The replacement was introduced by
[`ce42469f86197095c3d89edaf9bc08bb7784e05f`](https://github.com/ericvanlare/melee-web/commit/ce42469f86197095c3d89edaf9bc08bb7784e05f)
and is documented in [AUDIO_REPLACEMENT_EVIDENCE.md](AUDIO_REPLACEMENT_EVIDENCE.md).
The old versions remain reachable in Git and carry GPL-2.0-or-later notices;
for example, the pre-replacement tree at
[`e519c2fa4416c1ff8dba4e5467e65a2927b5ad29`](https://github.com/ericvanlare/melee-web/tree/e519c2fa4416c1ff8dba4e5467e65a2927b5ad29)
contains the old GPL adapter and generator.

The current generator is a new JavaScript expression of standard window and
Bessel formulas, but it intentionally applies twenty compatibility words. The
pinned Dolphin generator
[`generate_coefs.py`](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/docs/DSP/free_dsp_rom/generate_coefs.py)
shows the same filter construction and compatibility entries. Dolphin’s
pinned [`COPYING`](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/COPYING)
says most original Dolphin source is GPLv2+ and that per-file terms control;
the generator itself has no file-level grant. The pinned replacement history
describes the coefficients as an approximate replacement, not Nintendo’s DROM;
that description does not itself grant reuse rights.

The recommendation is therefore to preserve current behavior, keep the
historical GPL text at [`docs/licenses/dolphin-gpl-2.0-or-later.txt`](licenses/dolphin-gpl-2.0-or-later.txt),
and exclude current audio files and coefficient data from the initial MIT
scope. Do not claim that a rewritten implementation is automatically
independent, that numerical compatibility values are public domain, or that
client-side generation removes distribution questions. If an audio-enabled
artifact is published, bind its notices and any corresponding-source route to
the exact release manifest; the existing package inventory is not itself a
legal conclusion.

The [Copyright Office methods circular](https://www.copyright.gov/circs/circ33.pdf)
supports separating standard methods from their expressive implementation,
but it does not decide whether this selected table, its arrangement, or its
compatibility overlays are protected in every jurisdiction. Differential tests
and the `d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179`
output hash establish behavior and identity only.

## D4: reciprocal estimate, native probe, and renderer metadata

### Andrew Church reciprocal estimate

The current [fres header](../src/gameplay_fres.h) is Git blob
`321b87576902e5b1155f924ed8955562c0be12ea` and reproduces the 32 base/delta
entries and integer steps from `calc_fres`. The primary source is
[`https://achurch.org/cpu-tests/ppc750cl.s`](https://achurch.org/cpu-tests/ppc750cl.s),
retrieved for this review at SHA-256
`9d15ef92ca0470a99bac72661f74a6f12e035bcd96a85819c761b5852ad097be`.
Its first lines identify Andrew Church as author and state: “No copyright is
claimed on this file.” The source contains the `calc_fres` implementation and
the same table at the labeled software implementation. This is strong primary
provenance for the copied table and steps and is sufficient to correct the
earlier “retrieval returned 403” limitation in the first-pass inventory.

The header should retain the attribution and URL. The source statement is not
a separate formal license text, so no invented CC0 or MIT notice should be
added. Public-domain or no-claimed-copyright provenance for this source does
not grant rights in surrounding Melee code or in the project’s implementation.

### Native controller probe

The probe is a separate reference utility. Its source files are
`reference-capture/controller-probe/CMakeLists.txt` (blob
`39b7f6e57598b555f35326b0ff23f66f203cb49b`), `README.md` (blob
`9b04367243a1b0f8a69d7efeefeb9e43c90e8e78`), and
`scripts/build_reference_controller_probe.py` (blob
`d0b00a18a44c12fd6ba9d24937966a5bd5cef099`). The build consumes static SDL and
libusb archives from the pinned Dolphin reference workspace; it is separate
from the browser’s SDL 3.4.10 graph.

The exact upstream notices are now present under `docs/licenses/`:

| Dependency | Pin and primary notice | Tracked text |
| --- | --- | --- |
| SDL | SDL commit `5848e584a1b606de26e3dbd1c7e4ecbc34f807a6`; [upstream `LICENSE.txt`](https://github.com/libsdl-org/SDL/blob/5848e584a1b606de26e3dbd1c7e4ecbc34f807a6/LICENSE.txt) | [`controller-probe-sdl-zlib.txt`](licenses/controller-probe-sdl-zlib.txt), upstream fetched bytes SHA-256 `1c040b8271b37e5076359f8fd54240e371114112924d2df81ef87c7d6a1dfdfd` |
| libusb | libusb commit `15a7ebb4d426c5ce196684347d2b7cafad862626`; [upstream `COPYING`](https://github.com/libusb/libusb/blob/15a7ebb4d426c5ce196684347d2b7cafad862626/COPYING) and [pinned source header](https://github.com/libusb/libusb/blob/15a7ebb4d426c5ce196684347d2b7cafad862626/libusb/core.c) | [`controller-probe-libusb-lgpl-2.1-or-later.txt`](licenses/controller-probe-libusb-lgpl-2.1-or-later.txt), upstream fetched bytes SHA-256 `5df07007198989c622f5d41de8d703e7bef3d0e79d62e24332ee739a452af62a` |

The builder checks the SDL checkout is clean and at the expected commit. It
checks the expected Dolphin revision, hashes the libusb archive, and records
the libusb source path, but it does not check the libusb checkout’s Git commit
or clean state. The text files close the repository notice gap; they do not
prove that a future native binary has the matching libusb source, relink
materials, or a compliant release-specific source offer. Before distributing
that binary, either bind and verify the libusb source checkout in the builder
manifest or keep the probe as a local development tool.

### Generated Aurora renderer metadata

The current `web/initial_pipeline_cache.db.gz.b64` is Git blob
`4a608a7ac9f02644d232ee6af3c2b7ce7046cc93`. Materializing it with
`scripts/materialize_pipeline_cache.py` yields a SQLite database of 3,518,464
bytes with SHA-256
`8df6a998cef19b88a3eff0ee61f666e42791f5f73841818961aa3b224cad2c4b`.
Inspection found one Aurora schema row and 846 `pipeline_cache` rows: one
type-0 64-byte descriptor and 845 type-1 2,772-byte descriptors. The blobs
contain renderer configuration and hashes; no raw texture, model, audio, or
disc bytes were found in this inspection. Those facts describe content, not
rights. The seed was observed while rendering game routes and remains
game-derived renderer metadata for publication review. Aurora’s MIT license at
the pinned [Aurora commit](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca/LICENSE)
does not by itself license observed game-derived descriptor content.

The tracked `src/pipeline_preparation.generated.hpp` (blob
`9429267d28a299f9d226be2dd06c94db853cd3e2`) embeds seed digest
`cdf157ee0f1850884f07a71165fd2192177acb23c2c777313b719e70ea67546f` and
binding digest
`0ad391b8c9c929779abc228360dfbe47616b2c202ab75b17c47e5ccd838b31d4`, while
the current materializer requires seed digest
`8df6a998cef19b88a3eff0ee61f666e42791f5f73841818961aa3b224cad2c4b`. This is
a stale generated binding, not evidence of a rights grant. If selective
pipeline preparation is enabled, regenerate the header from certified private
inputs against the current seed and retain the resulting receipt. If it is not
enabled, describe the header as development metadata and do not present it as
the current release’s pipeline certificate.

## Compiler-cache follow-up

The [targeted compiler-cache review](COMPILER_CACHE_REVIEW.md) retains ordinary
CI caching after one-time private-era cache cleanup. These are intermediate
objects from the mixed project/dependency compile graph. Their recovered-source
and current-audio provenance remains within the documented uncertainty; neither
public cache access nor the root license supplies additional rights. There is
no identified cache-specific basis for a permanent blanket caching prohibition.
Retain upstream notices and exact source provenance. This operational decision
does not clear a combined player or authorize a new release.

## Publication wording and remaining actions

The following statements are supported by this assessment:

* The repository contains a narrow owner-selected MIT scope for identified
  project work. The scope does not cover recovered game/SDK source, current
  audio, retained coefficient data, or generated renderer metadata.
* The owner has accepted the unresolved publication risk for recovered source
  and generated game-derived material. That is an operator decision, not a
  claim of ownership, permission, or Nintendo authorization.
* Historical Dolphin-derived audio source remains identifiable GPL history.
  Current audio replacements retain known provenance and are not granted MIT
  by this assessment.
* The `fres` table has a primary source notice stating that no copyright is
  claimed on the source file. The surrounding project code retains its own
  separate provenance boundary.
* The browser SDL notice set and the native controller-probe SDL/libusb notice
  set are separate dependency closures.

Before a concrete public release, root integration should:

1. Make the exact MIT allowlist and exclusion language operative in the root
   publication scope, with no directory-wide grant.
2. Preserve the D2 recovered-source warning and the owner-risk decision in the
   repository publication record.
3. Keep current audio outside the initial MIT scope and bind any audio-enabled
   artifact’s notices/source-delivery path to its actual manifest.
4. Extend the native probe manifest check to validate the libusb source commit,
   or do not distribute its static binary.
5. Regenerate or explicitly quarantine the stale pipeline-preparation header;
   audit the current seed as generated game-derived metadata in the release
   manifest/SBOM.

The full first-pass inventory, audio review, runtime notice inventory, and
repository-content controls remain the detailed supporting records:
[SOURCE_LICENSE_INVENTORY.md](SOURCE_LICENSE_INVENTORY.md),
[AUDIO_COEFFICIENT_REVIEW.md](AUDIO_COEFFICIENT_REVIEW.md),
[PLAYABLE_RUNTIME_NOTICES.md](licenses/PLAYABLE_RUNTIME_NOTICES.md), and
[PUBLIC_REPOSITORY_CHECKLIST.md](PUBLIC_REPOSITORY_CHECKLIST.md).
