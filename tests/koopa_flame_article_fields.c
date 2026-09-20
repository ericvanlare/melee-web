#include <melee/it/types.h>
#include "gameplay_article_data.h"

#include <math.h>
#include <stdint.h>

static int close_float(float actual, float expected)
{
    return fabsf(actual - expected) < 0.00001f;
}

int melee_web_koopa_flame_article_fields_ok(const void* value)
{
    const Article* article = (const Article*)value;
    if (article == 0 || article->x4_specialAttributes == 0 ||
        article->xC_itemStates == 0 || article->x10_modelDesc == 0)
        return 1;

    const float* special = (const float*)article->x4_specialAttributes;
    const float expected[] = {28.0f, 20.0f, 1.9f, 2.2f, 2.1816616f,
                              2.5307274f};
    for (unsigned i = 0; i < 6; ++i)
        if (!close_float(special[i], expected[i]))
            return 2;

    const ItemStateDesc* state = &article->xC_itemStates->x0_itemStateDesc[0];
    if (state->x0_anim_joint != 0 || state->x4_matanim_joint != 0 ||
        state->x8_parameters != 0 || state->xC_script == 0)
        return 3;

    /* This is the source-valid model descriptor form consumed by
     * Item_802680CC: descriptor present, identity JObj requested by a null
     * authored joint, and no bone/attachment/flag metadata. */
    if (article->x10_modelDesc->x0_joint != 0 ||
        article->x10_modelDesc->x4_bone_count != 0 ||
        article->x10_modelDesc->x8_bone_attach_id != 0 ||
        article->x10_modelDesc->xC_bit_field != 0)
        return 4;
    return 0;
}

static int unchanged(const Article* article, void* common, void* special,
                     void* states, void* model, uint32_t unresolved)
{
    return article->x0_common_attr == common &&
           article->x4_specialAttributes == special &&
           article->xC_itemStates == states &&
           article->x10_modelDesc == model &&
           melee_web_article_unresolved(article) == unresolved;
}

int melee_web_koopa_flame_null_form_rejections(
    const MeleeWebNativeDat* reader, void* value)
{
    Article* article = (Article*)value;
    if (reader == 0 || article == 0)
        return 1;
    void* common = article->x0_common_attr;
    void* special = article->x4_specialAttributes;
    void* states = article->xC_itemStates;
    void* model = article->x10_modelDesc;
    uint32_t unresolved = melee_web_article_unresolved(article);
    char error[128];
    MeleeWebItemStateDesc valid = {0, 0, 0, (void*)1};
    MeleeWebItemStateDesc invalid_animation = {(void*)1, 0, 0, (void*)1};

    /* Each rejected structural field is checked independently so one guard
     * cannot hide the others. The real Koopa descriptor's authored flag is
     * checked by fields_ok and is otherwise preserved by publication. */
    if (melee_web_article_publish(reader, article, (void*)1, &valid, 1,
                                  0, 1, 0, 0, 1, error, sizeof(error)) != 0 ||
        !unchanged(article, common, special, states, model, unresolved))
        return 2;
    if (melee_web_article_publish(reader, article, (void*)1, &valid, 1,
                                  0, 0, 1, 0, 1, error, sizeof(error)) != 0 ||
        !unchanged(article, common, special, states, model, unresolved))
        return 3;
    if (melee_web_article_publish(reader, article, (void*)1,
                                  &invalid_animation, 1, 0, 0, 0, 0, 1,
                                  error, sizeof(error)) != 0 ||
        !unchanged(article, common, special, states, model, unresolved))
        return 4;
    return 0;
}
