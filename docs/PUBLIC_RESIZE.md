# Acquired WebGPU presentation bounds

A browser resize during graphics preparation could stop the public player.
The canvas had changed from 640×480 to 320×607, but Aurora still encoded the
EFB presentation scissor using the previous 640×480 surface configuration.
WebGPU rejected the rectangle because it extended beyond the acquired texture.
This can happen before SDL delivers its corresponding resize event.

The downstream Aurora patch now queries the acquired texture's dimensions
inside the presentation callback. It calculates the presentation viewport from
those dimensions and the captured source texture size, and uses the same
attachment dimensions for the scissor and ImGui viewport. Ordinary framebuffer
resizing and aspect-preserving presentation remain enabled. The change does
not alter game simulation, source EFB drawing, pipeline descriptors, or the
pinned upstream revision.

`tests/public_resize_browser_test.mjs` exercises actual backing-store changes
in a visible browser, including startup and an open Controls dialog. The old
frozen candidate fails on the first narrow resize. The corrected candidate
passes 14 observations, 13 before graphics preparation finishes. The full
shared-controller browser test also passes with no browser errors.

```sh
node tests/public_resize_browser_test.mjs \
  --url "$CANDIDATE_ORIGIN" --playwright "$PLAYWRIGHT_PACKAGE" \
  --out work/resize-validation
```

These are presentation and interface checks, not gameplay accuracy or
performance acceptance. Local failed attempts and their original artifact
inventories remain preserved. The bounded receipt is
[`acquired-surface-bounds-v1.json`](evidence/acquired-surface-bounds-v1.json).
