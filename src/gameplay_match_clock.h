#ifndef MELEE_WEB_GAMEPLAY_MATCH_CLOCK_H
#define MELEE_WEB_GAMEPLAY_MATCH_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* The source clock is a singleton. MENU and MATCH are mutually exclusive
 * owners; the owner is restored, rather than inferred, at end(). */
enum {
    MELEE_WEB_SOURCE_CLOCK_MENU = 1,
    MELEE_WEB_SOURCE_CLOCK_MATCH = 2,
};

typedef void (*MeleeWebSourceFrame)(void);

/* These functions are implemented in the MELEE_WEB_GAMEPLAY section of
 * melee/gm/gm_1A45.c. The caller owns PAD renewal and lb_80019900 cadence.
 * MATCH pre() requires lb_80019A30(0) to be ready, invokes on_frame, and
 * publishes the original gm_1A45 process mask before the caller runs audio
 * and HSD_GObj_80390CFC(). A changed HSD mask pointer is an explicit failure;
 * the source clock never overwrites another owner during rollback. */
int melee_web_source_clock_begin(int owner);
int melee_web_source_clock_pre(MeleeWebSourceFrame on_frame);
int melee_web_source_clock_post(void);
int melee_web_source_clock_present(void);
int melee_web_source_clock_request(int* request);
int melee_web_source_clock_end(void);

/* Compatibility entry points used by the current native menu host. They
 * retain the existing MENU ordering: scene OnFrame is called by the menu
 * session, then the scheduler runs, then menu_clock_tick updates counters. */
int melee_web_menu_clock_begin(void);
int melee_web_menu_clock_request(int* request);
int melee_web_menu_clock_tick(void);
int melee_web_menu_clock_present(void);
int melee_web_menu_clock_end(void);

/* The browser owns the source cadence record for a nominal 60 Hz match tick.
 * begin() snapshots the complete lb_804329F0 record and rejects a live source
 * alarm; tick() calls the original lb_80019900 after Master PAD renewal and
 * before source pre(); end() restores the snapshot. These APIs do not emulate
 * VI retrace or GameCube PAD sampling hardware. */
int melee_web_source_cadence_begin(void);
int melee_web_source_cadence_tick(void);
int melee_web_source_cadence_end(void);

#ifdef __cplusplus
}
#endif

#endif
