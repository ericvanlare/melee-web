# Validation

Run `python3 -m unittest discover -s tests -v` for setup, real HTTP, synthetic disc,
DAT, texture, fighter metadata, model and animation tests. Native parser and clock
tests require a C++20 compiler. Original HSD polygon/skin, transform, material and
animation tests use the pinned project SDK and skip if it is not installed.
CI runs them again after installing the SDK. Input adapter tests use a controlled
PAD provider; they need the configured SDL headers and CI
runs them after the browser build. They do not validate physical controllers.
Run `python3 scripts/build.py` for the actual WebAssembly link.
The build also runs the source-registry drift check. After an intentional upstream
registry change, run `python3 scripts/generate_fighter_registry.py`, review the
generated diff, then run it again with `--check`.

The source traces check envelope blending and model-node transforms, captured
normal/reflection palettes, pose updates and nonfinite rejection; they also check
original animation interpolation, frame requests, loop flags and pose ownership.
GX calls are recorded in tests, so these establish source/SDK behavior rather
than GPU rendering. Synthetic visibility tests cover costume fallback, distinct
main/metal DObj lists, ambiguous variants and DObj-to-PObj mapping. Clock tests
cover cadence, pause, hidden time and bounded catch-up.
Binding tests cover exact source identities, common part layouts, rejected
alternate mappings, action aliases and holes, and bounded container slices.
Stage tests cover counted entry tables and malformed service references; model
tests cover explicit pass omissions and retained transform dependencies. Original
material and polygon traces check shininess above 128 and direct RGBA8 colors.

## Prepare local fighter and animation inputs

Use a local GALE01 revision-2 image and new output paths. The extractor opens the
image read-only and refuses to replace existing output or write outside ignored
`assets-local/`:

```sh
python3 scripts/extract_disc_file.py "/path/to/your/melee.ciso" PlMrNr.dat --output assets-local/next-gate/PlMrNr.dat
python3 scripts/extract_disc_file.py "/path/to/your/melee.ciso" PlMr.dat --output assets-local/next-gate/PlMr.dat
python3 scripts/extract_disc_file.py "/path/to/your/melee.ciso" PlCo.dat --output assets-local/next-gate/PlCo.dat
python3 scripts/extract_disc_file.py "/path/to/your/melee.ciso" PlMrAJ.dat --output assets-local/next-gate/PlMrAJ.dat
```

For Fox, also extract `PlFxNr.dat`, `PlFx.dat` and `PlFxAJ.dat`; the same `PlCo.dat`
supplies common layouts for both fighters. Full AJ containers hold multiple DAT
archives and are sliced by the imported fighter action table, not parsed as one
archive. Selecting an action remains explicit.

The extractor can still select one embedded clip: add `--offset 0 --length 4239`
to the PlMrAJ command and choose a new output such as `MarioWait1.dat`. Offsets
are relative to the selected FST file, not the disc; omitting `--length` selects
the remainder. Decimal and `0x` values are accepted. A selected nonempty range
must fit inside the file and be at most 64 MiB. Output includes its range and
SHA-256 without interpreting the payload as a DAT.

No game content is required by the synthetic tests. If the explicitly extracted
`assets-local/next-gate/MarioWait1.dat` exists, the original animation trace also
evaluates that local clip. Its expected structure is 61 nodes, 111 tracks and a
50-frame endpoint. That optional evaluation is separate from browser acceptance.

## Browser smoke test

1. Start `python3 scripts/serve.py --directory build/browser` and open
   `http://127.0.0.1:8787` in a desktop browser with WebGPU enabled by default.
2. Check that the page renders a three-color triangle and increments frames.
3. Inspect the page log and browser console. Treat a WebGPU validation error,
   uncaught exception, or abort as failure even if the frame counter advances.
4. Extract `TyTarget.dat` as described in README.md and choose it in the page.
   Expect a red/gray bullseye, 2 meshes, 11 primitive packets and 676 submitted
   vertices. No game bytes are included in tests.
   Repeat with `TyHarise.dat`: expect the cream fan with red handle, 2 textures,
   3 primitive packets and 147 vertices. Loading an unsupported asset should
   replace the old model with a clear rejection and allow another import.
5. Import `TyBacket.dat`: expect a reflective bucket and handle with 2 joints,
   8 meshes, 11 texture objects, 18 packets and 408 submitted vertices. This uses
   original HSD material expressions and authored inspection lights; it does not
   establish pixel equivalence to the original game camera or light rig.
6. Import `PlMrNr.dat` and select `PlyMario5K_Share_joint`. Expect 61 joints and
   68 decoded envelope meshes. Apply `PlMr.dat` through the fighter metadata
   import for the default costume: the normal representation should contain
   43 DObjs and 52 meshes. Verify the visible fighter uses the normal body parts
   rather than drawing every alternate representation together. These indices
   address DObjs, so filtering by individual PObj/mesh number is incorrect.
7. Choose `PlCo.dat` in **Common fighter DAT** and `PlMrAJ.dat` in **Animation
   container DAT**. Explicitly select Wait1 under **Fighter action**, then press
   Play. Repeat by explicitly selecting WalkSlow. Expect 50- and 60-frame clips,
   respectively. Repeat the complete flow with `PlFxNr.dat`, `PlFx.dat` and
   `PlFxAJ.dat`: expect 73 joints, 45 normal DObjs, a 120-frame Wait1 and a
   50-frame WalkSlow. Selecting a container alone must not choose an action.
   Check that the pose
   changes and loops while the frame display advances at 60 Hz; Pause should
   hold the pose. Hide and restore the tab and verify there is no hidden-time
   catch-up. A stall requiring more than eight animation steps must pause with
   a message instead of blocking the browser. Reject incompatible fighter
   mappings, unsupported channels and malformed streams visibly. Replacing a
   model must clear the old animation and its visibility selection.
8. Extract and import `GrNLa.dat`; `map_head` is recognized automatically. Select **Stage entry** 3 and
   **Opaque only**. Expect 13 retained opaque meshes and 13 omitted meshes, with
   unapplied stage services listed. Verify that other entries remain selectable
   and an unsupported complete model produces an explicit rejection. Do not
   record this partial pass as a complete stage or collision/animation test.
9. Enable keyboard controls and focus the canvas. D should produce raw stick
   `[127, 0]` and a separate PADClamp result `[72, 0]`; Q should produce raw left
   trigger 255 and clamped 150. Release controls, then move focus to a page control:
   current input must be neutral. Also test a physical gamepad and disconnect it
   before recording physical-device support as verified.
10. Reload once and check initialization again. Hide and restore the tab; samples
   should reset and rendering should resume without including the hidden interval.
11. Record OS, hardware/browser identification available from the runtime, build
    type, observed duration and any errors in STATUS.md. Do not convert this
    inspection draw's CPU submission time into a claim about full-game performance.

These steps define acceptance checks, not a record that they have all passed.
Record actual results and remaining gaps in STATUS.md.

This currently has no automated GPU screenshot comparison. The CI workflow
checks tests and compilation, while the smoke test exercises the real browser.
Add a reference-image test when the first real asset scene is stable; future
gameplay tests must also compare simulation state, not only pictures.

## Batch asset coverage

`python3 scripts/check_assets.py assets-local/` builds the actual CPU parser once
and emits JSON lines for each public root. Supported roots report joint, mesh,
material, texture and primitive counts; other roots report their exact rejection. Exit
status is nonzero if any input/root is rejected. Redirect reports into ignored
`work/` or `build/` when comparing a local corpus across changes. A parser pass
does not validate source HSD transforms, GPU output or game-scene behavior.

For a stage, select one entry explicitly; `--symbol` selects the header symbol:

```sh
python3 scripts/check_assets.py assets-local/next-gate/GrNLa.dat --symbol map_head --stage-entry 3 --opaque
```

The report includes the selected pass, retained geometry, omitted DObjs/meshes
and joints, and unapplied stage services. Omit `--opaque` to check the complete
selected model strictly. `--opaque` requires `--stage-entry` and never silently
changes the material or joint behavior of retained geometry.
