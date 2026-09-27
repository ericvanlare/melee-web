#include "gameplay_compat.h"
#include "gameplay_font_atlas.h"
#include <dolphin/mtx.h>
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/lb/lblanguage.h>
#include <sysdolphin/baselib/sislib.h>
#include <string.h>
#include <stdio.h>

static int sis_mapped_glyphs(const u8* encoded, int length)
{
    int cursor = 0;
    int glyphs = 0;
    while (cursor < length) {
        if (encoded[cursor] == 0x0B) {
            cursor += 1;
        } else if (encoded[cursor] == 0x0A) {
            cursor += 5;
        } else if (encoded[cursor] >= 0x20 && cursor + 1 < length) {
            glyphs += 1;
            cursor += 2;
        } else {
            return -1;
        }
    }
    return glyphs;
}

int melee_web_test_player_name_sis_encoding(void)
{
    static const char* valid_tags[] = { "ALFA", "BETA" };
    static const u8 mario_sjis[] = {
        0x82, 0x6C, 0x82, 0x81, 0x82, 0x92, 0x82, 0x89, 0x82, 0x8F, 0,
    };
    static const u8 mario_utf8[] = {
        /* Bytes produced for the original full-width literal under UTF-8. */
        0xEF, 0xBC, 0xAD, 0xEF, 0xBD, 0x81, 0xEF, 0xBD, 0x92,
        0xEF, 0xBD, 0x89, 0xEF, 0xBD, 0x8F, 0,
    };
    static const u8 mario_jp_sjis[] = {
        0x83, 0x7D, 0x83, 0x8A, 0x83, 0x49, 0,
    };
    static u8 atlas[MELEE_WEB_FONT_ATLAS_BYTES];
    char error[256];
    struct PlayerInitData defaults;
    MeleeWebFontAtlas* owner = melee_web_font_atlas_register(
        atlas, sizeof(atlas), error, sizeof(error));
    if (owner == NULL) {
        fprintf(stderr, "%s\n", error);
        return 0;
    }
    lbLang_SetLanguageSetting(LANG_US);
    lbLang_SetSavedLanguage(LANG_US);
    gm_SetupPlayerDefaults(&defaults);
    if (defaults.nametag != GM_NAMETAG_NONE)
        goto fail;
    {
        u8 first[128], second[128];
        const int first_length = HSD_SisLib_803A67EC(first, (u8*) valid_tags[0]);
        const int second_length = HSD_SisLib_803A67EC(second, (u8*) valid_tags[1]);
        if (first_length <= 0 || second_length <= 0 ||
            (first_length == second_length &&
             memcmp(first, second, (size_t) first_length) == 0))
            goto fail;
    }
    for (u8 kind = 0; kind < CKIND_PLAYABLE_COUNT; ++kind) {
        const char* name = gm_80160980(kind);
        u8 encoded[128];
        int length;
        int glyphs;
        if (name == NULL || name[0] == '\0')
            goto fail;
        if (kind == CKIND_MARIO &&
            memcmp(name, mario_sjis, sizeof(mario_sjis)) != 0)
            goto fail;
        length = HSD_SisLib_803A67EC(encoded, (u8*) name);
        glyphs = sis_mapped_glyphs(encoded, length);
        if (glyphs <= 0 || (kind == CKIND_MARIO && glyphs != 5))
            goto fail;
        if (kind == CKIND_MARIO) {
            u8 malformed[128];
            const int malformed_length =
                HSD_SisLib_803A67EC(malformed, (u8*) mario_utf8);
            if (sis_mapped_glyphs(malformed, malformed_length) != 0)
                goto fail;
        }
    }
    lbLang_SetLanguageSetting(LANG_JP);
    lbLang_SetSavedLanguage(LANG_JP);
    {
        const char* name = gm_80160980(CKIND_MARIO);
        u8 encoded[128];
        if (name == NULL || memcmp(name, mario_jp_sjis, sizeof(mario_jp_sjis)) != 0)
            goto fail;
        const int length = HSD_SisLib_803A67EC(encoded, (u8*) name);
        if (sis_mapped_glyphs(encoded, length) != 3)
            goto fail;
    }
    lbLang_SetLanguageSetting(LANG_US);
    lbLang_SetSavedLanguage(LANG_US);
    if (!melee_web_font_atlas_close(owner, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 0;
    }
    return 1;
fail:
    lbLang_SetLanguageSetting(LANG_US);
    lbLang_SetSavedLanguage(LANG_US);
    melee_web_font_atlas_close(owner, NULL, 0);
    fprintf(stderr, "U.S. fighter name bytes did not map through original SIS glyph lookup\n");
    return 0;
}

/* Original SIS interpreter and style stack, including values whose byte order
 * matters. The font decoder test separately exercises immutable input/bounds. */
int melee_web_test_sis_consume(void* descriptor, unsigned count)
{
    HSD_Text text = {0};
    float width, height;
    u8 scaled[] = {14, 2, 0, 1, 0, 0x20, 0, 15, 0x20, 1, 0};
    u8 spaced[] = {10, 0xff, 0, 1, 0, 0x20, 0, 11, 0x20, 1, 0};
    char pointer_marker;
    HSD_SisLib_803A6048(64*1024);
    if(descriptor){
        HSD_SisLib_803A62A0(0,"SdSlChr.usd","SIS_SelCharData");
        if(HSD_SisLib_804D1124[0]!=descriptor)return 0;
    }
    text.x80.x = text.x80.y = 1;
    text.x6E = 256;
    text.string_buffer = HSD_SisLib_Alloc(text.x6E);
    memset(text.string_buffer,0,text.x6E);
    HSD_SisLib_803A8134(scaled,&text,&width,&height);
    if(width!=96 || height!=32 || text.x80.x!=1 || text.x80.y!=1){fprintf(stderr,"scaled width=%g height=%g scale=%g,%g\n",width,height,text.x80.x,text.x80.y);return 0;}
    HSD_SisLib_803A8134(spaced,&text,&width,&height);
    if(width!=63 || height!=32 || text.x78.x!=0){fprintf(stderr,"spaced width=%g height=%g spacing=%g\n",width,height,text.x78.x);return 0;}
    HSD_SisLib_803A7684(&text,(u8*)&pointer_marker,5);
    if((uintptr_t)HSD_SisLib_803A7F0C(&text,5)!=(uintptr_t)&pointer_marker){fprintf(stderr,"SIS pointer stack round trip failed\n");return 0;}
    /* Every original CSS string is interpreted up to its first line stop by
     * the real layout routine. Its original font/kerning bytes are in use. */
    text.kerning = 1;
    for(unsigned i=2;i<count;i++) {
        void* stream=((void**)descriptor)[i];
        if(stream)HSD_SisLib_803A8134(stream,&text,&width,&height);
    }
    HSD_SisLib_Free(text.string_buffer);
    HSD_SisLib_803A5FBC();
    return 1;
}
