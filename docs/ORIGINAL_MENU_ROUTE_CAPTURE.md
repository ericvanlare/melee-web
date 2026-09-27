# Original menu-route capture

This page records the bounded capture preparation for the original cold-boot
menu route. It does not certify retail behavior or browser menu equivalence.
The generated capture is an allocation diagnostic and is forbidden from
candidate admission.

## Route under investigation

The pinned GALE01r2 source path identifies these ordinary source transitions:

| Boundary | Source evidence | Route input |
| --- | --- | --- |
| Fresh DOL entry | `gmboot.c` starts in `GS_MEMCARD`; `gmopeningmode.c` runs the opening movie and title state | Resolve the ordinary card prompt, allow the opening movie to advance |
| Title to root menu | `gmtitlemode.c` accepts Start and requests `GM_MENU` after its save checks | Start |
| Root menu to Versus | `mnmain.c` selects `SEL_MAIN_VS`, then `SEL_VS_MELEE` | Navigate and confirm twice |
| CSS to its parent menu | `mncharsel.c` recognizes the L+R+Start parent-menu shortcut | L+R+Start |
| Root menu to title | `mnmain.c` maps root-menu cancel to `GM_TITLE` | B / `PAD_CANCEL` |

These are **Source identified** observations from the locked source checkout,
not an observed run of the owned disc. The original-menu driver encodes this
path so it can be checked against a fresh retail boot when the required private
inputs are available.

## Bounded diagnostic driver

`tools/retail_allocation_menu.py` exposes
`render_menu_round_trip_driver()`. The matching runner mode is:

```sh
python3 scripts/capture_allocation_history.py \
  --dolphin /path/to/pinned/Dolphin \
  --disc /path/to/owned/GALE01r2.iso \
  --dol /path/to/extracted/main.dol \
  --template-user /path/to/private/template-user \
  --checkpoint-gc /path/to/private/GC \
  --provenance /path/to/private/provenance.json \
  --output work/original-menu-route-001 \
  --menu-round-trip
```

The runner removes the savestate option and begins at original DOL entry. It
uses the copied template profile and memory-card files, normal controller
pipes, and bounded source scheduler stepping. It does not write guest memory,
registers, save data or RNG. The evidence directory retains the original DOL
and disc identity, the source-file digests for boot/opening/title/main/CSS,
card hashes, controller commands with their pre-command source scene/frame,
and one read-only source-frame record per scheduler return. Frame records
include the observed scene kind, game mode, decoded main-menu selection, CSS
payload and four-port cursor/model state when present, PAD copy state, RNG
pointer/value when the pointer is in MEM1, and the current HPS name bytes and
voice-owner word. Audio bytes are metadata only; they are not PCM evidence.
The verifier rejects a missing or reordered marker, unexpected final scene,
missing scheduler frame, or required input issued in the wrong source scene.

The route trace is a diagnostic, not a complete retail reference bundle. It
does not currently retain original screenshots, PCM, GPU diagnostics or
title/menu asset snapshots. Its `captured_diagnostic` status cannot be used as
gameplay, performance, pixel, audio or candidate-admission evidence.

## Evidence ledger

| Item | State | Evidence or remaining gate |
| --- | --- | --- |
| Pinned source route | **Source identified** | `gmboot.c`, `gmopeningmode.c`, `gmtitlemode.c`, `gm_1A3F.c`, `gmvsmelee.c`, `mnmain.c`, `mncharsel.c` at the locked Melee revision |
| Reproducible route driver | **Compiled** | Focused Python tests compile the generated GDB/Python driver and validate route markers, input scenes, bounds and no guest-state writes |
| Owned retail cold boot | **Not run** | The required owned USA rev1.02 disc/DOL and the private baseline memory card/profile were not present in this worktree or configured Reference Capture environment |
| First retail/browser divergence | **Unknown** | Requires the route capture plus retained browser source-boundary state and screenshots |
| Production CSS-first back/forward lifecycle | **Not implemented or validated** | The current production owner covers CSS/SSS; the CSS parent-menu request closes the menu session. Title and main-menu source-scene ownership is not integrated |
| Repeated teardown and re-entry | **Not run** | Requires production route integration and an owned-asset browser run |

The capture prerequisite is specific: provide the path to the owned
USA revision-1.02 disc image, its matching extracted `main.dol`, and the
private memory-card/template/provenance inputs that describe the initial
profile. Keep those bytes under ignored `assets-local/` or `work/`; never add
them to Git. Once available, use this driver for a source route receipt before
porting title/main-menu ownership. Keep the capture's source identity and
failure logs even if a route boundary stalls.

The production implementation remains CSS-first during this diagnostic work.
No menu history is synthesized and no hidden source ticks are run at browser
startup.
