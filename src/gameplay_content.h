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
    /* effNessDataTable is a model-only table whose authored referenced
     * region ends at +0x60: exactly four 20-byte entries, the fourth a
     * complete null row. The original efSync dispatch reaches bank 10
     * entries 0-2 (gfx ids 10000-10002) from PK Thunder and PSI Magnet;
     * entry 3 keeps its authored index and zero descriptors. */
    /* effPeachDataTable is a model-only table with exactly one authored
     * 20-byte entry (lifetime 10.0, one joint and one anim, both particle
     * roots null). The original efSync dispatch reaches bank 15 entry 0
     * (gfx id 15000, vegetable pull) from ftpeachspeciallw.c; the float
     * sparkle and Toad spore generators (286, 370, 371) live in
     * already-parsed common banks. */
    static const MeleeWebFighterContent rows[] = {
        { CKIND_MARIO, FTKIND_MARIO, 5, "Mario", "EfMrData.dat", "effMarioDataTable", 1, 2, "mario.ssm" },
        { CKIND_FOX, FTKIND_FOX, 4, "Fox", "EfFxData.dat", "effFoxDataTable", 3, 6, "fox.ssm" },
        { CKIND_FALCO, FTKIND_FALCO, 4, "Falco", "EfFxData.dat", "effFoxDataTable", 3, 6, "falco.ssm" },
        { CKIND_MARS, FTKIND_MARS, 5, "Marth", "EfMsData.dat", "effMarsDataTable", 16, 2, "mars.ssm" },
        { CKIND_DRMARIO, FTKIND_DRMARIO, 5, "Dr. Mario", "EfMrData.dat", "effMarioDataTable", 1, 2, "drmario.ssm" },
        { CKIND_EMBLEM, FTKIND_EMBLEM, 5, "Roy", "EfFeData.dat", "effEmblemDataTable", 49, 2, "emblem.ssm" },
        { CKIND_LINK, FTKIND_LINK, 5, "Link", "EfLkData.dat", "effLinkDataTable", 6, 4, "link.ssm" },
        { CKIND_CLINK, FTKIND_CLINK, 5, "Young Link", "EfLkData.dat", "effLinkDataTable", 6, 4, "clink.ssm" },
        { CKIND_CAPTAIN, FTKIND_CAPTAIN, 6, "Captain Falcon", "EfCaData.dat", "effCaptainDataTable", 4, 6, "captain.ssm" },
        { CKIND_GANON, FTKIND_GANON, 5, "Ganondorf", "EfGnData.dat", "effGanonDataTable", 19, 6, "ganon.ssm" },
        { CKIND_LUIGI, FTKIND_LUIGI, 4, "Luigi", "EfLgData.dat", "effLuigiDataTable", 18, 2, "luigi.ssm" },
        { CKIND_PIKACHU, FTKIND_PIKACHU, 4, "Pikachu", "EfPkData.dat", "effPikachuDataTable", 7, 6, "pikachu.ssm" },
        { CKIND_PICHU, FTKIND_PICHU, 4, "Pichu", "EfPkData.dat", "effPikachuDataTable", 7, 6, "pichu.ssm" },
        { CKIND_PURIN, FTKIND_PURIN, 5, "Jigglypuff", "EfPrData.dat", "effPurinDataTable", 11, 1, "purin.ssm" },
        { CKIND_DONKEY, FTKIND_DONKEY, 5, "Donkey Kong", "EfDkData.dat", "effDonkeyDataTable", 8, 7, "dk.ssm" },
        { CKIND_KOOPA, FTKIND_KOOPA, 4, "Bowser", "EfKpData.dat", "effKoopaDataTable", 12, 4, "koopa.ssm" },
        { CKIND_NESS, FTKIND_NESS, 4, "Ness", "EfNsData.dat", "effNessDataTable", 10, 4, "ness.ssm" },
        { CKIND_PEACH, FTKIND_PEACH, 5, "Peach", "EfPeData.dat", "effPeachDataTable", 15, 1, "peach.ssm" },
        { CKIND_MEWTWO, FTKIND_MEWTWO, 4, "Mewtwo", "EfMtData.dat", "effMewtwoDataTable", 13, 4, "mewtwo.ssm" },
        { CKIND_GAMEWATCH, FTKIND_GAMEWATCH, 4, "Mr. Game & Watch", NULL, NULL, 0, 0, "gw.ssm" },
        { CKIND_KIRBY, FTKIND_KIRBY, 6, "Kirby", "EfKbData.dat", "effKirbyDataTable", 5, 9, "kirby.ssm" },
        { CKIND_POPONANA, FTKIND_POPO, 4, "Ice Climbers", "EfIcData.dat", "effIceclimberDataTable", 14, 1, "ice.ssm" },
        { CKIND_SAMUS, FTKIND_SAMUS, 5, "Samus", "EfSsData.dat", "effSamusDataTable", 2, 4, "samus.ssm" },
        { CKIND_YOSHI, FTKIND_YOSHI, 6, "Yoshi", "EfYsData.dat", "effYoshiDataTable", 9, 1, "yoshi.ssm" },
        /* The two selectable transformation forms have distinct FTKinds,
         * effects and results, but share the original zs.ssm sound bank. */
        { CKIND_ZELDA, FTKIND_ZELDA, 5, "Zelda", "EfSsData.dat", "effSamusDataTable", 2, 4, "zs.ssm" },
        { CKIND_SEAK, FTKIND_SEAK, 5, "Sheik", "EfZdData.dat", "effZeldaDataTable", 17, 7, "zs.ssm" },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (rows[i].character_kind == ckind) return &rows[i];
    return NULL;
}

/* A small number of selectable identities publish more than one source
 * FighterKind. Player_80031AD0 creates the extra identity as a second live
 * entity (Ice Climbers) or as the other transformation form (Zelda/Sheik).
 * The source Player mapping remains authoritative for creation and switching;
 * this helper enumerates the asset owners that must be resident first. */
static inline int melee_web_fighter_alternate_kind(int ckind)
{
    switch (ckind) {
    case CKIND_POPONANA: return FTKIND_NANA;
    case CKIND_ZELDA: return FTKIND_SEAK;
    case CKIND_SEAK: return FTKIND_ZELDA;
    default: return FTKIND_NONE;
    }
}

static inline unsigned melee_web_fighter_kind_count(int ckind)
{
    return melee_web_fighter_alternate_kind(ckind) == FTKIND_NONE ? 1U : 2U;
}

static inline int melee_web_fighter_kind_at(int ckind, unsigned index)
{
    const MeleeWebFighterContent* row = melee_web_fighter_content(ckind);
    if (!row || index >= melee_web_fighter_kind_count(ckind)) return FTKIND_NONE;
    return index == 0 ? row->fighter_kind : melee_web_fighter_alternate_kind(ckind);
}

static inline const MeleeWebFighterContent* melee_web_fighter_content_by_kind(int kind)
{
    const int characters[] = { CKIND_MARIO, CKIND_FOX, CKIND_FALCO, CKIND_MARS,
                               CKIND_DRMARIO, CKIND_EMBLEM, CKIND_LINK, CKIND_CLINK, CKIND_CAPTAIN, CKIND_GANON, CKIND_LUIGI,
                               CKIND_PIKACHU, CKIND_PICHU, CKIND_PURIN, CKIND_DONKEY, CKIND_KOOPA, CKIND_NESS,
                               CKIND_PEACH, CKIND_MEWTWO, CKIND_GAMEWATCH, CKIND_KIRBY,
                               CKIND_POPONANA, CKIND_SAMUS, CKIND_YOSHI,
                               CKIND_ZELDA, CKIND_SEAK };
    for (size_t i = 0; i < sizeof(characters) / sizeof(characters[0]); ++i) {
        const MeleeWebFighterContent* row = melee_web_fighter_content(characters[i]);
        if (row->fighter_kind == kind) return row;
    }
    for (size_t i = 0; i < sizeof(characters) / sizeof(characters[0]); ++i) {
        const MeleeWebFighterContent* row = melee_web_fighter_content(characters[i]);
        if (melee_web_fighter_alternate_kind(row->character_kind) == kind) return row;
    }
    return NULL;
}

typedef struct MeleeWebStageContent {
    int stage_kind, ground_kind;
    const char* name;
    const char* archive;
    const char* music;
    int music_id; /* Authored primary BGM; the original stage selector chooses the live track. */
    const char* audio_bank;
} MeleeWebStageContent;

static inline const MeleeWebStageContent* melee_web_stage_content(int stkind)
{
    static const MeleeWebStageContent rows[] = {
        { St_Kind_Last, Gr_Kind_Last, "Final Destination", "GrNLa.dat", "sp_end.hps", 78, NULL },
        { St_Kind_Battle, Gr_Kind_Battle, "Battlefield", "GrNBa.dat", "sp_zako.hps", 81, NULL },
        { St_Kind_Story, Gr_Kind_Story, "Yoshi's Story", "GrSt.dat", "ystory.hps", 96, NULL },
        { St_Kind_OldPupupu, Gr_Kind_OldPupupu, "Dream Land", "GrOp.dat", "old_kb.hps", 58, "pupupu.ssm" },
        { St_Kind_Shrine, Gr_Kind_Shrine, "Hyrule Temple", "GrSh.dat", "shrine.hps", 75, NULL },
        { St_Kind_Izumi, Gr_Kind_Izumi, "Fountain of Dreams", "GrIz.dat", "izumi.hps", 49, NULL },
        { St_Kind_OldYoshi, Gr_Kind_OldYoshi, "Yoshi's Island 64", "GrOy.dat", "old_ys.hps", 59, NULL },
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (rows[i].stage_kind == stkind) return &rows[i];
    return NULL;
}

static inline const MeleeWebStageContent* melee_web_stage_content_by_ground(int grkind)
{
    const int stages[] = { St_Kind_Last, St_Kind_Battle, St_Kind_Story,
                           St_Kind_OldPupupu, St_Kind_Shrine, St_Kind_Izumi, St_Kind_OldYoshi };
    for (size_t i = 0; i < sizeof(stages) / sizeof(stages[0]); ++i) {
        const MeleeWebStageContent* row = melee_web_stage_content(stages[i]);
        if (row->ground_kind == grkind) return row;
    }
    return NULL;
}

#endif
