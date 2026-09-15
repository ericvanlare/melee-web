# Verify CI cost audit

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
