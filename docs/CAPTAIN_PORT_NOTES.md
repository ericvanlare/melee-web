# Captain Falcon development integration

Captain Falcon is being integrated on `codex/full-game-integration`. This is
development work. Native source lifecycles pass both Captain/Mario orientations
and all six costumes on Final Destination, including combat, original pause,
No Contest and reconstruction. The browser completes original CSS/SSS entry and all 30 declared action cases.
Independent original comparison and broader fighter gates remain pending.

## Source and ownership

Captain uses `CKIND_CAPTAIN` 0 and `FTKIND_CAPTAIN` 2. His source data has
318 motion rows, a 0x8c Captain-family attribute record, six costumes, no
fighter Article table, and zero dynamic bones and modes. All six costume
material-animation symbols are authored null. The nonnull metal model still
uses the checked shared costume topology. `EfCaData.dat` provides bank 4 and
six effect entries; `captain.ssm` is source audio bank 6.

The source names the red costume `PlCaRe.`. Original `lbFileGetFullName`
selects `.usd` for the US setting and `.dat` otherwise. The gameplay loader
now reuses that rule from `DatMenuSupport::resolve_filename`, preserving the
registry's authored basename and rejecting a missing selected locale. The
current menu/HUD profile selects English, so its disc manifest imports
`PlCaRe.usd`. Both original variants were extracted for the decoder checks;
this does not enable a Japanese gameplay profile.

The self-action command range ends at the authored final row, 317. Its
sword/parasol branches reach opcodes 45/42. Those commands remain explicitly
unsupported at execution readiness: recognizing their encoded length or
retaining their original word does not establish the operand ABI and live item
services. No item behavior is replaced by a success stub.

## Evidence and next boundary

Focused real-data checks cover the Captain attributes, action container,
zero-count dynamics, null Article table and six-costume bounds. Filename
checks exercise both language selectors, explicit extensions and a missing
US variant without fallback. The first native mixed-content probe failed at
the neutral costume with `Only supported direct vertex color formats are
accepted`. Its actual descriptor is indexed RGBA8 with an array relocated to
data offset zero. The native HSD/GX path already supports that format; the
model decoder now preserves it while validating packed widths, strides,
relocations, display indices and array bounds. Focused malformed-data tests,
an indexed16 RGB565 case and all six original costume models pass. The viewer
keeps its narrower direct-RGBA8 policy.

The next constructor failure came from Captain's particle palette metadata.
`EfCaData.dat` texture group 6 stores `0x01000002` in the palette-format word;
original `psdisp.c` casts that word to `u8` before using GX format 2. The shared
decoder now validates that same low byte while retaining the entire authored
word. A focused source-bank fixture checks preserved metadata and palette/image
bytes, and rejects invalid low-byte formats.

The first advancing native run lands Falcon Punch for 25 damage. Two subsequent
test-driver failures are retained: full-stick recentering overshot because of
Captain's run/brake momentum, and an unassisted Falcon Kick ran off Final
Destination before the original respawn timer expired. Position-feedback walking
and ordinary movement off the rebirth platform address these fixture inputs;
the fighter's movement, collision, stock loss and respawn code are unchanged.
The final bounded trace passes all six costumes in both player orientations.
Captain executes ground and aerial neutral special on each costume and side,
up and down specials on the first costume. Its unassisted ground Falcon Kick
loses one stock before returning through source rebirth; this is not a recovery
technique or a claim that the move stayed onstage.

The [hash-bound development receipt](evidence/captain-development-v1.json)
retains the source identity, original asset hashes and first failed native
attempt. Later lifecycle, drawn action, original-comparison and performance
results are recorded separately. The versioned
`captain-falcon-visible-actions-v1` diagnostic completes all 30 common-action
and unassisted ground/air special cases over 5,200 drawn source frames. Raptor Boost
hit branches and Falcon Dive catches/throws require actual interactions.

The first live match reports exposed the excluded catch branch: a dive catch
runs the common CaptureCaptain submotion row 276 on the catcher's own store
(`grab_cb` -> `ftCo_8009CA0C`), and the unadmitted row dispatched the
unsupported-command sentinel, aborting the player mid-match. Ganondorf's Dark
Dive shares the same state. Real Ganon data confirms row 276 carries a one-word
END command stream, so the focused action-store check admits it for both
Captain-family kinds; a full browser dive-catch interaction remains unexercised
until a next gate run.

The first visible sweep was declared diagnostic with concurrent native checks.
It completed every action case but failed timing/audio/pipeline acceptance:
26 live pipelines, six browser gaps, six native callbacks over 33.3 ms,
404 audio underrun frames and four automatic resumes during the sweep, plus
one entry resume. The worst native/browser callbacks were 431.275/444.465 ms.
This failed run is retained. A reviewed cache merge adds 35 descriptors across
entry and gameplay while preserving all 665 previous records; that checkpoint
contains one shader and 699 pipeline records. The rebuilt isolated cold/warm pair
passes all 10,400 drawn source frames at 640×480, DPR 1. Native maxima are
8.515/8.275 ms and browser maxima 24.410/25.140 ms. Each run has zero native
target misses, hard gaps, sweep long tasks, audio underruns, live pipelines,
timing resumes, focus losses or live heap growth. Texture uploads are
941,568 bytes per sweep and peak staging use is 2,804,936 bytes.

These counters cover the action sweep. The cold pre-sweep screenshot retains
one 69 ms long task before those counters reset; full-route timing is not
admitted. Both entries reach source Ready without a timing resume. Application
cache is cleared for cold, then saved on unload and restored after a full page
reload for warm; browser/driver caches are uncontrolled. Complete matches,
other stages, interaction branches, pixels, PCM and physical controls remain
separate gates.
