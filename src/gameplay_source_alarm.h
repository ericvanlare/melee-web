#ifndef MELEE_WEB_GAMEPLAY_SOURCE_ALARM_H
#define MELEE_WEB_GAMEPLAY_SOURCE_ALARM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Retail periodic alarms are supported only inside an entered movie scene.
 * Their deadlines advance at the owned source-frame boundary. */
int melee_web_source_alarm_begin(void);
int melee_web_source_alarm_tick(void);
int melee_web_source_alarm_end(void);

#ifdef __cplusplus
}
#endif

#endif
