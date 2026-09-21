# Publication review brief

**Status:** the owner chose the [internal assessment](PUBLICATION_PROVENANCE_ASSESSMENT.md)
without outside outreach. This is an optional, unsent brief for a future review,
not an outstanding publication gate. Current project terms are in
[LICENSE_SCOPE.md](../LICENSE_SCOPE.md).

This is a cover sheet for a qualified legal review of repository publication.
It is not a license grant, blanket clearance, ownership finding, or
authorization to change repository visibility. No external outreach is part of
this request.

## Review packet

Please read the existing technical records together:

- [Public repository checklist](PUBLIC_REPOSITORY_CHECKLIST.md): publication
  surfaces, open owner decisions and final checkpoint work.
- [Source ownership and license inventory](SOURCE_LICENSE_INVENTORY.md):
  proposed project-authored scope, recovered Melee/SDK boundaries, generated
  material, patches and separate tools.
- [Audio coefficient provenance review](AUDIO_COEFFICIENT_REVIEW.md): retained
  numerical material, historical contributions, existing-terms analysis and
  distribution surfaces.
- [Third-party provenance](../THIRD_PARTY.md): dependency, adaptation, notice
  and reference-tool boundaries.

Those documents are the factual packet for this review. They record open
questions and technical provenance; they do not select a root license or close
the rights analysis.

## Questions for the reviewer

Provide a file- and portion-scoped determination for the intended repository,
source and binary/package surfaces:

1. For the proposed project-authored portions, what exact files or portions
   can the project license, on what authority, and with what contributor
   assignment or notice requirements? Identify exclusions rather than applying
   a repository-wide conclusion.
2. For recovered Melee/HSD/SDK source, downstream patches, translations and
   generated declarations/data, which exact paths may be published, which must
   be held or separately attributed, and what notices, source context or other
   conditions apply?
3. For the audio replacement, address the exact retained material identified
   in the packet: `web/dsp-coefficients.mjs`, the generated `dsp_coef.bin`
   output, the filter-design parameters and the 20 retained numerical values.
   Are any of these covered by the historical GPL provenance or another term,
   and what treatment applies to each source/data/output boundary?
4. For historical GPL-derived player sources and the separate Dolphin observer,
   identify the exact files, reachable history and artifacts covered, and the
   notices, source-delivery and corresponding-source requirements for each
   distribution surface. Distinguish repository source publication from a
   distributed binary or hosted package.
5. For a combined repository/player distribution containing covered and
   project-authored material, what exact combined-work, linking, notice and
   corresponding-source obligations apply? Identify any artifact or profile
   that needs separate packaging or must remain outside the intended release.
6. For every unresolved item, state the evidence relied on, the precise
   uncertainty, the action needed before publication, and whether the result is
   publish, publish with specified terms/notices, or hold.

The requested output is a written determination keyed to an explicit file,
portion, data, history or artifact inventory, with terms and obligations per
item. A conclusion that the repository or combined executable is cleared in
the abstract would not answer this review. The owner will make the publication
decision after considering the scoped determination; this brief does not ask
the reviewer to contact upstream contributors or obtain new permissions.
