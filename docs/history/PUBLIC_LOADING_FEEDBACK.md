# Public loading feedback and bounded file handoff

The current production renderer fix is documented in
[the promotion record](RENDERER_PRODUCTION_PROMOTION.md). This separate follow-up
is available at the immutable noindex preview
[d605313c.webmelee.pages.dev](https://d605313c.webmelee.pages.dev/).
It has not been deployed to production.

The first HTML now shows “Starting player…” over the canvas. The shared owner
reports graphics preparation, disc reading and game-data preparation separately.
The progress bar uses actual counts where available and stays indeterminate
otherwise. Pending work never rounds up to 100%. Loading feedback retires before
interactive play or on an error; disc selection remains available during catalog
preparation. The owner uses its existing frame notifications and the existing
local pipeline status, with no polling timer or network telemetry.

Disc import previously awaited a separate native command boundary for each file.
It now transfers files in ordered batches, yielding after eight files, 8 MiB or
eight milliseconds. The time budget is checked between files: one indivisible
large or slow native file call can exceed it. Existing allocation/free ownership,
native error propagation, retry and unload behavior remain in place. The direct
single-file development helper is unchanged. This removes avoidable per-file
frame waits as the set of loaded game files grows.

## Bounded evidence

One cold/warm original Mario/FD lifecycle pair completed on Chrome 153.0.8010.36,
macOS 26.6.2 arm64, 1280×960 at DPR 1. “Cold” here means a fresh browser profile;
the operating system and graphics driver caches were not cleared. Both runs
returned to original CSS and passed unload/reload, with zero interactive
pipeline creation, unexpected descriptors, deferred requests or pipeline errors.
This is browser-exercised, scenario-scoped performance evidence, not retail
comparison or wider content admission.

| Measurement | Cold | Warm |
| --- | ---: | ---: |
| Disc import through prepared Play control | 2148.711 ms | 2009.690 ms |
| Renderer bootstrap total | 1805.870 ms | 1675.945 ms |
| Renderer initialization | 59.180 ms | 19.175 ms |
| Catalog preparation until ready | 1746.690 ms | 1656.770 ms |
| Catalog submission, included above | 7.480 ms | 2.075 ms |
| CSS entry elapsed | 140.599 ms | 132.557 ms |
| SSS entry elapsed, including ordinary input recipe | 1294.761 ms | 1308.397 ms |
| Match entry elapsed, including ordinary input recipe | 2328.762 ms | 2320.548 ms |
| Maximum native gameplay callback | 7.335 ms | 8.960 ms |
| Maximum browser gameplay gap | 24.010 ms | 27.305 ms |

Renderer and disc work overlap; their totals must not be added together. The
earlier warm Mario/FD disc result was 2992.772 ms, about one second longer. This
single pair is not a controlled cold-driver comparison and does not prove that
the user's roughly ten-second first visit is resolved. Full 508-descriptor GPU
preparation is unchanged and remains the larger cold-start risk.

Focused owner tests cover batching, ordering, allocation cleanup, errors/retry,
loading-state changes, silent startup and missing cache support. Shell tests
cover real versus unknown progress, error replacement and control availability.
The actual packaged browser check additionally verifies that the overlay retires
after graphics preparation and before interactive CSS. A separate UI fixture
was visually inspected at desktop and 320-pixel widths; it is layout evidence
only. All native/gameplay measurements above used the actual native build.

Local audit, hosted HTTP verification and all ten real-browser public UI checks
on both local Pages and the immutable preview pass. Final full discovery passed
758 tests with 31 optional-target/fixture skips in 228.313 seconds. The full-suite
and hosted results are retained alongside the candidate under
`work/loading-feedback-20260914/`. The initially occupied local port was recorded
and left alone; a free port served the candidate. Performance measurements ran
before the full test suite, without a concurrent build or CPU suite.

## Identities and verification scope

- Artifact source commit: `b32dcfd155037deb9129e9122e2ce07710e48a80`.
- Runtime directory: `0aacf74ab4cd2f5b`.
- Manifest SHA-256: `0d61ffacb4f0e53b41735877aa31e3c8ebc346179da72b1a20e1acb7aa05d9bb`.
- Wasm SHA-256: `7e15e154fbec89f2a24de55a02a5d72c725eaff69c97f4f813aed299074624a6`.
- Cold record SHA-256: `60f8cc0f5f4e232d7a222c72a17c483110895611f8830544d838af6df18b46e3`.
- Warm record SHA-256: `b5947615f8349774c4849bbd29e9f2d1b2dc2029b9b06eea7ed6730fe23ee3da`.

The public packager verified the existing native build identity. Wasm, generated
loader and catalog bytes are identical to production. Only the shared JavaScript
owner and player presentation changed, so another native compilation and CPU or
human-replay corpus would not test the changed boundary. Focused tests and one
browser lifecycle pair are the iteration loop; full discovery runs once at the
final checkpoint. Both human holdouts remain unopened and prior failures remain
preserved. No provenance/certification framework or native renderer work was
added. Exhaustive 4×4 manifest certification, capture-size optimization and
reducing cold graphics preparation remain separate follow-up work.

Once reviewed and authorized for production, the exact promotion command is:

```sh
WRANGLER_SEND_METRICS=false \
  work/pipeline-provenance-20260913-01/deploy-tools/node_modules/.bin/wrangler \
  pages deploy work/loading-feedback-20260914/public-candidate \
  --project-name webmelee --branch main \
  --commit-hash b32dcfd155037deb9129e9122e2ce07710e48a80 --commit-dirty=false
```
