# Donkey Kong platform shield-drop repair

The Battlefield shield-drop crash from [issue #50](https://github.com/ericvanlare/melee-web/issues/50)
is repaired. At the terminal of a fully consumed single-datum FObj track the
original reaches `FObjUpdateAnim` with `op_intrp` still NONE and passes an
uninitialized stack word to the callback — the same undefined-output class as the
observed FD terminal single-CON case. The host now defines that terminal output
as the authored last value (`p1`), which is what each interpolation type's own
zero-duration path emits; parser state and flags stay untouched and every other
undefined state still fails explicitly. **Focused checks**: the endpoint
reproducer (`hsd_native_trace --terminal-{branch-linear,branch-spline,
pass-endpoint,stop-ceil-endpoint}`) passes after panicking pre-fix; the
Battlefield reducer that aborted at Pass frame 25 (`donkey-platform-pass-v2.log`
state) now completes Pass motion 244 for all five costumes with pause and
repeated teardown; both public and development menu contracts pass with
`CKIND_DONKEY` re-enabled; Donkey's both-orientation FD lifecycle passes
unchanged. **Suite**: local 1,129-test run passes with 55 skips (owned fixtures
for some skipped checks live in other worktrees). Donkey is re-enabled in
public character selection. The independent original Donkey Pass consumer
capture remains the open confirmation item; see
[Donkey Kong's measured scope](../DONKEY_KONG_PORT_NOTES.md).
