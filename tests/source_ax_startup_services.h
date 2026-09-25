#ifndef MELEE_WEB_SOURCE_AX_STARTUP_SERVICES_H
#define MELEE_WEB_SOURCE_AX_STARTUP_SERVICES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Fixture-only entry point.  It executes the pinned AIInit and AXInit source
 * bodies and returns only after the original AXOut path has enabled AI DMA. */
int melee_web_source_ax_startup_run(const char* mode);

#ifdef __cplusplus
}
#endif
#endif
