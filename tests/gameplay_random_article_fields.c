#include <melee/it/types.h>
#include "gameplay_article_data.h"

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
