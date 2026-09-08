# From source probes to a playable stock match

The target is an accurate local Mario-versus-Mario stock match on Final
Destination at 60 fps. The immediate gate is smaller and concrete: call original
`Fighter_Create`, run its source processes in neutral Wait, unload and restart.
Mario/Fox clip playback and opaque stage inspection remain regression fixtures.

## Foundation available

- Original HSD transforms, skinning, materials, polygons and animation evaluation
  already drive the browser inspector. Aurora supplies GX/WebGPU and PAD.
- The isolated gameplay target owns a real SDK heap and executes original HSD
  GObj allocation, process ordering and lifetime behavior. It intentionally has
  no initialized graphics-object kinds or full fighter/stage lifecycle.
- Checked Wasm ABI patches preserve fighter animation-flag aliases. Packed
  imported command words still require explicit decoding; native bitfield casts
  are not valid just because pointer width matches.
- PlCo root0 has a source-generated scalar schema. The other 22 roots retain
  explicit readiness. Original input reset and walk-threshold logic consume
  scoped typed data without pretending full common initialization has happened.
- Typed collision data feeds original static loading/pruning/island/query code.
  Stage scale and original stage kind are explicit. Joint bindings, callbacks,
  dynamic geometry and fighter ECB/physics remain separate requirements.

## Parallel implementation batches

1. **Common data and named parts.** Hydrate common roots1–5,9–15,18–19,21 as
   owned typed graphs: item/staling tables, named part maps, counted shake vectors,
   scale/status modifiers and palettes/crowd configuration. Existing animation
   binding keeps part counts but does not supply the named maps required by
   `ftParts_80074E58`. Use exact source counts and real null alternates; reject
   unsupported alternate insertion. Root17 has no established source consumer
   and remains unresolved, not guessed from its palette-like bytes.
2. **Original HSD construction and lifetime.** Hydrate common root20 and the
   Mario costume/material-animation graph for original HSD constructors.
   `Fighter_800679B0` unconditionally loads root20 in `ftCo_800C8064` and
   `ftCo_800C8F6C`, and creates stage lighting in `ftCo_8009F4A4`. Establish real
   graphics-kind destruction, class ownership, part setup and texture AObjs.
   Entry/respawn roots8/16 reuse this graph capability. More inspector passes
   are useful only when they unblock this runtime path.
3. **Mario data and action loading.** Decode Mario's ftData, common/special
   attributes, hurtboxes, dynamics, item Article registrations, blend maps and
   weighted Wait choices. Extend the source registry with the actual costume
   material-animation identity. Preserve the explicitly selected source motion
   record even when another record shares its figatree bytes. Implement checked
   packed action/color-command graphs and original execution; Wait's blink must
   reach real texture animation. CPU root22 is a distinct future AI dependency.
4. **Player/stage/services integration.** Supply real player settings, stage
   collision-to-joint bindings, camera/lighting and required service ownership.
   Final Destination's source binding maps collision joint0 to stage entry3,
   render joint0; empty archive binding tables do not remove that requirement.
   Run original fighter processes at 60 Hz and establish deterministic traces.
   Audio and effects that the retained path calls need actual implementations
   or an explicit unsupported result, never successful no-ops.

The loader is a concrete portability blocker: `ftData_80085E50` classifies
addresses below `0x80000000` as ARAM, which includes ordinary Wasm pointers.
It also expects in-place big-endian archive relocation. Replace that storage
classification with checked owned decoded-clip identity while preserving source
record, cache and completion semantics. Do not fake ARAM transfers or bias
pointer values. Reuse the existing FigaTree decoder and original evaluator.

## Integration checks

```sh
python3 scripts/build.py --target gameplay
python3 scripts/check_gameplay.py
python3 scripts/check_gameplay.py --common assets-local/PlCo.dat --stage assets-local/GrNLa.dat --stage-kind 37
```

These run Wasm foundation checks with the pinned local Node runtime. Optional
inputs remain local; `37` is original `GrKind`, not viewer entry3. The output
must continue to distinguish decoded data, initialized subsystems and a running
match. Source compilation counts are diagnostic coverage only.

Accept creation only after allocation→creation→neutral ticks→unload→restart
works with owned data and no half-ready fighter publication. Then advance the
same runtime through walk/dash/jump/land, attacks and damage, and stock-match
completion. Compare original-game state traces before accuracy claims, and use
release measurements on named reference devices before a 60 fps claim.
