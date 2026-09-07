#ifndef MELEE_WEB_HSD_HOST_SUPPORT_H
#define MELEE_WEB_HSD_HOST_SUPPORT_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Current live host allocation bytes, including alignment and bookkeeping. */
size_t melee_web_hsd_allocation_bytes(void);
#ifdef __cplusplus
}
#endif
#endif
