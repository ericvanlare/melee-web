# Public player presentation and resize

## Preserve the original display proportions

The public shell must fit a centered 4:3 canvas into `#runtime-host`, whose
height is the space left after the wrapping toolbar and safe-area padding.
The host is a size-query container; both canvas dimensions use its available
width and height. The toolbar does not shrink. This keeps the complete game
image visible with black bars, including in the player element's DOM
fullscreen layout.

Previously the canvas used `width: 100%; height: 100%`. Initially its 640×480
buffer could be letterboxed by `object-fit: contain`, but a browser resize
made SDL resize that buffer to the unrestricted CSS rectangle. Aurora's game
framebuffer followed the window shape, so the image filled the tall or wide
rectangle. `object-fit` cannot restore 4:3 proportions once the buffer has
that shape. This is a shell sizing defect; the fix does not change the game
projection or renderer.

The browser backing canvas uses the configured 640×480 game pixels. CSS scales
that image into the available 4:3 rectangle. Aurora's Emscripten window omits
automatic high-DPI and resizable flags and explicitly restores the configured
size after SDL discovers the initial CSS box. This avoids multiplying the
render targets by device pixel ratio or reallocating them for toolbar and
viewport changes. Native desktop window behavior is unchanged.

SDL's pointer handler still scales canvas-relative coordinates using the CSS
dimensions. Keeping the bars outside the canvas avoids an additional pointer
offset. Touch controls use the same fitted rectangle.

Run the owned-disc check against a locally served production package:

```sh
node tests/public_presentation_browser_test.mjs \
  --url "$CANDIDATE_ORIGIN" --disc "$OWNED_DISC" \
  --playwright "$PLAYWRIGHT_PACKAGE" --dpr 1 \
  --out work/presentation-dpr1
```

Repeat with `--dpr 2` or `--dpr 3` and a new output directory. The check uses headless
installed Chrome and the actual public shell, imports the disc through its
normal controls, and retains original character-select screenshots. It asserts
4:3 geometry, centering, containment, toolbar clearance, absence of scrolling,
backing dimensions, nonempty game imagery, and canvas-relative pointer offsets
across tall, wide, desktop, narrow, short, repeated-resize and DOM fullscreen
states. Screenshots use CSS-pixel resolution to bound capture work;
the actual game backing buffer remains 640×480. The
[presentation receipt](evidence/public-presentation-aspect-v1.json) records the
earlier aspect-ratio correction and its historical DPR-sized buffers.

The smaller `tests/browser_surface_resolution_test.mjs` checks the real public
WebGPU startup at DPR 1, 2 and 3, with portrait, landscape and desktop viewport
changes, without importing a disc. Pass `--url`, `--out`, `--playwright` and,
optionally, `--manifest` as above.

This is **Browser exercised** presentation evidence. OS fullscreen/focus,
foreground timing and original-game pixel equivalence remain separate gates;
headless DOM fullscreen does not establish them. Owned disc data and game
screenshots remain in ignored local evidence directories.

## Acquired WebGPU presentation bounds

A browser resize during graphics preparation could stop the public player.
The canvas had changed from 640×480 to 320×607, but Aurora still encoded the
EFB presentation scissor using the previous 640×480 surface configuration.
WebGPU rejected the rectangle because it extended beyond the acquired texture.
This can happen before SDL delivers its corresponding resize event.

The downstream Aurora patch now queries the acquired texture's dimensions
inside the presentation callback. It calculates the presentation viewport from
those dimensions and the captured source texture size, and uses the same
attachment dimensions for the scissor and ImGui viewport. This defensive bound
remains in place with the fixed browser surface policy above. The change does
not alter game simulation, source EFB drawing, pipeline descriptors, or the
pinned upstream revision.

The historical acquired-surface receipt below exercised actual backing-store
changes, including startup and an open Controls dialog. Its old frozen candidate
failed on the first narrow resize. The corrected candidate passed 14 observations,
13 before graphics preparation finished. Those results describe that earlier
resizable-surface policy.

`tests/public_resize_browser_test.mjs` now checks that CSS resize preserves the
configured backing pixels during graphics preparation and with Controls open.
It uses headless installed Chrome by default. The separate surface-resolution
matrix above covers multiple device pixel ratios.

```sh
node tests/public_resize_browser_test.mjs \
  --url "$CANDIDATE_ORIGIN" --playwright "$PLAYWRIGHT_PACKAGE" \
  --out work/resize-validation
```

These are presentation and interface checks, not gameplay accuracy or
performance acceptance. Local failed attempts and their original artifact
inventories remain preserved. The bounded receipt is
[`acquired-surface-bounds-v1.json`](evidence/acquired-surface-bounds-v1.json).
