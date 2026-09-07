#ifndef MELEE_WEB_HSD_PROBE_H
#define MELEE_WEB_HSD_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the synthetic GX scene through the original HSD state cache.
 * Caller owns projection, vertices, lighting, TEV setup and frame submission.
 */
void melee_web_hsd_apply_render_state(void);

#ifdef __cplusplus
}
#endif

#endif
