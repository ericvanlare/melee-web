# Core gameplay loop and the first menus

The browser now runs a two-player Mario stock match on Final Destination.
The immediate cycle is direct disc import, first-use rendering preparation,
and complete match/restart verification. The full acceptance milestone remains
open until the checks in [ROADMAP.md](ROADMAP.md) pass, including physical
controllers, audible output, and cold and warm performance.

## Independent work boundaries

1. **Disc import.** The bounded Blob reader handles ISO/GCM/CISO and checked FST
   lookup independently of the runtime's match-specific asset manifest. Validate
   the original executable, keep data local, and retain malformed-input tests.
2. **First-use rendering.** Measure death, respawn, and specials with the existing
   frame, upload, pipeline, and audio diagnostics. Prepare required graphics before
   play through shared HSD/GX paths. CPU pipeline drainage does not prove GPU
   completion. Do not advance a hidden match or change the 60 Hz source clock.
3. **Acceptance.** Exercise stock loss, respawn, outcome and restart with both
   keyboard and physical controllers. Preserve original-game trace comparisons;
   successful rendering or average FPS does not establish gameplay equivalence.

After this cycle, build Mario-only character selection → FD-only stage selection
→ stock match → return, initially without the full rules menu. Use shared original
menu and roster interfaces. Falco is the first planned roster expansion after
that flow and the core loop are stable.

## Foundation to reuse

- [Fighter runtime](FIGHTER_RUNTIME.md): common/fighter/costume graphs, native action
  commands, stage collision, items, effects and source process/destructor lifetime.
- Checked archive extern preservation and typed named-section publication.
  Unsupported references and services still fail explicitly.
- Immutable original archive bytes, mutable owned native display lists and
  descriptor-ID cleanup across complete heap restarts.
- [Testing](TESTING.md): source ABI comparisons, focused native tests, real-data
  probes, browser rendering checks and fixed dependency pins.

Keep worker assignments independent and bounded. Use GPT-5.6 Luna xhigh unless
Eric changes that preference; the lead reviews and integrates their work.
