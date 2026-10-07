# Pokémon Stadium yakumono scalar owner

**Compiled / Source identified / Native traced** for the exact scalar prefix
of the retained English `GrPs.usd` `yakumono_param` source object. The
[portable receipt](../evidence/pokemon-stadium-yakumono-scalar-owner-v1.json)
records the decoder, one retained-input probe, full-suite and Release-build
identities, and exclusions.

The pinned source declares seven signed 32-bit values, three RGB bytes, ten
unsigned 32-bit values, and five signed 16-bit values. The arena-owned native
object is exactly `0x54` bytes, with compile-time size and member-offset
assertions; its two explicit padding fields are zeroed and are not read from
the DAT. This matches the source object extent. The broader `0x70` public
symbol interval recorded by the prior C0 receipt is not used as the object
size.

One opt-in harness probe used the already retained `GrPs.usd` input
(`1,461,024` bytes, SHA256
`aa740cfbbeced294f058449caca8ac0380532521dde06ca4a6dcc03080cf6a6d`). It
resolved the public root at data offset `252776`, checked the next authored
target at `+0x54`, and called `melee_web_stadium_yakumono_decode` through two
sequential, independent `NativeDatArena` owners. All 22 scalar values and RGB
matched the existing C0 source facts. A guarded reader checked the exact field
read sequence and rejected access at or beyond `root + 0x54`. After each owner
lifetime, the raw file and the separately snapshotted resolved DAT data section
were unchanged. The harness explicitly called the decoder; it does not observe
a natural menu handoff. The same executable also passed the synthetic short-target
rejection-before-allocation and allocation-failure/independent-arena-recovery
checks.

The full Python suite passed with 1,964 tests: 1,828 passed and 136 skipped.
The one-shot retained-input test is explicitly opt-in and was skipped during
the suite so the real-data probe was not repeated. The Release
`gameplay_menu_browser` runtime compiled the new decoder into
`fighter_source_runtime`; the runtime was not executed.

This result covers one arena-owned scalar decoder and the stated source
object. It does not construct or publish a stage, bind a profile, modify
`StageInfo`, call `Stage_8022524C` or `on_init`, or add admission/content rows.
It does not observe an original menu archive request, source-file service,
stage lifecycle, rendering, collision, E8/publication, browser behavior, or
playable Stadium. The next source-owned dependency remains a separate bounded
task.
