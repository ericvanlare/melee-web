# B4 `add492c` to `190d150` comparison

This is a source-and-evidence review for the retained failed ten-cursor attempt
and the successful one-callback probe. It does not modify either run receipt.

The minimal v10 support patch is equivalent across the two source bases:
`git range-diff 73d3c4c..add492cf 707c2ab..be1943d` pairs the single B4
v10-support commit exactly. That patch adds v10 parsing and the three-entry
CPU9 setup/profile checks, applies the recorded setup table at match entry, and
allows the maintained whole-session runner to accept the v10 format. Therefore
the current one-callback result is not explained by a new v10 recipe/parser
change relative to `add492c`.

The source-order correction is separate. `01b66dc` changes only the native
trace harness and its assertion: it moves `retail_replay_frame(recipe, 0, CSS)`
before the ordinary audio boundary to match the browser callback's source order.
It does not change the browser's production tick or draw order. The bounded
native reducer on `572ac785` consumed the complete recipe, entered fresh
original CSS, returned from one CSS host tick, emitted frame zero, crossed the
ordinary audio boundary, and tore down. It used a hash-bound 76-file authored
menu fixture. Its frame-zero diagnostic is the reference for the browser
comparison, not a retail result.

The timeout/ownership observability changes are also distinct from replay
semantics. The prior prefix runner polled native state every 250 ms and its
last successful observation was cursor 0; it never retained a later snapshot.
The current probe installs the CDP console listener before navigation, persists
each recognized marker synchronously, records CDP process information early,
and bounds the click/tail operation. The earlier owner observed and cleaned up
its OS-owned browser and server groups but had no CDP process inventory, so
strict attribution failed. The current run has complete CDP and OS cleanup
proof. These changes improve observation and ownership evidence; they do not
identify the old native stall's cause.

The callback markers and pause are another separate change. In development
builds, the explicit opt-in records the header/log callback, `menuReplayStarted`
and its native-memory snapshot, CSS tick, frame-zero output, ordinary audio,
`SourceFrameSequence` finish, Aurora begin/draw/end, and callback tail. It calls
the existing native pause only at the normal callback tail after diagnostics
and `SourceFrameSequence.finish()`. The browser pass reached this tail once,
with one step and one draw, at cursor 1. That demonstrates the first callback
returned with this probe enabled; it does not show what a second callback or
the original unpaused full replay would do.

The narrow `190d150` correction changes the marker lookup from `window` to
`globalThis` so the existing Node VM lifecycle fixture, which has no `window`,
can run. The fixture is unchanged. The complete suite failed before that fix
at `f904e98` with this one `ReferenceError`; focused lifecycle and marker tests
then passed, and the official Release build passed at `190d150`. The full suite
was not rerun after the fix.

| Dimension | Earlier failed prefix | Successful callback probe |
| --- | --- | --- |
| B4 source / base | `add492cf` / `73d3c4c` | `190d150` / `707c2ab` |
| Inputs | Same v10 recipe, disc, and loaded `.data` hashes | Same hashes |
| Browser | Chrome 154.0.8037.98; Playwright 1.62.1 | Same versions |
| Node orchestration | 22.23.2 | 24.19.0 |
| Run mode | Whole-session prefix toward target 10, 250 ms cursor polling | Durable first-callback marker and tail pause at cursor 1 |
| Bound | 90-second replay timeout; 255-second owner, including 45-second cleanup | 10-second callback bound; 95-second owner, including 5-second cleanup |
| Runtime JS / Wasm | `e232955d…866c1` / `b91ac09b…23a2` | `fd0faf2d…e051f` / `930adb12…99975` |
| Runtime `.data` | `255ca059…c53ca` | Same |
| Last reliable cursor | 0, running 1, CSS phase; terminal cursor unknown | 1, running 0 after deliberate pause, CSS phase |
| Persisted screenshot / CDP attribution | Neither; OS cleanup passed, strict CDP attribution failed | Screenshot retained; CDP attribution and strict cleanup passed |

The source base advanced from `73d3c4c` to `707c2ab`, incorporating main
changes `#174` and `#176`; the final runner, runtime JS, Wasm, Node version, and
owner bounds also differ. The same-input/browser overlap is useful, but this is
not a controlled before/after comparison and does not establish that the old
stall is fixed. The old terminal cursor remains unknown, and its reported
page-closed error occurred after owner cleanup rather than as an observed native
runtime error.

## Next bounded boundary

The next discriminating experiment is a fresh exact-recipe run that allows two
ordinary source callbacks and pauses at the normal callback tail once native
cursor reaches 2. Keep the same full v10 recipe, input, mode, draw path, and
callback ordering; change only the diagnostic pause target. Use the same
10-second launch-to-target bound and 95-second owned cleanup envelope. Extend
the marker validator to partition markers by callback/cursor because per-call
`SourceFrameSequence` step and draw counters reset; retain every begin/return
and the first unmatched marker. A pass isolates the first two callbacks only;
if cursor 2 passes, choose a later target from its trace under a separately
reviewed preflight. Do not infer a GPU cause or run an automatic cursor sweep.
