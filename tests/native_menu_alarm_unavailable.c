#include <dolphin/os/OSAlarm.h>
#include <stdio.h>
#include <stdlib.h>
/* Explicit limit of the neutral owned-assets diagnostic. Never synthesize a
 * successful scene-preload completion when no provider ran the operation. */
void OSCreateAlarm(OSAlarm* alarm){(void)alarm;fputs("Menu diagnostic reached unsupported scene-preload alarm\n",stderr);abort();}
void OSSetAlarm(OSAlarm* alarm,OSTime tick,OSAlarmHandler handler){(void)alarm;(void)tick;(void)handler;fputs("Menu diagnostic reached unsupported scene-preload alarm\n",stderr);abort();}
