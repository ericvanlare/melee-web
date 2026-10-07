#ifndef MELEE_WEB_TEST_GAMEPLAY_RANDOM_ARTICLE_LAYOUT_H
#define MELEE_WEB_TEST_GAMEPLAY_RANDOM_ARTICLE_LAYOUT_H

#include <stdint.h>

enum { MELEE_WEB_TEST_ITEM_PUBLIC_FIELDS = 6 };

typedef struct MeleeWebTestItemPublicDataLayout {
    uint32_t root_bytes;
    uint32_t field_offsets[MELEE_WEB_TEST_ITEM_PUBLIC_FIELDS];
    uint32_t target_bytes[MELEE_WEB_TEST_ITEM_PUBLIC_FIELDS];
    uint32_t character_article_count;
    uint32_t random_article_index;
    uint32_t article_root_bytes;
} MeleeWebTestItemPublicDataLayout;

#ifdef __cplusplus
extern "C" {
#endif

int melee_web_test_item_public_data_layout(
    MeleeWebTestItemPublicDataLayout* layout);

#ifdef __cplusplus
}
#endif

#endif
