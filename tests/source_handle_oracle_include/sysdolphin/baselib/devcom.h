#ifndef MELEE_WEB_SOURCE_HANDLE_ORACLE_DEVCOM_H
#define MELEE_WEB_SOURCE_HANDLE_ORACLE_DEVCOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*HSD_DevComCallback)(int, int, void*, bool);
int HSD_DevComRequest(int file, uintptr_t src, uintptr_t dest, size_t size,
                      int type, int priority, HSD_DevComCallback callback, void* args);

#endif
