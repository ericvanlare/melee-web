# Stadium C5: archive transfer and source teardown

**Compiled / Source identified**

Current boundary: service components, public/audio builds, and the repaired full regression pass. C5 and whole-stage acceptance remain open.

[Milestone #345](https://github.com/ericvanlare/melee-web/issues/345) composes the
exact-disc transfer, source-file service, and Stadium map-2 writable-region
owner. The [evidence record](../evidence/issue345-stadium-archive-service-v1.json)
keeps the service, compatibility, build, and regression identities separate.

The C5 service implementation is at 9d9d53b6; its five-file compatibility
repair was committed as 3639903a. The composed service controls and ordinary
and Stadium-diagnostic Release builds passed at the service source. The
compatibility repair then passed its 23-test focused control, and the public
runtime and audio-preview Release builds passed at 3639903a.

The first full regression is preserved unchanged: 2,973 tests, 211 skips, four
failures at 9d9d53b. They expose a public export-list expectation that omitted
the new asset-transfer export, two extracted end-flow fixtures without the
production world binding, and a native-menu fixture that extracted region
helpers without their call sites. The reviewed five-file repair is now applied
at 3639903a; it updates those compatibility expectations and fixtures only.
The repaired full regression at 3639903a passed 2,973 tests with 211 skips;
the process group was absent at completion. Its result and complete captured output are retained at
`runs/stadium-owner-parity-20261026-150215-d5a0bac0/c5-archive-service-full-regression-v2/`. The first four failures remain preserved as historical evidence. Final current-head CI is pending.

The service component now carries authenticated rounded-transfer tail bytes
without changing logical file bytes or hashes, accepts the source's 0x21 and
0x23 request classes only for owned files and destinations, and records a
destination-to-filename receipt only after a complete file copy. Its match
owner pumps requests at the source tick boundary. The diagnostic Stadium
StageLast owner binds the exact live map-2 region to its active source scope;
tick, draw, and teardown check ownership and quiescence before continuing or
mutating owners. These are service and lifetime components, not a demonstrated
vanilla map transformation.

No browser gameplay capture or transformation run was performed. DVDFastOpen
remains an explicit STOP stub; the direct original lbFile async open/read
path is not yet connected end to end. No deterministic completion rule has
been justified across forms, and the transformed map archive/graph lifecycle
remains incomplete. Thus these results do not enable transformations or close
checkpoint 5. The earlier four failures, the native fixture compile failure, and the initial
cancellation fixture failure remain retained as historical evidence alongside
their reviewed repairs.

The next repository check is current-head CI. Even a passing component suite
does not demonstrate a live archive transform or C5 acceptance.
