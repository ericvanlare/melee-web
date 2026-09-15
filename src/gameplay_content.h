#ifndef MELEE_WEB_GAMEPLAY_CONTENT_H
#define MELEE_WEB_GAMEPLAY_CONTENT_H

#include "gameplay_compat.h"
#include <melee/ft/forward.h>
#include <melee/gr/forward.h>
#include <stddef.h>

/* Implementation mappings, independent of the original unlock profile.
 * Model/animation filenames and costume identities remain generated from the
 * pinned source in fighter_registry.inc. Adding a row requires runtime and
 * rendered lifecycle validation; a linked source file is not availability. */
typedef struct MeleeWebFighterContent {
    int character_kind, fighter_kind;
    unsigned costumes;
    const char* name;
    const char* effect_archive;
    const char* effect_symbol;
    unsigned effect_bank, effect_count;
    const char* audio_bank;
} MeleeWebFighterContent;

static inline const MeleeWebFighterContent* melee_web_fighter_content(int ckind)
{
    static const MeleeWebFighterContent rows[] = {
        { CKIND_MARIO, FTKIND_MARIO, 5, "Mario", "EfMrData.dat", "effMarioDataTable", 1, 2, "mario.ssm" },
        { CKIND_FOX, FTKIND_FOX, 4, "Fox", "EfFxData.dat", "effFoxDataTable", 3, 6, "fox.ssm" },
        { CKIND_FALCO, FTKIND_FALCO, 4, "Falco", "EfFxData.dat", "effFoxDataTable", 3, 6, "falco.ssm" },
        { CKIND_MARS, FTKIND_MARS, 5, "Marth", "EfMsData.dat", "effMarsDataTable", 16, 2, "mars.ssm" },
        { CKIND_DRMARIO, FTKIND_DRMARIO, 5, "Dr. Mario", "EfMrData.dat", "effMarioDataTable", 1, 2, "drmario.ssm" },
        { CKIND_EMBLEM, FTKIND_EMBLEM, 5, "Roy", "EfFeData.dat", "effEmblemDataTable", 49, 2, "emblem.ssm" },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (rows[i].character_kind == ckind) return &rows[i];
    return NULL;
}

static inline const MeleeWebFighterContent* melee_web_fighter_content_by_kind(int kind)
{
    const int characters[] = { CKIND_MARIO, CKIND_FOX, CKIND_FALCO, CKIND_MARS,
                               CKIND_DRMARIO, CKIND_EMBLEM };
    for (size_t i = 0; i < sizeof(characters) / sizeof(characters[0]); ++i) {
        const MeleeWebFighterContent* row = melee_web_fighter_content(characters[i]);
        if (row->fighter_kind == kind) return row;
    }
    return NULL;
}

typedef struct MeleeWebStageContent {
    int stage_kind, ground_kind;
    const char* name;
    const char* archive;
    const char* music;
    int music_id; /* Authored primary BGM; alternate music selection remains outside this slice. */
    const char* audio_bank;
} MeleeWebStageContent;

static inline const MeleeWebStageContent* melee_web_stage_content(int stkind)
{
    static const MeleeWebStageContent rows[] = {
        { St_Kind_Last, Gr_Kind_Last, "Final Destination", "GrNLa.dat", "sp_end.hps", 78, NULL },
        { St_Kind_Battle, Gr_Kind_Battle, "Battlefield", "GrNBa.dat", "sp_zako.hps", 81, NULL },
        { St_Kind_Story, Gr_Kind_Story, "Yoshi's Story", "GrSt.dat", "ystory.hps", 96, NULL },
        { St_Kind_OldPupupu, Gr_Kind_OldPupupu, "Dream Land", "GrOp.dat", "greens.hps", 35, "pupupu.ssm" },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (rows[i].stage_kind == stkind) return &rows[i];
    return NULL;
}

static inline const MeleeWebStageContent* melee_web_stage_content_by_ground(int grkind)
{
    const int stages[] = { St_Kind_Last, St_Kind_Battle, St_Kind_Story, St_Kind_OldPupupu };
    for (size_t i = 0; i < sizeof(stages) / sizeof(stages[0]); ++i) {
        const MeleeWebStageContent* row = melee_web_stage_content(stages[i]);
        if (row->ground_kind == grkind) return row;
    }
    return NULL;
}

#endif
