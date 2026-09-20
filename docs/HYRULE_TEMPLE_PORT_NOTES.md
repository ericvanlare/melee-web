# Hyrule Temple development integration

Hyrule Temple is being integrated on `codex/full-game-integration`. Its native
stage owner passes two complete lifetimes. Browser gameplay and original-game
equivalence remain open.

## Source contract

Original `St_Kind_Shrine` 14 selects `Gr_Kind_Shrine` 7 and `GrSh.dat`.
`grSh_StageData` creates map IDs 0, 1 and 2. The archive contains three map
entries, each consumed with one animation slot, 98 collision vertices, 91
collision lines, 11 collision joints and three lights. The source ground scale
is 0.9. The authored item table is empty.

Temple publishes neither `map_ptcl` nor `map_texg`. Original `grDatFiles` creates
the stage particle banks only when both roots exist. The profile therefore
allows this source absence while rejecting incomplete root pairs and checking
every authored particle event against the banks actually published. It does
not create an empty replacement bank.

The `yakumono_param` public root is present and its four-byte payload is zero.
The public address is nonnull; reading the first word as an optional pointer
would lose the source contract. Temple's callbacks retain that address without
dereferencing the payload, so the native arena owns an opaque four-byte copy
and restores the previous source pointer at teardown.

The source music words are 75, 1, 75 and 1, with selection flags 3, 12 and 100.
Primary BGM 75 is `shrine.hps`; alternate BGM 1 is `akaneia.hps`. The ordinary
alternate selection uses the original unlock/input/RNG logic, including its
Marth-unlock condition and 12% choice. Both files are present in the runtime
manifest. The native fixture's forced source selection masks check the four
table candidates; they do not validate the random branch or emitted PCM.

## Checks and retained failures

The source map constructor and exact scaled player, camera and blast marker
checks pass. The first attempt failed an
overly broad light-list assertion. A diagnostic rerun exposed flags
`4, 1025, 13` rather than the serialized `4, 13, 13`. Original
`Ground_801C20E0` applies override flags 0, 32 and 192: the second light clears
bits 4/8 and gains `0x400`, producing the observed 1025. The fixture must check
this source mutation separately from archive decoding. Scaled-light checks
must inspect live `HSD_LObj` positions because `Ground_801C2374` scales those
objects, leaving the serialized descriptor coordinates unchanged.

Further fixture corrections use the original `HSD_WObjDesc` layout (positions
start at byte 4), query the three global `map_plit` identities through their
separate native owner, and query a known platform midpoint instead of assuming
Temple's disconnected first floor chain crosses x=0. These failures and their
source evidence are retained; no game behavior was changed to satisfy them.

The final native probe passes two lifetimes of 120 source ticks each: map IDs
0/1/2, exact scaled markers and lights, global light overrides, floor line 0,
all four forced music candidates, immutable archive bytes, restored yakumono
pointer and zero live SDK objects. This is a stage-owner check; it does not
instantiate a complete match session. See the
[hash-bound receipt](evidence/hyrule-temple-development-v1.json).

A separate native match check also passes twice: explicit four-stock Mario/Mario
start, original Ready, 120 neutral source ticks, pause, L+R+A+Start No Contest,
original completion and teardown. This is a source-start fixture, not an
elimination match or menu-input test.

The browser uses original CSS/SSS input to select Temple and advances 30 drawn
source frames. Its first driver attempt sent Start before the original 30-tick
initial CSS cooldown; the corrected 35-tick input wait succeeds. The successful
entry retains one automatic timing resume, 801.65/815.10 ms native/browser maxima
and 64 audio underrun frames. Its 16 newly observed pipeline descriptors were
reviewed and appended while preserving all 700 previous records. The rebuilt
cold/warm entry pair reaches 30 advancing source frames each, with no entry
timing resumes, new entry pipelines, hard active callback gaps or audio
underruns. Cold retains one 71 ms browser long task; this short entry check
is not broad performance admission. The complete fighter/stage action matrix,
broader interactions, original comparison, pixels, PCM and physical input remain
open. See the [stage verification contract](ADDING_STAGES.md).
