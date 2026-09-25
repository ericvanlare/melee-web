#ifndef MELEE_WEB_SOURCE_HANDLE_ORACLE_OSALARM_H
#define MELEE_WEB_SOURCE_HANDLE_ORACLE_OSALARM_H

#include <stdint.h>

typedef int64_t OSTime;
typedef struct OSAlarm { uint8_t bytes[0x28]; } OSAlarm;
typedef struct OSContext OSContext;
typedef void (*OSAlarmHandler)(OSAlarm*, OSContext*);

void OSCreateAlarm(OSAlarm* alarm);
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler);
int OSDisableInterrupts(void);
void OSRestoreInterrupts(int enabled);

#define OSMillisecondsToTicks(msec) ((OSTime)(msec))

#define OSRoundUp32B(value) ((uint32_t)(((uintptr_t)(value) + 31u) & ~31u))

#endif
