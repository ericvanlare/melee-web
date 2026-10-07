# One MWRC v10 original-CSS replay callback

**Source identified / Native traced / Browser exercised**

This covers the first CSS callback only; it is not retail compared,
pixel-compared, or performance admitted.

The exact MWRC v10 recipe (`cb6bf42b…13b95b`, seed `3336171383`, 50,394
source frames) completed one original-CSS browser callback on a clean Release
runtime built from `190d150` over `707c2ab`. The callback produced one source
step and one draw, returned to the callback tail, and was paused there at native
cursor 1 in CSS phase. Cursor 1 is a menu replay tick; this is not a gameplay
frame count. The browser's frame-zero record matched the separate native
one-tick reducer for record, scene, index, supplied inputs, RNG, and semantic
PAD history. The PAD-history digest was
`f3d40bdf39ba8121d351cd87aedfb807426cffe296ce4b46dd8b114b5d8ae64e`.

The 414 ms value in the capture report measures host-side replay-launch click to
the durable callback-tail marker. It is not native callback execution time. The
first-to-last page timestamp span across the 36 marker records was 18.660 ms;
that span includes the instrumented marker path and is not a performance
measurement. The visible screenshot shows original CSS. The full 32-file
served/local browser-artifact inventory matched before and after the run, and
the loaded `.data` payload matched its expected 3,674,112-byte hash. The owned
browser, capture, and server processes were absent after cleanup, with both OS
process observation and CDP attribution passing.

The earlier `add492c` bounded-prefix attempt used the same full recipe, disc,
loaded `.data`, Chrome, and Playwright versions, but it was not the same test.
It used a ten-cursor target with 250 ms polling, a 90-second replay timeout,
Node 22, and a 255-second owner budget on base `73d3c4c`. Its last successful
poll observed running 1, phase 1, cursor 0; no later cursor was retained, so the
terminal cursor is unknown. Its post-timeout page-closed error is not a native
runtime error. That run had no screenshot or CDP process inventory. The current
probe uses a durable marker listener, a one-callback pause, Node 24, and a
95-second owner. Its JS and Wasm hashes differ from the old build, while the
`.data` hash is identical. The v10 recipe-support patch itself range-diffs as
identical between the two source bases; current base commits `#174` and `#176`
and the later probe changes are additional differences. The one-callback pass
therefore does not establish that the older full-replay stall is fixed.

The full Python suite on `f904e98` ran 1,930 tests (141 skipped) and retained
one failure: the browser replay lifecycle VM called `window` where its Node VM
fixture has no `window`. `190d150` changes that lookup to `globalThis`; the
fixture remains unchanged. Focused lifecycle and marker-protocol checks pass on
`190d150`, and its official Release runtime build passed. The full suite was not
rerun after this narrow fix; final CI remains required.

The portable receipt [`b4-first-replay-callback-v1.json`](../evidence/b4-first-replay-callback-v1.json)
binds the producer, inputs, native reducer, browser markers, screenshot,
ownership, and test/build evidence by source identities and SHA-256. Private
artifact locations and raw game assets are omitted. The separate
[source/run comparison](../evidence/b4-add492-to-190d-review-v1.md) explains
the earlier failure and proposes the next bounded callback boundary. This
scoped result is not a full-session comparison, retail comparison, pixel or
PCM validation, GPU-cause finding, foreground timing result, or performance
result.
