# Working on Melee Web

Goal: full vanilla Melee running accurately and performantly in a desktop browser
through compiled source, without a PowerPC CPU interpreter or JIT.

- Read README.md, STATUS.md and docs/PERFORMANCE_AND_ACCURACY.md before work. Keep
  status backed by observed evidence and use the playbook's scoped evidence labels.
- Before adding or enabling a fighter, read [docs/ADDING_CHARACTERS.md](docs/ADDING_CHARACTERS.md),
  starting with its required checkpoints, failure lookup and handoff template.
  Locate owned inputs and pass real-asset construction before a long replay;
  missing extracted files alone do not mean the owned disc is unavailable.
  Measure source-cursor progress, not renderer CPU usage. Inspect a preparation
  error immediately; never repeat a timed-out run without a changed hypothesis.
  After two experiments at the same boundary yield no new evidence, reduce the
  reproducer or request a bounded review using the guide's evidence packet.
  Verify the starting branch contains the current shared fixes. Derive table
  bounds from authored data; preserve save/music RNG, original arithmetic and
  source draw ordering.
  Reduce the first divergence to a small reproducer before another full replay.
  Recorded-queue state agreement does not establish live timing or performance.
- Follow docs/ACCURACY_CONTRACT.md. The next deliverable is original in-game CSS
  → original SSS → four-stock Mario/FD gameplay → original CSS. Temporary HTML
  menus do not satisfy it. Preserve accuracy; record and resolve existing deviations.
- Make small coherent changes; inspect actual APIs and compiler output before changing code.
- Prioritize runnable milestones and parallelize work across bounded interfaces. Once
  relevant checks pass, integrate; defer cleanup that does not unblock the port.
- Reuse original HSD/game routines for shared behavior. Keep archive decoding separate
  from GPU resource lifetime; use the batch asset checker to identify the next missing
  capability across a corpus. Do not add per-asset exemptions or guessed success paths.
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
- For code changes, use focused boundary checks during iteration. Before integration,
  run `python3 -m unittest discover -s tests -v`, build the affected target and inspect
  diffs. Documentation-only changes require link/path and diff checks; runtime
  builds and the full suite are unnecessary when executable behavior is unchanged.
- A dependency upgrade requires revalidation. Do not reset or overwrite unexplained changes.
- Subagents may own bounded files. The lead integrates, reviews, and verifies their results.
- Use GPT-5.6 Luna with xhigh reasoning for subagents unless the user changes this
  preference. Keep assignments bounded and avoid duplicate audits or idle workers.

The browser has limited playable integration, not an accepted accurate or
tournament-ready port. Refer to docs/ROADMAP.md and docs/ACCURACY_CONTRACT.md
for acceptance criteria before broadening its scope.
