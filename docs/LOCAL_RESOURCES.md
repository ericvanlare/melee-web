# Local resource ownership

Every worktree contains source, installed tools, incremental builds, disposable
scratch, and potentially irreplaceable evidence. Git-ignored does not mean
disposable. Keep those categories separate when cleaning up.

## Start, run, and finish

At task start and handoff, record the current free-space reading:

```sh
python3 scripts/agent_workspace.py status
```

Bootstrap and the browser/reference build entry points hold a checkout mutation
lock. Use the wrapper for other commands that use this checkout's builds or
tools, including a full test suite:

```sh
python3 scripts/agent_workspace.py run -- python3 -m unittest discover -s tests -v
```

There is **no host-wide operation cap, queue, slot wait, or new compiler-job
default**. Different checkouts run independently. Main's browser-build default
remains `min(cpu_count or 2, 6)`; the reference builder continues to leave
parallelism unspecified unless `--jobs` is supplied. Existing CI job defaults
also remain unchanged.

On macOS and Linux, a nonblocking mutex per checkout protects shared build and
tool files. A conflicting mutation in that same checkout refuses immediately;
it does not wait. Nested build/test steps can reuse a verified live ancestor's
lock. Cleanup and toolchain deduplication cannot borrow that lock: they refuse
while the checkout is active, even when invoked by an active build. Kernel locks
release when the holder exits. Avoid abandoning a parent while its children are
running; these cooperative locks do not manage arbitrary detached processes.

The status command reports available disk space. Entry points warn below 30 GB
locally or 5 GB on CI and continue; these thresholds are advisory, not admission
floors or reservations. `MELEE_WARN_FREE_GB` customizes the warning threshold.
Estimate space before large captures or rebuilds, and consider explicit
retirement when appropriate. Do not manually delete swap files.

The small coordination files live in `~/.local/state/melee-web` by default.
`MELEE_RESOURCE_STATE` can select an isolated directory for tests; all normal
agents sharing a checkout must use the same directory. Each lock is keyed by
the canonical checkout path; no global lock or slot is acquired. Never delete
live lock files. Windows retains advisory reporting and existing build defaults;
maintenance refuses when POSIX mutation locks are unavailable.

A repository change cannot coordinate older worktrees that do not contain it,
arbitrary commands that bypass the wrapper, or the desktop app's own workers.
Fresh worktrees created from main after this change inherit the implementation
and these instructions. Update older checkouts separately when appropriate.

## Successful tests own their scratch

The joined synth/audio runtime tests use `tests/owned_test_workspace.py`.
Successful classes remove their uniquely created scratch directories, including
compiled objects and temporary binaries. Test failures, subtest failures, setup
errors, class teardown errors, and interrupted test runs retain their evidence
at the printed original path. `MELEE_KEEP_TEST_ARTIFACTS=1` explicitly retains a
passing run when it will be used as evidence. Existing captures and neighboring
work directories are never swept by this helper.

Use this helper for new expensive class-level test fixtures. For lightweight
tests, prefer `TemporaryDirectory` with registered cleanup. Intentionally
retained reference fixtures and failure stores have different lifecycles; do not
convert them into disposable scratch just because they use `mkdtemp`.

Keep small reports, commands, hashes and failure diagnostics. Compress large
retained traces using a supported archive format and verify restoration before
removing originals. Never delete the only copy of recordings or local inputs.

## Retire a completed build

Keep the current incremental build while a task is active. Retirement is a
conscious end-of-task choice when the build is no longer needed, never an
automatic action after a successful build. It trades disk space for future
recompilation. Review the dry run before applying it:

```sh
python3 scripts/agent_workspace.py retire-builds
python3 scripts/agent_workspace.py retire-builds --apply
python3 scripts/agent_workspace.py status
```

The first command is a dry run. Applying the plan removes only unchanged `.o`
and `.a` products recorded from Ninja's actual output journal after a successful
guarded build. It checks content hashes, file identities, Git tracking, path
boundaries, and open files. Changed products, tracked files, failed builds,
symlinks, unjournaled files, final `.wasm`/application binaries, recordings, and
reports remain. Missing `lsof` or an active build causes cleanup to refuse.
Rebuilding recreates removed intermediates and will take extra work. No other
checkout is modified. Invoke maintenance directly, outside the `run` wrapper;
an active wrapper intentionally prevents it from acquiring the checkout lock.

This deliberately does not guess how to clean historical unjournaled builds.
Their contents require a separate reviewed cleanup. It does not remove source
worktrees, dependencies, or host applications.

## Opt in to toolchain sharing

Ordinary bootstrap only installs the pinned tools. When the checkout is idle,
explicitly opt in to APFS sharing with:

```sh
python3 scripts/agent_workspace.py dedup-toolchain
```

This command takes the checkout mutation lock and checks for open tool files.
It checks up to three compatible registered Git worktrees with the exact same
dependency lockfile. On macOS it verifies identical large SDK and environment
files and replaces only the current checkout's
copies with APFS clones. File paths, bytes, permissions, extended attributes and
modification times are preserved. Copies have independent inodes and later
writes stay local. Mutable caches are not shared through symlinks or hardlinks.

This is a post-install storage optimization: the first download/installation and
its peak disk usage still occur. The first checkout, incompatible files, and
filesystems without APFS clone support use the normal installation. A peer
changing during verification is skipped. No daemon, recurring deletion job, or
background compression is installed.

## Remaining overhead

These safeguards are not free: disk reporting, lock metadata, Ninja-journal
scans, file stats, hashing and deletion still cost time and I/O. Successful builds
record disposable products but do not delete them. The journal hashes each
distinct Ninja output once and reuses hashes across builds only when device,
inode, size, mtime and ctime match; changed or legacy entries are hashed again.
Retirement rechecks content hashes rather than trusting that cache. Dry run and
apply each validate their own current snapshot. Explicit deduplication hashes
and verifies large files and can be expensive. This PR makes no zero-overhead
or build-performance claim. Compute tuning and mandatory disk admission belong
in a separate, measured change.
