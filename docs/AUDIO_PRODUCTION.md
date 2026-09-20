# Production audio player release

The operator has authorized a production audio candidate built from the
replacement audio implementation on the current main line, with PR #42 and
PR #49 integrated and PR #44 excluded. This page defines the package and
promotion boundary; it does not record a completed audit or deployment. Record
the resulting package, HTTP, browser and deployment results in the release PR
or receipt.

The bounded [PR #42 audio evidence](evidence/audio-main-integration-v1.json)
records browser-exercised audio through CSS/SSS, Mario/Final Destination,
pause, No Contest and a second match entry. The [replacement implementation
record](AUDIO_REPLACEMENT_EVIDENCE.md) preserves the coefficient-table and
historical Dolphin provenance. These records support the candidate's starting
point; they do not establish full-match performance, original PCM or hardware
fidelity, or licensing clearance.

## Source-bound package

Build the separate Release producer and use the production wrapper. Keep the
output and sidecar manifest in fresh ignored paths; the manifest stays outside
the directory that will be uploaded.

```sh
python3 scripts/build.py --target runtime-audio-preview --configuration Release
python3 scripts/release_audio_player.py prepare \
  --output work/audio-production/site \
  --manifest work/audio-production/site.manifest.json
python3 scripts/release_audio_player.py audit \
  --output work/audio-production/site \
  --manifest work/audio-production/site.manifest.json
```

`release_audio_player.py` reuses the source-bound `runtime-audio-preview`
producer and the reviewed audio-preview package functions with
`production=True`. Its package identity is deliberately distinct from both
the staging preview and the silent rollback:

- schema: `melee-web-audio-player-package-v1`
- profile: `audio-player`
- project: `webmelee`
- native producer: `runtime-audio-preview` in Release, identified by
  `build/runtime-audio-preview-identity.json`

The manifest records the source commit, runtime graph hash, producer identity
hash and complete file inventory. The upload contains only the audited site;
it does not contain the source checkout, build sidecar, disc image, extracted
audio, coefficient binary or diagnostic routes. The default
`stage_audio_preview.py` entry point remains staging-only, and the legacy
`runtime-public`/`build_public.py` path remains the silent rollback identity.

The generated notices describe the replacement implementations and retain the
coefficient table's historical Dolphin revision and GPL text. That provenance
notice is required for the artifact and is not a legal-clearance conclusion or
a new source-distribution promise.

## Stage, verify and promote

Deploy the audited directory to the `webmelee-staging` Pages project on its
`staging` branch, then verify the immutable URL with the same sidecar manifest.
The package identity remains `project: webmelee`; the separate staging project
is a hosting preflight boundary.

```sh
WRANGLER_SEND_METRICS=false wrangler pages deploy \
  work/audio-production/site --project-name webmelee-staging --branch staging
python3 scripts/release_audio_player.py verify \
  --url "$AUDIO_STAGING_ORIGIN" \
  --manifest work/audio-production/site.manifest.json \
  --report work/audio-production/staging-http.json
```

Run both browser checks against that immutable staging origin with an owned
disc. The public-player check covers its ten UI, network and privacy checks;
the audio-preview check covers the PCM and Mario/Final Destination lifecycle.

```sh
node tests/public_player_browser_test.mjs \
  --url "$AUDIO_STAGING_ORIGIN" --audio --disc "$OWNED_DISC" \
  --playwright "$PLAYWRIGHT_DIR" \
  --out work/audio-production/staging-public-browser
node tests/audio_preview_browser_test.mjs \
  --url "$AUDIO_STAGING_ORIGIN" --disc "$OWNED_DISC" \
  --playwright "$PLAYWRIGHT_DIR" \
  --out work/audio-production/staging-audio-browser
```

After the staging checks complete, promote the exact unchanged `site`
directory. Do not rebuild, re-prepare or edit the manifest between the two
uploads. Upload explicitly to the `webmelee` Pages project on branch `main`,
then verify both the returned immutable production origin and the apex against
the same manifest:

```sh
WRANGLER_SEND_METRICS=false wrangler pages deploy \
  work/audio-production/site --project-name webmelee --branch main
python3 scripts/release_audio_player.py verify \
  --url "$AUDIO_PRODUCTION_IMMUTABLE_ORIGIN" \
  --manifest work/audio-production/site.manifest.json \
  --report work/audio-production/production-immutable-http.json
python3 scripts/release_audio_player.py verify \
  --url https://webmelee.gg \
  --manifest work/audio-production/site.manifest.json \
  --report work/audio-production/apex-http.json
```

Run the same two browser checks on both production origins:

```sh
node tests/public_player_browser_test.mjs \
  --url "$AUDIO_PRODUCTION_IMMUTABLE_ORIGIN" --audio --disc "$OWNED_DISC" \
  --playwright "$PLAYWRIGHT_DIR" \
  --out work/audio-production/production-immutable-public-browser
node tests/audio_preview_browser_test.mjs \
  --url "$AUDIO_PRODUCTION_IMMUTABLE_ORIGIN" --disc "$OWNED_DISC" \
  --playwright "$PLAYWRIGHT_DIR" \
  --out work/audio-production/production-immutable-audio-browser
node tests/public_player_browser_test.mjs \
  --url https://webmelee.gg --audio --disc "$OWNED_DISC" \
  --playwright "$PLAYWRIGHT_DIR" \
  --out work/audio-production/apex-public-browser
node tests/audio_preview_browser_test.mjs \
  --url https://webmelee.gg --disc "$OWNED_DISC" \
  --playwright "$PLAYWRIGHT_DIR" \
  --out work/audio-production/apex-audio-browser
```

Use the real browser audio check for the declared scenario in the PR #42
evidence and record the package, HTTP, browser and deployment results in the
release PR or receipt. Keep the last verified silent deployment available for
rollback if any candidate or hosted check fails.
