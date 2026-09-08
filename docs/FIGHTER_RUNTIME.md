# Fighter runtime gate

This separate Wasm target calls original `Player_80031AD0` → `Fighter_Create`,
runs every registered fighter process under the original HSD scheduler, and
disposes the fighter through its original GObj destructor. It runs under Node
with checked local assets; it is not the browser match loop.

```sh
python3 scripts/build.py --target fighter
python3 scripts/check_fighter.py assets-local/next-gate
```

The directory must contain GALE01 revision 2 `PlCo.dat`, `PlMr.dat`, `PlMrNr.dat`,
`PlMrAJ.dat`, `GrNLa.dat`, `ItCo.usd`, `EfMrData.dat`, `PdPm.dat`, and
`sislib_font.bin`. Extract each named disc file with `scripts/extract_disc_file.py`.
The font comes from the original executable's configured font range:

```sh
python3 scripts/extract_font_atlas.py /path/to/game.ciso --output assets-local/next-gate/sislib_font.bin
```

Assets stay local and ignored. CI builds the full source closure and runs the
asset-independent checks; original-data tests skip when the user's files are absent.

## Acceptance

Two complete SDK worlds each create and destroy Mario twice. For each fighter,
original Fall/collision callbacks must reach grounded Wait within 120 ticks,
followed by 120 uninterrupted Wait ticks. The probe never assigns grounded state
or forces an action. It verifies one fighter and camera subject, eight inserted
metal DObjs, two retained eye animation tables and actual command-driven image
changes. Teardown must restore scoped player, PAD, RNG and counter state. All
input archives must remain byte-identical, and the next world must initialize.

Mario's initial costume bind pose has its lowest collision bone about 3.088 units
above the root. Starting the player one unit above the queried floor therefore
selects Fall during construction; the source simulation determines settling.
This is not an error to conceal by changing collision flags.

## Boundaries

- Owned native descriptors replace in-place big-endian archive relocation.
  The original constructors, action consumers, material animation and collision
  routines still execute. Mutable display lists belong to native descriptor owners.
- Actual common initialization, Mario item registration, effect-bank loading,
  stage lights, camera/blast bounds, font bytes and bonus thresholds are supplied.
  Item spawning, particle rendering and complete stage initialization are separate gates.
- Wait and Fall commands are decoded explicitly. Other scripts retain a failing
  sentinel until their operands and required services are implemented. In particular,
  Landing's surface effect/audio opcode is not enabled by this gate.
- Unavailable audio, disc/card, video and device services fail explicitly. The
  standalone owned asynchronous I/O queue is tested but not yet wired into game loading.
- Builds use assertions, safe-heap checks and debugging instrumentation. Their
  timings are not evidence of a 60 fps playable port.
- No original-game state trace has yet established simulation equivalence.

## Source ABI fixes

The generated gameplay tree preserves original MSL `bool` as an explicit
four-byte `melee_source_bool`; SDK and libc booleans remain unchanged. A checked
identifier transform preserves comments/literals and composes its diff with the
reviewed patch. Incremental patch transitions preserve unaffected file timestamps.

The full link exposed three inconsistent C contracts. `mnCharSel_802640A0` now
matches its canonical integer return declaration. Original executable instructions
show that `ftLib_800876B4` preserves the animation predicate in return register r3
(call at `0x800876c0`, tested by its caller at `0x80179be4`), and that the call at
`0x8024c754` passes the preceding `mn_802295AC` result to `gm_801677E8`. The source
patch makes these return/argument contracts explicit for Wasm's typed calls.
