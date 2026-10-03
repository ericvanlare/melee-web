# CI verification turnaround

Three consecutive full PR verification runs completed in 6m 32s for a source
change, 5m 49s for a test change and 8m 53s for a cold dependency change. The
workflow uses standard Linux runners, partitions the existing targets/tests,
and reuses only validated compiler-cache entries. All 895 previously passing
baseline test IDs remain covered; deliberate build/test failures reject the
aggregate, and a new PR revision cancels obsolete work. Automatic PR and main
verification remain, with duplicate feature-branch push runs removed.
See [CI timings, coverage, cache boundaries and cost](../investigations/CI_COST.md) for the
measured evidence and retained outliers. This changes verification turnaround;
gameplay accuracy and runtime performance keep their separate acceptance gates.
