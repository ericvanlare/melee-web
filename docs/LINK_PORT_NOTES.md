# Link and Young Link port notes

Development candidates based on PR 25's final source tip
`23680fb951abb740e04c701fea256d66a623f507` (merged as
`5eb27bc63160ab185a866df6a9113ab5be1fc56f`). Original dependencies remain pinned;
source changes live in the downstream patch, not the upstream checkout.
The [bounded receipt](evidence/link-young-link-replay-v1.json) binds the local
recording, recipe, runtime and comparison artifacts without distributing assets.

## Source identified

| Character | Character kind | Fighter kind | Base data | Action data | Costumes |
| --- | ---: | ---: | --- | --- | ---: |
| Link | `CKIND_LINK` (6) | `FTKIND_LINK` (6) | `PlLk.dat` / `ftDataLink` | `PlLkAJ.dat` | 5 |
| Young Link | `CKIND_CLINK` (21) | `FTKIND_CLINK` (20) | `PlCl.dat` / `ftDataClink` | `PlClAJ.dat` | 5 |

Both use the original `0xdc`-byte `ftLk_DatAttrs` extension. Item slots 0–4
are Bomb, Boomerang, Hookshot, Arrow and Bow; Young Link also uses slot 5 for
Milk. Slot 6 is an `HSD_Joint` part descriptor, never an Article. Native owners
hydrate this joint before publishing the fighter. The shared accessor rejects
the joint slot and bounds every family's native table; unused entries are null.
The extension's pointer-typed `x94`, `x9c` and `xA0` fields preserve their source
bits: the original common catch-attribute view consumes them as integer timers.

Article animation rows are serialized descriptors, **not** the number of
callbacks in an `ItemStateTable`:

| Article | Special block | Serialized animation rows |
| --- | ---: | ---: |
| Bomb | `0x34` | 4 |
| Boomerang | `0x64` | 3 |
| Hookshot | `0x60` | 0; original table is null |
| Arrow | `0x2c` | 1 |
| Bow | optional `0x08` | 6 |
| Young Link Milk | optional `0x04` | 2 |

Arrow's unused nominal trailing field is not present in its serialized region.
Hookshot's three joint pointers, Arrow's two, and Boomerang's two joint plus
animation/material/shape graphs are converted into native-owned resources.
Absent optional special blocks remain null. Distinct Link/Young Link item kinds
and attributes are retained; no per-asset success exemption is used.

`EfLkData.dat` / `effLinkDataTable` has four bank-6 model effects, including
the two children of Spin Attack. Its particle-command and texture roots are
both null, which the original loader permits; a partial pair is rejected.
The manifest includes all costumes, base/action archives, `link.ssm` and
`clink.ssm`. Special actions use the original Link-family IDs 344–359; the
bounded action inventory is not exhaustive move coverage.

## Shared runtime repairs

- Preserve a null, zero-length Wait-choice table. Inventing a nonnull empty
  table changes the original random-selection path and RNG consumption.
- Resolve base-fighter DAT optional externals to null, as the original
  `lbArchive_InitializeDAT` does. Common-item relocation handling remains separate.
- Admit the existing damage-adjust fighter command used by down-air, checked
  item hitbox scaling, and bounded positive item-command loops. Invalid indices,
  nested or unmatched loops, and oversized expansions fail explicitly.
- Decode common light-item actions 78–88 and 96–103 for every supported fighter:
  opponents also need the original bomb pickup/throw commands.
- Propagate preparation errors immediately to the browser replay report,
  retaining archive identity instead of waiting for a long timeout.

## Hookshot causal repair

The original matching-C helper writes through `*(&y + 6)` to force a particular
PowerPC stack layout. That is outside the C object and corrupts live hookshot
chain coordinates in Wasm. The downstream PC patch owns the volatile float
spill instead; an AddressSanitizer test passes the fixed helper and rejects the
unmodified helper with a stack-buffer-overflow.

Removing corruption alone leaves tiny position differences. Disassembly of the
owned revision-2 executable shows that normalization in `it_802A44CC` and
`it_802A4BFC` has fused squared-distance accumulation, three double fused
Newton refinements and fused position updates. The standalone `it_802A3C98`
has a different, unfused sum. The patch restores only those original inlined
operations through `gameplay_hookshot_math.h`; it does not enable global
contraction, fast-math, new tolerances or guessed arithmetic.

A passive original probe and the repaired native prefix agree at all 34 source
boundaries 347–380 for the defined fighter/item/chain fields. Uninitialized
previous-position words are not used as evidence. The final full browser
comparison then matches all declared fields, including the formerly divergent
fighter Z and subject transforms.

## Retail compared: one complete physical recording

The latest selected recording is P1 human Link against P2 level-2 Young Link on
Yoshi's Story, four stocks. A fresh strict original input replay, with a passive
queue-clock observer, independently reproduces all 38,108 semantic events,
7,070 source updates and 7,064 draws through result and teardown.

MWRC v6 carries the independently observed nonempty controller-queue snapshots
as platform inputs. It preserves the human input bytes and supplies neither
expected game state nor expected draw indexes to the port. This establishes
conditional gameplay equivalence, not original interrupt/CPU timing or live
controller scheduling. See [recorded-queue scope](RECORDED_QUEUE_REPLAY.md).

The visible browser matches all 7,070 updates and every one of the 7,064 draw
boundaries. Core fighter/input/PAD/RNG fields, CPU decisions, camera, subject
bones, HUD, magnifier and match outcome all match with the existing comparator.
Link wins with three stocks. Both fighters and their distinct stock icons were
visually inspected; this is not a pixel comparison. The replay harness enters
the source match from recorded setup and verifies completion and owner teardown.
It does not establish an ordinary controller-driven CSS → SSS → match → CSS
round trip for these fighters; that remains a separate route gate.

## Retained failures and limits

Earlier local runs remain preserved, not relabeled as successes: optional DAT
externals and incorrect article extents blocked construction; missing command
admission and Spin Attack's unregistered child effects stopped gameplay. The
first complete browser run still differed at source 367 in fighter Z and subject
transforms. The first passing debug run was separately instrumented and observed
a 139.375 ms native callback, a 145.325 ms browser callback, a 128.235 ms GPU
staging wait and 128 audio underrun frames during concurrent compilation.

The frozen Release runtime independently passes the same complete comparison;
the repository suite passes 967 tests with 50 skips. Its state capture records
a 98.650 ms native callback, 110.805 ms browser callback, 94.050 ms GPU staging
wait and 214 audio underrun frames while repository tests run concurrently.
These state captures are not a cold/warm performance admission. First-use pipelines, heap growth and
the existing GPU staging stall remain open. Pixels, PCM, live scheduling,
unexercised move interactions and broader stage/opponent coverage remain open.
A human-controlled Young Link recording is useful additional coverage, especially
for specials, aerial hookshot and Milk; it is not needed to resolve this selected
Link match. Neither fighter is broadly admitted or tournament-certified. No
public deployment or capture-app modification was made.
