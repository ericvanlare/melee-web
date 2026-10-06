# Networked runtime determinism at the supported route

**Compiled / Source identified / Headless browser exercised (functional, declared-channel scope)** for A1 under [issue #158](https://github.com/ericvanlare/melee-web/issues/158). The [scoped receipt](../evidence/net-determinism-a1-v1.json) binds the source, runtime artifacts, scenarios, comparator controls, and exclusions.

## Result

The comparator reports exact agreement across the six declared streams—`header`, `input`, `master_pad`, `scene_state`, `total`, and `objects`—for the full 5,082-tick CSS → SSS → Match → Results → CSS workload in the repeated baseline, cold normal versus cold CPU ×4, and cold session-arena fill 0 versus 165 comparisons. Each full run also matched its agreed start record and scene sequence. The route assertions selected Final Destination (stage 32), two human players, and four stocks each.

The repeated unperturbed run used **warm-existing** Chrome profiles. The separate cold normal and cold CPU ×4 runs used cold-empty profiles and were compared offline; they were not simultaneous. The CPU ×4 run recovered 352 timing pauses through the network owner lifecycle and then completed the route. These are functional state-comparison results, not timing or performance measurements.

The full arena-fill pair perturbed session-arena bytes at the pre-publication boundary. All six declared streams, start records, and scene runs matched. At each of five scene boundaries, the arena base and size matched but the 32 MiB whole-region hash differed, establishing that the raw regions differ. That digest is auxiliary and its differing bytes were not localized, so it establishes no whole-memory equivalence or localized gameplay-state divergence claim. The fill is one-shot, refuses after session allocation/ownership begins, and does not touch stack or unrelated memory.

A 1,200-tick negative control flips one input byte at tick 1,000. The comparator reports its first mismatch at tick 1,000 in `input`, `master_pad`, `scene_state`, and `total`; `objects` agrees at that tick. Focused comparator tests also reject empty streams, failed or incomplete metadata, and a mismatched agreed start context, while preserving an earlier object divergence separately from a later gating divergence.

## Runtime identity and validation

The development `runtime` build at source revision `7eb5d89` applies to the cold normal, cold CPU ×4, and full arena-fill captures. Before those captures, the served manifest bound 28 runtime files by HTTP status and SHA-256. After them, all 28 served hashes still matched; a post-capture HTTP pass also verified 200 responses and local/served byte identity for four diagnostic imports omitted from that earlier inventory. Those four files were verified after the captures only. Per-instance records do not embed a runtime build identity; the receipt therefore uses the retained pre/post HTTP manifests and their hashes instead of claiming an embedded ID.

The earlier warm repeat, prefix preflight, and input-flip negative control used a prior frozen development build. Its served artifact manifest was not retained, so the `7eb5d89` build identity does not apply to those earlier captures.

Headless installed Chrome 154.0.8037.98 ran with host speakers silent. Profiles were cold-empty where stated; OS and GPU-driver caches were not cleared or controlled. `gameplay_menu_browser.data` emitted a retained `net::ERR_ABORTED` request diagnostic, but runtime readiness, native CSS startup, record progress, and the full route passed. The request's abort origin is unknown, and no `.data`-specific exemption is claimed.

The shared retail Fighter-field visitor passed an exact four-row fixture comparison against the pre-change formatter output. The final `python3 -m unittest discover -s tests -v` run passed 1,910 tests with 141 skips in 339.533 seconds. The affected development `runtime` target built successfully at the revision recorded in the receipt.

## Evidence limits and retained failures

The comparator covers only the declared checksum fields. In particular, it does not claim complete Results internals or whole-world coverage. The receipt makes no retail-equivalence, FPS, timing, latency, performance, pixel, PCM, audible-output, physical-input, or network-transport claim.

Cross-browser and cross-architecture determinism remain untested. The original `e1` attempt is retained as a startup failure: both instances received connection refused and recorded zero ticks. It is excluded from all positive comparisons. Earlier request-failure and pause-recovery experiments are also retained in the run evidence; only the changed, successful hypotheses above are counted as results. The external evidence binding index is identified by run ID and uses literal paths relative to each retained run directory, without machine-specific paths.
