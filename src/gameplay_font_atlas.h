#ifndef MELEE_WEB_GAMEPLAY_FONT_ATLAS_H
#define MELEE_WEB_GAMEPLAY_FONT_ATLAS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MELEE_WEB_FONT_GLYPHS 287u
#define MELEE_WEB_FONT_GLYPH_BYTES 512u
#define MELEE_WEB_FONT_ATLAS_BYTES (MELEE_WEB_FONT_GLYPHS * MELEE_WEB_FONT_GLYPH_BYTES)
typedef struct MeleeWebFontAtlas MeleeWebFontAtlas;
/* Copies the original GALE01 font texture bytes unchanged into a 32-byte aligned
 * allocation. No font asset is embedded or synthesized. Only one scope may be
 * active. Keep it alive until all text draws and GPU users of its data finish. */
MeleeWebFontAtlas* melee_web_font_atlas_register(const void*,size_t,char*,size_t);
int melee_web_font_atlas_close(MeleeWebFontAtlas*,char*,size_t);
/* Original font array storage boundary; aborts explicitly if no owner exists. */
void* melee_web_font_atlas_data(void);
#ifdef __cplusplus
}
#endif
#endif
