# H1 browser snapshot CSS observer storage repair

**Source identified**. The [portable receipt](../evidence/h1-css-observer-abi-suite-v1.json) binds the change and full-suite result to harness commit `06f570c0409ad2ee29c59dcfe96e71998d8a8791`.

The native `melee_web_css_observe_port` boundary writes `int ids[14]` and `float geometry[8]`. The browser snapshot helpers had allocated 16 bytes for `ids`, short by 40 bytes, while the 32-byte geometry allocation was correctly sized. The whole-session and character-reference browser helpers now allocate all 14 integers and 8 floats and free whichever allocation succeeded if the other allocation fails. The VM boundary tests write every output element, check canaries, cover both allocation-failure paths, and retain null-result and non-CSS-phase behavior.

The required wrapped `python3 -m unittest discover -s tests -v` run passed at this source commit: 1,929 tests, 1,788 passed, 141 skipped, and none failed. This includes the development CSS observer storage boundary and whole-session capture/replay tests. The log and result are retained in external run `h1-full-unittest-20261007-20261006-202157-dfc77437` and hashed in the receipt.

The repaired browser helpers and tests do not change `web/` or `src/`; the H1 Release producer and paired-scene capture remain bound to commit `3470f887` and its frozen 32-artifact manifest. No rebuild or browser rerun was performed at `06f570c`. The H1 diagnostic disables CSS snapshot capture, so its successful stopped-image pair did not exercise this CSS observer branch. This repair does not explain the earlier cursor reset or identify a natural timing-pause cause. H1 timing and full-route acceptance remain open.
