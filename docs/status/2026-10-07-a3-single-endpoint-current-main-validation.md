# A3 single-endpoint current-main validation

**Source identified / Compiled**

The issue [#197 single-peer relay endpoint](https://github.com/ericvanlare/melee-web/issues/197) has a [portable receipt](../evidence/a3-single-endpoint-current-main-validation-v1.json) with the rebased producer, focused transport and Worker/A2 checks, full Python suite, and Release runtime build.

The endpoint implementation and its earlier component evidence were replayed onto current main with patch equivalence verified. The current-main checks are source/build validation only; no browser endpoint run was performed here.

The separate [#187 browser relay evidence](../evidence/a3-browser-relay-full-route-controls-v1.json) remains bound to its historical `4b58eaeb` producer. Its full route and controls were not repeated or represented as current-main browser evidence by this validation.
