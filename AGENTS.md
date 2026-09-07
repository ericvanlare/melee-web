# Working on Melee Web

Goal: full vanilla Melee running accurately and performantly in a desktop browser
through compiled source, without a PowerPC CPU interpreter or JIT.

- Read README.md and STATUS.md before work. Keep status backed by observed evidence.
- Make small coherent changes; inspect actual APIs and compiler output before changing code.
- Keep original upstream checkouts intact. Pin dependencies in dependencies.lock.json;
  store explained, reviewable downstream changes under patches/.
- Never describe a successful compile, synthetic scene, or average FPS as gameplay validation.
- Missing runtime functionality must be explicit. No silent success stubs for audio,
  threads, file loading, graphics, or gameplay. Narrow smoke targets may omit whole systems
  if the omission is documented and unreachable from that target.
- Preserve numerical behavior: no fast-math, speculative precision changes, or frame-rate
  changes to simulation. Compare against a verified original for gameplay changes.
- Keep game images, extracted assets, generated binaries, credentials, and personal paths
  out of tracked files. Local input belongs in ignored assets-local/ or an explicit path.
- Validate the changed boundary with meaningful tests. Use real HTTP for server behavior,
  a real browser for graphics, and original-game traces for simulation correctness.
- Run `python3 -m unittest discover -s tests -v`, build the affected target, inspect diffs.
- A dependency upgrade requires revalidation. Do not reset or overwrite unexplained changes.
- Subagents may own bounded files. The lead integrates, reviews, and verifies their results.

The current probe is a feasibility artifact, not a playable port. Refer to docs/ROADMAP.md
for the acceptance criteria before broadening its scope.
