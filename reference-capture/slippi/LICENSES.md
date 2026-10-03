# Local Slippi reference testbed licenses

This directory contains two separately licensed parts. It does not grant rights
to the Melee game, disc image, extracted game data, or any generated binary.

## Project-authored local service

The source files in `local_matchmaker/`, the process/profile/runtime helpers,
the local scenario runner, its focused Python tests, and the client lock
manifest are project-authored and covered by the root MIT license, as
enumerated in [`LICENSE_SCOPE.md`](../../LICENSE_SCOPE.md). The service
compiles against the exact ENet submodule and vendored nlohmann JSON header
pinned by [`client.lock.json`](client.lock.json). ENet and nlohmann JSON retain their MIT
notices from the pinned Dolphin source tree; this repository does not copy
either dependency into the service directory.

## Downstream client patches

`patches/0001-client-loopback-and-observation.patch` modifies Dolphin files from
`project-slippi/dolphin` at the commit recorded in `client.lock.json`. Those
source files declare GPL-2.0-or-later; the patch retains those terms.

`patches/0002-rust-local-endpoints.patch` modifies
`project-slippi/slippi-rust-extensions` at the pinned commit. That source tree
is GPL-2.0; the patch retains those terms. Apply it only to that exact clean
submodule revision.

`patches/0003-desktop-rollback-diagnostic.patch` adds an opt-in diagnostic to
the same pinned Dolphin source, after patch 0001. Its new client source and
changes retain GPL-2.0-or-later. The diagnostic's authored Python configuration,
runner and exact replay comparator have separate root MIT scope; that grant
does not include the combined native client.

`patches/0004-desktop-rollback-duplicate-role2.native.patch`,
`patches/0005-duplicate-receive-join.native.patch` and
`patches/0006-jitter-reorder-native.patch` extend that same opt-in native
boundary with duplicate, receiver-arrival, jitter and reorder observations.
Their changed Dolphin files retain GPL-2.0-or-later. The authored
`transport_fault_recipes.py` validator and its focused tests remain under the
root MIT project-file scope; they do not grant rights to the combined client.

The project-authored `generate_service_lineage.py` helper and the sanitized
desktop transport evidence under `docs/evidence/` are also MIT-scoped project
files. They record identities and bounded observations; they do not relicense
the adapted client, game data, or retained native binaries.

The patches are separated from the MIT matchmaking service. No client patch is
an MIT grant for the Dolphin or Rust client source. Build and distribute a
combined client only under the applicable upstream terms and with their required
notices and corresponding source.

The full pinned license texts are retained in [`licenses/`](licenses/): the
Dolphin GPL-2.0-or-later text comes from its `LICENSES/GPL-2.0-or-later.txt`,
and the Slippi Rust Extensions GPL-2.0 text comes from that submodule's
`LICENSE` file. These notices are outside the root MIT file scope.

## Game modifications and local inputs

The pinned Slippi ASM repository's license remains authoritative for its
generated `GALE01r2.ini`. The lock file records its source commit and hash; the
generated file is GPL-3.0-only material, copied only into private runtime profiles,
and is not committed here. Disc images, extracted assets, saves, user
identities, replay files, profiles, logs, and client binaries remain local test
inputs or evidence and are not covered by this repository's MIT grant.
