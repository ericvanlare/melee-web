# Pokémon Stadium: auxiliary image-buffer constructor ownership

**Compiled / Source identified / Native traced**

The original `grStadium_801D2D78` constructor was observed across two sequential
lifetimes. Each call made the original 28-byte wrapper request, then
`lb_800121FC` created the 640×406 format-4 image buffer for entry `0x7D3`.
Read-only source-memory observations matched each exact payload, request size,
heap/world identity and allocation generation. Original `HSD_Free` and
immediate GObj removal retired those two leases; source used-pool counts and
tracked lists returned to their prior state. A reused address is distinguished
by generation. The callback was registered but never dispatched.

The Emscripten32 layout gate is backed by actual `grpstadium.c` Release `-O3`
IR from pinned Emscripten 6.0.9. `HSD_ImageDesc` occupies 24 bytes, while the
private wrapper is 32 bytes and the original constructor still allocates only
28 bytes. The compiler emits a 24-byte descriptor clear and a one-byte flag
access at offset 24; it does not access `x1A` or `x1C`. The source declaration's
26-byte prefix bound is only a layout-derived candidate, not the actual write
span. The probe never enlarges the allocation, reads trailing fields, or copies
the private wrapper. Host64 rejects before the constructor call.

The retained 98-file C1 fixture passed one native invocation with two
constructor/remove lifetimes, using the recorded input-union manifest and
pre-run inventory. The trace remained header-only. The post-run supplement
records source and build-artifact identities but no post-run fixture inventory,
so this entry makes no file-by-file unchanged-after-run claim. The run reported
no stage entry or ticks. Numeric allocation generations were asserted
internally and were not printed. The observer and constructor phases are tied to
their separate source producers: observer review at `ea42449`, then the clean
constructor freeze and native fixture at `cf4a167`.

Integration producer `69e44ffc` passed full unittest discovery: 2,031 tests,
132 skipped. A previous full-suite run failed only because the downstream
gameplay patch was not in canonical file order. The documented canonicalizer
reordered the patch hunk and index metadata, leaving generated source bytes and
the pinned upstream checkout unchanged; focused canonicality and ownership
checks then passed. Both ordinary Release builds also passed with the C1 and
other diagnostic opt-ins OFF: the `native_menu_host_trace` target and the
shipped `gameplay_menu_browser` runtime target. These results are from the
recorded `69e44ffc` producer based on `e3220aa`; the queued refresh onto current
main `d8f7600` had not yet been validated when this entry was drafted. The
ordinary Release cache recorded all ten `MELEE_WEB_*` boolean options OFF; its
cache, Ninja graph, targets and outputs are identified in the receipt.

The [portable receipt](../evidence/pokemon-stadium-buffer-consumer-v1.json)
records source, IR, patch, fixture, test, build and output identities. It also
corrects the type of one historical value: the earlier preview field named
`applied_source_tree_sha256` contains a 40-character Git tree ID. This receipt
calls it `applied_source_git_tree`; the frozen preview was preserved unchanged.
The failed suite log and source-preparation records are retained with the
external run evidence.

The source-only next-boundary proposal follows the original display owner and
the actual browser GX implementation. After state 7 clears the 0x7D3 wrapper flag,
grStadium_801D2FD0 calls lb_800122C8(&desc, 0, 36, 0); that helper forwards the
same coordinates and clear value to HSD_ImageDescCopyFromEFB with sync=true.
The actual generated tobj.c preprocessor resolves GX declarations to Aurora's
GXFrameBuffer.h. The ordinary gameplay_menu_browser Wasm links libaurora_gx.a
and contains the HSD helper and Aurora GX copy symbols. Aurora writes custom
source, destination, pointer, and BP 0x52 commands into its buffered FIFO; its
command processor routes the texture-copy trigger to copy_tex, which resolves
the current render pass into a texture indexed by the destination pointer. The
old Dolphin direct GXWGFifo copy producer is not part of this target.

This establishes an existing source/link service, not a copy result. FIFO publish
requires an active Aurora frame; Emscripten processes it inline on this non-pthreads
profile, and Aurora end_frame drains commands and finishes the graphics packet.
GXPixModeSync writes PE state but does not prove GPU completion, while Aurora's
GXInvalidateTexAll is empty. The next step remains source-only: trace the supported
draw owner, active render pass, and genuine display-Ground ownership before asking
to invoke one original callback. No bridge, callback, Ground owner, stage entry, or
EFB copy was added or run. The portable receipt records the compile, preprocessed-TU,
archive, symbol, and Wasm identities and these synchronization limits.

This is scoped constructor/lifetime evidence only. It does not establish
`0x7D4`, callback dispatch, Ground `OnInit`, stage entry/admission, ticks or
draw, camera/text, browser output, retail comparison, performance, pixels,
audio, or whole-session acceptance.
