#include <melee/it/types.h>
#include "gameplay_article_data.h"

/* Read-only assertions against the original C Article layout.  Keeping this
 * in a C translation unit avoids treating the source's anonymous unions and
 * reserved identifiers as a portable C++ ABI. */
int melee_web_pikachu_article_fields_ok(const void* value, unsigned slot,
                                        int pichu, unsigned rows)
{
    const Article* article = (const Article*) value;
    if (article == 0 || article->x4_specialAttributes == 0 ||
        article->xC_itemStates == 0 || article->x10_modelDesc == 0)
        return 1;

    const float* special = (const float*) article->x4_specialAttributes;
    const float expected = slot == 0 ? (pichu ? 40.0f : 60.0f) :
                           slot == 1 ? 100.0f : 0.0f;
    if (special[0] != expected)
        return 2;

    if (slot == 1) {
        /* TJolt ground's authored model descriptor is present but its joint
         * is null.  The source item constructor explicitly handles that case
         * by creating an identity JObj and uses the descriptor's zero-bone
         * attachment path. */
        if (article->x10_modelDesc->x0_joint != 0 ||
            article->x10_modelDesc->x4_bone_count != 0 ||
            article->x10_modelDesc->x8_bone_attach_id != 0)
            return 3;
    } else if (article->x10_modelDesc->x0_joint == 0) {
        return 4;
    }

    for (unsigned row = 0; row < rows; ++row) {
        const ItemStateDesc* state =
            &article->xC_itemStates->x0_itemStateDesc[row];
        if (slot == 0) {
            if (state->x0_anim_joint != 0 || state->x4_matanim_joint == 0 ||
                state->x8_parameters != 0 || state->xC_script == 0)
                return 5;
        } else if (slot == 1) {
            if (state->x0_anim_joint != 0 || state->x4_matanim_joint != 0 ||
                state->x8_parameters != 0 || state->xC_script == 0)
                return 6;
        } else {
            if (state->x0_anim_joint == 0 || state->x4_matanim_joint == 0 ||
                (pichu ? state->x8_parameters != 0 : state->x8_parameters == 0) ||
                state->xC_script == 0)
                return 7;
        }
    }
    return 0;
}

int melee_web_pikachu_article_null_form_rejected(const MeleeWebNativeDat* reader,
                                                 void* article)
{
    Article* published = (Article*) article;
    void* before_special = published->x4_specialAttributes;
    ItemStateArray* before_states = published->xC_itemStates;
    ItemModelDesc* before_model = published->x10_modelDesc;
    uint32_t before_unresolved = melee_web_article_unresolved(article);
    MeleeWebItemStateDesc malformed_fields = {0, 0, 0, (void*) 1};
    MeleeWebItemStateDesc malformed_states = {(void*) 1, 0, 0, (void*) 1};
    char error[128];
    if (melee_web_article_publish(reader, article, (void*) 1, &malformed_fields, 1,
                                  0, 1, 0, 0, 1, error, sizeof(error)) != 0)
        return 1;
    if (melee_web_article_publish(reader, article, (void*) 1, &malformed_fields, 1,
                                  0, 0, 0, 0, 2, error, sizeof(error)) != 0)
        return 1;
    if (melee_web_article_publish(reader, article, (void*) 1, &malformed_states, 1,
                                  0, 0, 0, 0, 1, error, sizeof(error)) != 0)
        return 1;
    if (published->x4_specialAttributes != before_special ||
        published->xC_itemStates != before_states ||
        published->x10_modelDesc != before_model ||
        melee_web_article_unresolved(article) != before_unresolved)
        return 1;
    return 0;
}

typedef struct MissingModelReader {
    const MeleeWebNativeDat* base;
    uint32_t missing_slot;
} MissingModelReader;

static uint32_t missing_model_word(void* context, uint32_t offset)
{
    MissingModelReader* reader = (MissingModelReader*) context;
    return reader->base->word(reader->base->context, offset);
}

static uint16_t missing_model_half(void* context, uint32_t offset)
{
    MissingModelReader* reader = (MissingModelReader*) context;
    return reader->base->half(reader->base->context, offset);
}

static uint8_t missing_model_byte(void* context, uint32_t offset)
{
    MissingModelReader* reader = (MissingModelReader*) context;
    return reader->base->byte(reader->base->context, offset);
}

static uint32_t missing_model_pointer(void* context, uint32_t offset,
                                      size_t size)
{
    MissingModelReader* reader = (MissingModelReader*) context;
    if (offset == reader->missing_slot)
        return UINT32_MAX;
    return reader->base->pointer(reader->base->context, offset, size);
}

static const void* missing_model_region(void* context, uint32_t offset,
                                        size_t size)
{
    MissingModelReader* reader = (MissingModelReader*) context;
    return reader->base->region(reader->base->context, offset, size);
}

static void* missing_model_allocate(void* context, size_t count, size_t width)
{
    MissingModelReader* reader = (MissingModelReader*) context;
    return reader->base->allocate(reader->base->context, count, width);
}

static void missing_model_reject(void* context, const char* message)
{
    MissingModelReader* reader = (MissingModelReader*) context;
    reader->base->reject(reader->base->context, message);
}

int melee_web_pikachu_article_missing_model_rejected(
    const MeleeWebNativeDat* reader, uint32_t root)
{
    if (reader == 0)
        return 1;

    MissingModelReader context = {reader, root + 16};
    MeleeWebNativeDat shadow = *reader;
    shadow.context = &context;
    shadow.word = missing_model_word;
    shadow.half = missing_model_half;
    shadow.byte = missing_model_byte;
    shadow.pointer = missing_model_pointer;
    shadow.region = missing_model_region;
    shadow.allocate = missing_model_allocate;
    shadow.reject = missing_model_reject;

    uint32_t unresolved = 0;
    void* article = melee_web_article_decode(&shadow, root, &unresolved);
    if (article == 0)
        return 1;

    Article* registration = (Article*) article;
    void* before_common = registration->x0_common_attr;
    void* before_special = registration->x4_specialAttributes;
    void* before_hurtbones = registration->x8_hurtbones;
    void* before_states = registration->xC_itemStates;
    void* before_model = registration->x10_modelDesc;
    void* before_dynamics = registration->x14_dynamics;
    const uint32_t before_unresolved = melee_web_article_unresolved(article);
    MeleeWebItemStateDesc states = {0, 0, 0, (void*) 1};
    char error[128];
    const int published = melee_web_article_publish(
        &shadow, article, (void*) 1, &states, 1, 0, 0, 0, 0, 1,
        error, sizeof(error));
    if (published != 0)
        return 1;
    if (registration->x0_common_attr != before_common ||
        registration->x4_specialAttributes != before_special ||
        registration->x8_hurtbones != before_hurtbones ||
        registration->xC_itemStates != before_states ||
        registration->x10_modelDesc != before_model ||
        registration->x14_dynamics != before_dynamics ||
        melee_web_article_unresolved(article) != before_unresolved)
        return 1;
    return 0;
}
