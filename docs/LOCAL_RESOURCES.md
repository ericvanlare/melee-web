# Local resource ownership

Every worktree contains source, installed tools, incremental builds, disposable
scratch, and potentially irreplaceable evidence. Git-ignored does not mean
disposable. Keep those categories separate when cleaning up.

## Start, run, and finish

At task start and handoff, record the current free-space reading:

```sh
python3 scripts/agent_workspace.py status
```

Bootstrap and the browser/reference build entry points coordinate automatically.
Use the wrapper for other expensive commands, including a full test suite:

```sh
python3 scripts/agent_workspace.py run -- python3 -m unittest discover -s tests -v
```

On macOS and Linux, at most **two cooperating heavy operations per user** run at
once, across worktrees, with one operation per checkout. Normal build commands
default to two compiler jobs each. Nested commands reuse their live ancestor's
lease; stale tokens do not bypass coordination. Kernel locks release when the
holding process exits. Busy operations wait up to five minutes and then stop
with a retry message. Avoid abandoning a parent while its children are running;
these cooperative locks do not manage arbitrary detached processes.

Each guarded operation prints free space before and after it runs. Local work
requires at least **30 GB free** when admitted (5 GB on CI); this is an admission
floor, not a reservation or a guarantee that the operation cannot fill a disk.
Estimate additional space before large captures or full rebuilds. A 50 GB
operating target leaves more room for simultaneous jobs and macOS swap. When
space is low, retire eligible output or report the blocker; do not lower the
floor to force a job through. A human-directed override can set
`MELEE_MIN_FREE_GB`. Do not manually delete swap files.

The small coordination files live in `~/.local/state/melee-web` by default.
`MELEE_RESOURCE_STATE` can select an isolated directory for tests; all normal
agents on one host must use the same directory. Never delete live lock files.
Windows retains the disk check and compiler-job defaults but does not currently
provide the POSIX host-wide lock guarantee.

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

Keep the current incremental build while a task is active. When the build is no
longer needed after completion, integration, or retirement of the checkout:

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
Rebuilding recreates removed intermediates. No other checkout is modified.

This deliberately does not guess how to clean historical unjournaled builds.
Their contents require a separate reviewed cleanup. It does not remove source
worktrees, dependencies, or host applications.

## Identical toolchains share storage

After normal pinned installation, bootstrap checks up to three compatible
registered Git worktrees with the exact same dependency lockfile. On macOS it verifies
identical large SDK and environment files and replaces only the new checkout's
copies with APFS clones. File paths, bytes, permissions, extended attributes and
modification times are preserved. Copies have independent inodes and later
writes stay local. Mutable caches are not shared through symlinks or hardlinks.

This is a post-install storage optimization: the first download/installation and
its peak disk usage still occur. The first checkout, incompatible files, and
filesystems without APFS clone support use the normal installation. A peer
changing during verification is skipped. No daemon, recurring deletion job, or
background compression is installed.
