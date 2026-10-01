#include "gameplay_source_alarm.h"

#include "gameplay_platform.h"
#include <dolphin/os/OSAlarm.h>
#include <dolphin/os.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

static int owner_active;
static OSTime source_time;
static OSAlarm* periodic_alarm;

static void unavailable(const char* operation)
{
    melee_web_platform_unavailable(operation);
}

int melee_web_source_alarm_begin(void)
{
    if (owner_active || periodic_alarm != NULL) return 0;
    owner_active = 1;
    source_time = 0;
    return 1;
}

int melee_web_source_alarm_tick(void)
{
    if (!owner_active) return 1;
    /* Match lbMthp's authored alarm interval exactly. Integer milliseconds
     * lose fractional source ticks and slowly drift from the movie cadence. */
    const OSTime frame = OSSecondsToTicks(1.0F / 60.0F);
    if (frame <= 0 || source_time > INT64_MAX - frame) return 0;
    source_time += frame;

    OSAlarm* alarm = periodic_alarm;
    if (alarm == NULL || alarm->handler == NULL || alarm->fire > source_time)
        return 1;
    if (alarm->period <= 0 || alarm->fire > INT64_MAX - alarm->period)
        return 0;

    /* Advance before callback entry so retail cancellation observes a live
     * deadline, as it does when the OS alarm interrupt dispatches. */
    alarm->fire += alarm->period;
    OSAlarmHandler handler = alarm->handler;
    handler(alarm, NULL);
    return owner_active;
}

int melee_web_source_alarm_end(void)
{
    if (!owner_active || periodic_alarm != NULL) return 0;
    owner_active = 0;
    source_time = 0;
    return 1;
}

void OSCreateAlarm(OSAlarm* alarm)
{
    if (!owner_active || !alarm || periodic_alarm != NULL)
        unavailable("OSCreateAlarm outside an owned movie alarm scope");
    memset(alarm, 0, sizeof(*alarm));
}

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period,
                         OSAlarmHandler handler)
{
    if (!owner_active || !alarm || !handler || start <= 0 || period <= 0 ||
        periodic_alarm != NULL || alarm->handler != NULL ||
        source_time > INT64_MAX - start)
        unavailable("unsupported or unowned OSSetPeriodicAlarm request");
    alarm->handler = handler;
    alarm->period = period;
    alarm->start = source_time + start;
    alarm->fire = alarm->start;
    periodic_alarm = alarm;
}

void OSCancelAlarm(OSAlarm* alarm)
{
    if (!owner_active || !alarm || periodic_alarm != alarm)
        unavailable("OSCancelAlarm lost movie alarm ownership");
    alarm->handler = NULL;
    alarm->period = 0;
    alarm->fire = 0;
    alarm->start = 0;
    alarm->prev = NULL;
    alarm->next = NULL;
    periodic_alarm = NULL;
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    (void)alarm;
    (void)tick;
    (void)handler;
    unavailable("unsupported one-shot OS alarm request");
}
