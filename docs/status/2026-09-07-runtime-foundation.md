# Runtime foundation

- Pinned Melee, Aurora and Emscripten; checked generated gameplay patches preserve
  the original int-bool ABI and packed flag aliases. Pristine dependencies stay intact.
- Actual full common initialization consumes owned supported PlCo roots, native
  material owners and stage lights. Missing roots retain explicit readiness.
- Typed Mario ftData, all action rows, independent source action-loader bindings,
  common attributes, hurtboxes, visibility tables, costume and metal graphs.
  Wait/Fall scripts use decoded native operands; unsupported scripts fail.
- Original item registration consumes the real ItCo.usd registry; Mario effect
  banks, native effect models/animation graphs, item materials and particle
  rendering load from the authored data. Ground/air fireball impact, expiry and
  restart are covered by browser and source checks.
- Final Destination uses real collision geometry, original priority-4 collision
  updates, light overrides, marker transforms, camera/blast bounds, ground
  parameters and full-stage render objects. The browser cycle includes all stage
  transitions and the normal authored player markers.
- Source camera subjects, player settings, PdPm bonus thresholds, original font
  atlas and scoped RNG/PAD state have explicit owners and restart checks.
- Native descriptor owners retain borrowed data through original destruction.
  Metal aliases are freed through the original ID allocator before world reset.
- A bounded owned I/O queue has copy/completion/cancellation tests. It is not yet
  wired into the original archive loader. Unavailable device services abort with
  named errors instead of reporting success.
