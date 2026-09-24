#ifndef MELEE_WEB_DAT_ITEM_COMMANDS_H
#define MELEE_WEB_DAT_ITEM_COMMANDS_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* melee_web_item_commands_create(const uint32_t*,size_t);
/* Return an entry inside an allocation while leaving destruction owned by the
 * allocation base.  Source-address ordering can place a subroutine before
 * the requested root, so callers must not free this interior pointer. */
void* melee_web_item_commands_entry(void*,size_t);
void melee_web_item_commands_destroy(void*);
#ifdef __cplusplus
}
#endif
#endif
