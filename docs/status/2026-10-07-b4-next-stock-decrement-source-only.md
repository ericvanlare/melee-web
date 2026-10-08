# B4: next typed stock decrease identified after the accepted event

**Source identified**

[Issue #269](https://github.com/ericvanlare/melee-web/issues/269) identified the
first typed stock decrease strictly after the accepted #258 event. One approved
source-only scan freshly rejoined clocks 1, 60, 300, 500, 1000 and 2000, then the
accepted event and its consumed prefix before continuing. The portable
[receipt](../evidence/v10-next-stock-decrement-source-v1.json) binds the audit,
launch approval, execution and independent result reviews.

The later event is slot 3 changing from 4 to 3 stocks, producing `[4,3,3,3]`,
at source tick 3982, match clock 3859 and browser cursor 5167. Its source tick
sequence is 24022; its PAD consume sequence is 24020. That nonadjacent pair joins
by tick. The fresh prefix contains 166,680,623 bytes and 24,023 records, with
SHA-256 `c31863f04fcc44a454f316b9090989aac057ab868f8b3afb4a034604f53889dc`.

The scan retained separate historical clock-2000 limits of 128 MiB / 24,000
records and fresh limits of 256 MiB / 48,000 records / 60 seconds. The owner used
a 65-second process-group deadline with a five-second cleanup budget. Execution
exited successfully in about 2.72 seconds; its PID and group were absent and the
source stat remained stable. The full source trace was not rehashed.

This is source-only evidence. It establishes no browser agreement at cursor
5167, KO attribution, live timing, physical input, pixels, PCM, performance or
whole-session acceptance. The accepted #258 browser export ends at cursor 4807.
[Issue #271](https://github.com/ericvanlare/melee-web/issues/271) owns the minimal
comparator extension and later bounded comparison. Close #269 only after this
receipt and status entry are integrated into main.
