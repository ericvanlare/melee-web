# Versus return loop

[Issue #34](https://github.com/ericvanlare/melee-web/issues/34) extends the old
Results-skipping playable slice. Its acceptance remains open: the original
Results scene, whole-session retail comparison, physical input and retained
source-heap sequences are separate from the keyboard crash repair below.

## No Contest input ownership, 2026-09-19

**Source identified / browser exercised.** A fresh Release build from
`7d02d4c9bb12b7aa66430d8a019045dc46470b35` reproduces
`CSS exited with unsupported pending scene 0` in visible Chrome
153.0.8010.48 on macOS ARM64. The ordinary development UI imports an owned USA
revision-2 CISO, selects B0XX keyboard P1 with a CPU opponent, and enters
Mario/Final Destination through original CSS and SSS. Start pauses the source
match; after its debounce, L+R+A+Start exits No Contest and fails on CSS return.
The keyboard mapping is Start=`7`, L=`q`, R=`9`, A=`m`. No raw-PAD diagnostic
or source selection write is used.

The original `gm_801A4D34` flushes the PAD queue at scene entry while retaining
HSD input history. The host instead reset all three HSD history banks on each
entry. A held quitting chord consequently became a new press in CSS.
`mn_8022F218` detects the newly triggered L+R+Start combination;
`mnCharSel_OnFrame` requests `GM_MENU` without setting its ordinary pending
CSS/SSS field. The pending value `0` was the downstream symptom.

The host now retains typed PAD configuration and Master/Copy/Game histories
across CSS, SSS and match ownership changes. New owners retain their own queue
and rumble pointers; external contexts still restore when an owner closes.
The final match history is captured before teardown restores the external
context. This preserves held/released edges instead of masking shortcut keys.

**Native traced.** The linked PAD regression runs the original HSD renewal,
controller map and CSS shortcut detector. Its reset-history negative control
produces the false edge; retaining the captured histories prevents it while a
real release/repress still triggers. The real-asset menu-host trace also passes
on stage kinds 32 and 31 (Final Destination and Battlefield), including the
first held CSS sample, subsequent release and a new CSS/SSS match selection.
The linked PAD regression is required in the existing Linux fighter CI job.

`tests/versus_return_browser_test.mjs` exercises three successive ordinary-input
matches in one document. All three pause/LRAS returns pass, and the second and
third match start successfully. The first quitting chord is held for 120 ms;
the other two remain held for 1,000 ms through the return. CSS remains stable
after release. The harness preserves a failure, never retries a preparation or
automatically resumes a timing pause, and takes its screenshot after the route.

The initial browser attempt failed before CSS: the imported pipeline seed was
still draining when disc import hit its renderer timeout. That failure is
retained. The next attempt explicitly observed the existing renderer-idle
signal before import and reproduced the input crash. This harness sequencing
change does not relax a runtime timeout or establish a performance pass.

Local evidence remains in ignored `work/issue34/`:

| Artifact | SHA-256 |
| --- | --- |
| Initial preparation failure, `repro-1/report.json` | `bbdd57a9559e29a7c7c2c741dbee6bd8181a93f3261f28304dee0af95d8cea5c` |
| Original No Contest failure, `repro-2/report.json` | `bfa5ae2992fafa826519e2ec7e4e712317bfdd3642a908a654a3920a7cee570b` |
| Three repaired returns, `three-returns/report.json` | `0f5b6706dc19f3bdf275d1a6c0e3f51f6da59da839ccba996d8fb1ca3168eb78` |
| Baseline Wasm | `7a03f99991e4c4082b5b499ee06db7ecf9c447c52d3b53eed26bd23a34c44f26` |
| Input-fix Wasm | `0bdc2f2f559c3b5ed68e7327c6da7d99ef14ea03dd6c37ac316a111546823f9f` |

The CSS allocator snapshots after the three returns contain 313,468,104,
314,554,440 and 314,558,880 live bytes. Linear memory remains 400,949,248 bytes
after the first match. These observations retain the first-use growth and the
4,440-byte final increment; three samples do not prove a leak bound. The source
SDK world is still reconstructed between scenes, and these runs do not certify
retained original heap history, retail equivalence, Results, physical-controller
behavior, audio equivalence or timing. The separate [GPU/holdout gate](
https://github.com/ericvanlare/melee-web/issues/33) remains required before the
whole-loop cold/warm acceptance measurements.
