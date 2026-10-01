#include "gameplay_source_alarm.h"

#include <dolphin/os/OSAlarm.h>
#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned callbacks;
static OSAlarm* expected_alarm;

void melee_web_platform_unavailable(const char* operation)
{
    fprintf(stderr, "unexpected unsupported platform call: %s\n", operation);
    abort();
}

static void require(int condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "source alarm trace: %s\n", message);
        exit(1);
    }
}

static void alarm_callback(OSAlarm* alarm, OSContext* context)
{
    require(alarm == expected_alarm, "callback alarm ownership changed");
    require(context == NULL, "browser callback invented an interrupt context");
    ++callbacks;
}

int main(void)
{
    OSAlarm alarm = {0};
    const OSTime frame = OSSecondsToTicks(1.0F / 60.0F);

    require(frame > 0, "retail source frame interval is invalid");
    require(melee_web_source_alarm_tick(),
            "an inactive movie alarm owner affected an unrelated source frame");
    require(melee_web_source_alarm_begin(), "movie alarm scope did not begin");
    require(!melee_web_source_alarm_begin(), "second movie alarm owner was accepted");
    expected_alarm = &alarm;
    OSCreateAlarm(&alarm);
    OSSetPeriodicAlarm(&alarm, frame, frame, alarm_callback);
    require(!melee_web_source_alarm_end(), "scope closed with a live alarm");
    require(melee_web_source_alarm_tick(), "first source-frame alarm tick failed");
    require(callbacks == 1, "first callback did not match the retail deadline");
    require(melee_web_source_alarm_tick(), "second source-frame alarm tick failed");
    require(callbacks == 2, "periodic callback did not advance once per source frame");
    OSCancelAlarm(&alarm);
    require(melee_web_source_alarm_tick(), "cancelled alarm scope stopped ticking");
    require(callbacks == 2, "cancelled alarm fired again");
    require(melee_web_source_alarm_end(), "cancelled movie alarm scope did not close");

    puts("Original source-frame periodic alarm ownership and cancellation passed");
    return 0;
}
