#ifndef MELEE_WEB_HSD_INSPECTION_H
#define MELEE_WEB_HSD_INSPECTION_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Authored inspection environment, not a game's scene camera/light rig. */
int melee_web_inspection_begin(const float view[3][4], char* error, size_t size);
int melee_web_inspection_specular(const float model_view[3][4], char* error, size_t size);
/* Checked static camera service for the original HSD material/light code. */
int melee_web_inspection_camera(const float view[3][4], char* error, size_t size);
#ifdef __cplusplus
}
#endif
#endif
