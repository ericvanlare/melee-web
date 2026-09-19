# Verify CI cost audit

## Issue 36: parallel verification candidate

The candidate workflow keeps the existing checked `RelWithDebInfo` compilation
flags and the exact union of `build.py`'s `all` and `fighter` targets. Seven
independent standard ARM Ubuntu jobs configure fresh build graphs: full unit tests
and source census, runtime, graphics, gameplay checks, fighter probe, effect/data
traces, and menu traces. Full discovery runs after configuration so generated
headers and SDK dependencies are present. Tests that need linked binaries run
again in their owning build job, with required asset-free consumers forbidden
from silently skipping. `browser-build` is the aggregate and fails if any
partition or the public shell fails, skips, or is canceled.

Automatic verification runs once for each PR revision and once after a push to
main. The public-shell checks are part of that same aggregate; the separate
public-release workflow retains manual/release public-player validation.

Only ccache compiler entries persist. Build directories, linked Wasm, generated
JavaScript and prepared source trees are never restored. Cache namespaces include
runner OS/architecture, the complete dependency lock, partition and an explicit
epoch; entries still validate compiler content, arguments and source/header
inputs. Dependency mode uses the compiler’s `-MD` dependency output (including
system headers) to avoid a separate preprocessing pass on a cache miss. No
ccache sloppiness is enabled. Source edits may reuse unrelated object
entries, while changing the dependency lock starts a separate namespace. A manual
`cache_epoch` can establish an empty-cache control without deleting other runs.
The cache is bounded to 512 MiB per partition. Dependencies are freshly fetched
and verified by the existing bootstrap on every job.

Each partition retains phase times, runner details, test outcomes, Ninja edge
wall times and compiler-cache statistics for 14 days. These reports contain no
game inputs or generated binaries. Summed Ninja edge time is concurrent work,
not elapsed workflow time. GitHub run/job timestamps remain the authority for
workflow turnaround and queue time. The aggregate job adds a clearly labeled
in-progress timing snapshot to its GitHub summary. After a run completes, obtain
the final result with:

```sh
python3 scripts/ci_report.py --run RUN_ID --output work/ci-run.json
gh run download RUN_ID --dir work/ci-artifacts
```

`accepted` requires a completed successful workflow, successful required jobs,
and no more than 600 seconds from creation to the last job’s completion. The
report separates each job’s queue and execution durations; it never substitutes
summed parallel work for elapsed turnaround. An incomplete or failed run cannot
be accepted even when its observed elapsed time is short.

Measured cold/warm acceptance, failure controls and the final run inventory are
pending. This candidate does not yet close issue 36.

The first x64 partition experiment, [run 35463683397](https://github.com/ericvanlare/melee-web/actions/runs/35463683397),
missed the target. Graphics completed at 9m 50s and gameplay at 11m 52s from
workflow creation; the larger partitions took longer. A stale test assertion
about the old build-map text formatting failed the unit partition; the assertion
now checks the actual target inventory. Retain this failed experiment alongside
subsequent cold/warm measurements.

The first ARM/dependency-mode control, [run 35464529105](https://github.com/ericvanlare/melee-web/actions/runs/35464529105),
passed every partition in 10m 40s overall, so it still missed the target. Its
reports contain all 895 distinct passed test IDs from the preceding serial
workflow (936 passed IDs in the partition union). Runtime compilation took
517.3s after a 46.8s configuration. The next cold namespace tests three Ninja
workers on the same two-core ARM runner; compiler flags remain unchanged.

When a new test requires a linked binary, put its suite in the owning partition’s
`LINKED_TESTS` and add asset-free mandatory consumers to `REQUIRED_TESTS`. Full
discovery alone cannot establish coverage when a missing build causes a skip.

## Preserved earlier baseline

The baseline is [push Verify 34916505822](https://github.com/ericvanlare/melee-web/actions/runs/34916505822)
at `f9f82bd29e5c3015cd69e6a558f3e54278a798ad`. Its corresponding
[PR Verify](https://github.com/ericvanlare/melee-web/actions/runs/34916530543)
also ran successfully. The push checked the exact commit; the PR checked the
temporary integration commit. Neither performs original-game or GPU acceptance.

| Phase | Observed wall time | Work |
| --- | ---: | --- |
| Bootstrap | 35 seconds | Pinned source/toolchain setup |
| Initial test discovery | 5m 17s | Entire suite: 870 tests, 54 skips |
| HSD bridge rerun | 10 seconds | Four tests already in initial discovery |
| Compile targets | 20m 01s | Three sequential build invocations, two Ninja jobs |
| Post-build checks | 2m 44s | Overlapping compiler/runtime tests plus gameplay checks and census |
| Browser input rerun | 2 seconds | 13 tests after SDL headers become available |

The job lasted 28m 51s. The first test step's old label, "Test setup, server
and asset parsers," understates its scope: it discovers every test module,
including native and Emscripten compilation and Node execution.

## Compile work

| Invocation | Wall time | Object compilations | Link steps |
| --- | ---: | ---: | ---: |
| Default `all` | 928.7 seconds | 1,645 (1,374 C, 271 C++) | 74 |
| `fighter` | 191.8 seconds | 28 | 7 |
| `runtime` | 80.6 seconds | 1 | 2 |

`scripts/build.py` already includes `gameplay_menu_browser` in `all`.
The workflow now omits the final redundant `runtime` invocation while keeping
the default and fighter targets and every test/check command. This removes
work that took 80.6 seconds in the baseline; a subsequent cold CI run is needed
to measure the resulting job time.

The redundant invocation was not a no-op: all three configurations rewrote
`imgui_impl_wgpu_emdawn.cpp`, rebuilt its object and relinked its library. The
final invocation relinked the browser runtime. The Aurora downstream CMake
adapter uses unconditional `file(WRITE)` for this generated source. Preserving
its timestamp when contents agree is a separate build improvement to validate.
The logs do not show wholesale fighter recompilation during later invocations.

## Remaining opportunities

1. Run full discovery once after all required builds, retaining a small early
   smoke check if desired. At least 65 test names repeat in the post-build
   gameplay/HSD groups. Estimated savings are two to three minutes; preserve
   tests whose initial skips disappear when build outputs or headers exist.
   The explicit gameplay check and source census are separate commands and
   must remain. This restructuring has not been implemented here.
2. Trial a compiler cache keyed by the pinned toolchain and actual compiler
   inputs. There is currently no CI build cache. Reusing object compilations
   could address the largest cost, but no cache speedup has been measured.
   Dependency/bootstrap caching alone targets only the observed 35 seconds.
3. Measure runner utilization before increasing the fixed two build jobs.
   More parallelism may help, but available memory and link contention matter.
4. Review automatic push/PR duplication as a policy decision. Both ran for the
   baseline head because concurrency keys use different Git refs. This doubles
   compute for an open PR; it does not double an individual job's duration.
   Preserve a way to verify the exact proposed commit as well as integration.

Several slow test gaps are compiler-driven: constructor storage lifetime
about 23 seconds, loader callback bounds 18, native fighter data 16, action
storage 15 and typed attributes 12.5. These are differences between CI log
timestamps, not isolated test-profiler measurements. Removing these tests would
remove useful coverage; sharing their setup or compilation needs separate
validation.
