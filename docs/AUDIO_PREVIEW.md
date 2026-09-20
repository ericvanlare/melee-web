# Audio listening preview

The owner requested an audio-enabled staging build of PR #42 for listening
before merge. This separate profile uses the replacement resampler and
coefficient generator described in [the audio evidence](AUDIO_REPLACEMENT_EVIDENCE.md).
It preserves the public player UI and minimal native API, includes music and
effects, and uses the latest checked-in pipeline seed. It is a listening
preview, not a hardware-fidelity or licensing-clearance claim.

The normal `runtime-public`, public packaging auditor and silent staging
wrapper remain unchanged in policy. They do not accept this profile's identity
or manifest. Production audio remains disabled. The audio preview belongs on
a separate branch such as `audio-pr-42` in the existing `webmelee-staging`
Pages project; keep the stable staging alias and `webmelee` project unchanged.
Pages preview URLs are publicly reachable; noindex is not access control.

## Build and package

Use a clean committed source checkout with its own pinned dependencies. The
new build has a separate output directory and always uses Release, public
path redaction and CPU diagnostic guards. It exports the same small lifecycle
API as the silent public build, with no replay or memory diagnostics.

```sh
python3 scripts/build.py --target runtime-audio-preview --configuration Release
python3 scripts/stage_audio_preview.py prepare \
  --output work/audio-staging/site \
  --manifest work/audio-staging/site.manifest.json
python3 scripts/stage_audio_preview.py audit \
  --output work/audio-staging/site \
  --manifest work/audio-staging/site.manifest.json
```

Choose fresh package paths. The producer sidecar is
`build/runtime-audio-preview-identity.json`; it binds source trees, prepared
source, toolchain, seed, compile/link audio inclusion and actual native exports.
The package rechecks those inputs and regenerates its complete expected file
inventory from source. The manifest records the source commit and every file
hash, and stays outside the upload. No disc image, extracted audio, coefficient
binary, diagnostic page, source checkout or build sidecar is uploaded.

The player uses the normal local disc import with a generated coefficient
table, a 32 kHz AudioContext and the existing AudioWorklet. Its toolbar and
notices explicitly describe enabled experimental audio and retained numerical
data provenance. No production template is changed by this transformation.

## Verify and deploy

Before deployment, run focused package/build checks, the full unittest suite,
the affected Release target, and a real browser check through Pages local
serving. Use authenticated Wrangler with metrics disabled. Inspect the existing
staging project, preserve its canonical deployment ID, and upload **only** the
audited `site` directory to preview branch `audio-pr-42`. Do not use the
production branch `staging` for this experiment. Record the returned immutable
URL, source commit and package manifest hash, and verify that the project's
canonical deployment is unchanged.

```sh
python3 scripts/stage_audio_preview.py verify \
  --url "$AUDIO_PREVIEW_ORIGIN" \
  --manifest work/audio-staging/site.manifest.json \
  --report work/audio-staging/hosted-http.json
node tests/audio_preview_browser_test.mjs \
  --url "$AUDIO_PREVIEW_ORIGIN" --disc "$OWNED_DISC" \
  --playwright work/audio-staging/tools/node_modules/playwright \
  --out work/audio-staging/hosted-browser
```

The HTTP check verifies exact bytes, security/isolation headers, noindex and
missing development routes. The browser check observes the real audio graph
and nonzero PCM through original menus and a supported match, then exercises
pause/resume and Eject. It is **Browser exercised** evidence for that short
scenario. It does not establish physical speaker quality, exact original PCM,
controller latency or full-match performance. The owner's listening test is
the purpose of the preview.

Use the current bundled browser tools (the initial check used Playwright
1.62.1 with Chrome 153). Playwright 1.55 stalled AudioWorklet module loading
even in an isolated probe. Preserve that failure separately from player
failures. The short stage-selection keyboard recipe is visually checked using
the retained local `stage-target.png` and `match.png` captures; a phase number
alone does not identify the stage.

Open the supplied preview URL in a desktop WebGPU browser, select an owned
USA 1.02 ISO/GCM/CISO, and press Play. Start with the CSS/SSS music, then a
Mario/Final Destination match. Listen for missing music/effects, clicks,
distortion, looping problems and pause/resume behavior. Keep any captured game
audio or video local.
