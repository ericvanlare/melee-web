#ifndef MELEE_WEB_GAMEPLAY_RENDER_H
#define MELEE_WEB_GAMEPLAY_RENDER_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebRender MeleeWebRender;
typedef struct MeleeWebRenderSettings {
    uint32_t width,height;
    float eye[3],interest[3],vertical_fov,near_plane,far_plane;
    uint64_t gx_links; /* Source GX-link bits; Fighter_Create installs link5. */
} MeleeWebRenderSettings;
/* Explicit camera bridge, not the original match camera controller. Begin
 * after match initialization. Original GX-link/Fighter/HSD callbacks render
 * within the caller's Aurora frame, including source material/reflection code.
 * Native descriptor owners automatically supply proven indexed-array bounds. */
MeleeWebRender* melee_web_render_begin(const MeleeWebRenderSettings*,char*,size_t);
/* Original standard/fixed match camera initialization and scheduled controller. */
MeleeWebRender* melee_web_render_begin_match(const MeleeWebRenderSettings*,char*,size_t);
/* Enable the camera's original complete draw callback after full stage setup. */
int melee_web_render_use_match_passes(MeleeWebRender*,char*,size_t);
/* Use original top-level camera ordering, including the match HUD, magnifiers
 * and nametags. Call only after those original scene owners have started. */
int melee_web_render_use_scene_cameras(MeleeWebRender*,char*,size_t);
int melee_web_render_update(MeleeWebRender*,const MeleeWebRenderSettings*,char*,size_t);
int melee_web_render_draw(MeleeWebRender*,char*,size_t);
/* Call after Aurora submits the last frame, before match/world teardown. */
int melee_web_render_end(MeleeWebRender*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
