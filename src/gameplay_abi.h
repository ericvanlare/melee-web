#ifndef MELEE_WEB_GAMEPLAY_ABI_H
#define MELEE_WEB_GAMEPLAY_ABI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Verify the reached Fighter animation-flag union against original numeric
 * bit positions. The implementation also asserts selected Wasm32 gameplay
 * struct sizes/offsets at compile time. This is not a serialized-data decoder
 * or a guarantee that other original bitfield overlays are portable. */
int melee_web_gameplay_check_fighter_flags(char* error, size_t error_size);

/* Original CmdUnion members are serialized bitfield overlays. A false result
 * forbids interpreting imported command words through those native structs;
 * command execution needs an explicit decoder or a separately verified port. */
int melee_web_gameplay_native_command_overlay_compatible(void);

#ifdef __cplusplus
}
#endif
#endif
