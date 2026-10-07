#include <melee/it/types.h>
#include <melee/it/it_3F14.h>
#include "gameplay_article_data.h"
#include "gameplay_random_article_layout.h"

#include <stddef.h>
#include <stdint.h>

int melee_web_test_item_public_data_layout(
    MeleeWebTestItemPublicDataLayout* layout)
{
    if (layout == 0) return 0;
    layout->root_bytes = sizeof(it_804D6D20_t);
    layout->field_offsets[0] = offsetof(it_804D6D20_t, x0);
    layout->field_offsets[1] = offsetof(it_804D6D20_t, x4);
    layout->field_offsets[2] = offsetof(it_804D6D20_t, x8);
    layout->field_offsets[3] = offsetof(it_804D6D20_t, xC);
    layout->field_offsets[4] = offsetof(it_804D6D20_t, x10);
    layout->field_offsets[5] = offsetof(it_804D6D20_t, x14);
    layout->target_bytes[0] = sizeof(ItemCommonData);
    layout->target_bytes[1] = It_Kind_Kuriboh * sizeof(uint32_t);
    layout->target_bytes[2] = (It_PKind_Start - It_Kind_Kuriboh) * sizeof(uint32_t);
    layout->target_bytes[3] = (It_Kind_Old_Kuri - It_PKind_Start) * sizeof(uint32_t);
    layout->target_bytes[4] = sizeof(it_804D6D40_t);
    layout->target_bytes[5] = 2 * sizeof(uint32_t);
    layout->character_article_count = It_PKind_Start - It_Kind_Kuriboh;
    layout->random_article_index = It_PKind_Random - It_Kind_Kuriboh;
    layout->article_root_bytes = sizeof(Article);
    return 1;
}

int melee_web_random_article_ready(void* value)
{
    Article* article = (Article*) value;
    return article == 0 || article->xC_itemStates == 0 ||
           article->x10_modelDesc == 0;
}

int melee_web_random_article_late_attributes_ready(void* value)
{
    Article* article = (Article*) value;
    static ItemAttr source_yaku_attributes;
    if (article == 0 || article->x0_common_attr != 0 ||
        melee_web_article_unresolved(article) != 1)
        return 1;
    /* ityaku.c assigns its authored common attributes immediately before the
     * original item constructor calls melee_web_article_require_ready. */
    article->x0_common_attr = &source_yaku_attributes;
    melee_web_article_require_ready(article);
    return melee_web_article_unresolved(article);
}

/* Compile the original anonymous-union source layout as C, then exercise the
 * exact flexible tail that Ground_801C0800 indexes for ALDYakuAll commands. */
int melee_web_random_article_rows_writable(void* value, uint32_t rows)
{
    Article* article = (Article*) value;
    static int script_marker;
    if (article == 0 || rows < 8 || article->xC_itemStates == 0) return 1;
    for (uint32_t row = 1; row < 8; ++row) {
        ItemStateDesc* state = &article->xC_itemStates->x0_itemStateDesc[row];
        state->xC_script = &script_marker;
        if (state->xC_script != &script_marker) return 2;
    }
    return 0;
}
