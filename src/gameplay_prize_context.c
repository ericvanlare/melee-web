#include "gameplay_prize_context.h"
#include "gameplay_bootstrap.h"
#include "gameplay_menu_host.h"
#include <melee/gm/gm_1A36.h>
#include <melee/gm/types.h>
#include <melee/if/if_2FD9.h>
#include <melee/if/ifprize.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/lblanguage.h>
#include <melee/ty/toy.h>
#include <melee/ty/tydisplay.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/rumble.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/video.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern PadLibData default_libinfo_data;
extern HSD_PadStatus default_status_data;
extern HSD_RumbleData HSD_Rumble_804C22E0[4];
extern int melee_web_audio_is_active(MeleeWebAudio*);
extern int melee_web_menu_clock_begin(void);
extern int melee_web_menu_clock_tick(void);
extern int melee_web_menu_clock_request(int*);
extern int melee_web_menu_clock_present(void);
extern int melee_web_menu_clock_end(void);
extern int melee_web_prize_source_begin(void);
extern int melee_web_prize_source_end(void);

struct MeleeWebPrizeContext {
    MeleeWebAudio* audio;
    uint64_t generation, audio_generation;
    uint32_t seed, ticks;
    u32* saved_seed;
    PadLibData saved_library;
    HSD_PadStatus saved_game[4], saved_master[4], saved_copy[4];
    HSD_RumbleData saved_rumble[4];
    HSD_PadRumbleListData rumble_lists[12];
    HSD_PadData queue;
    struct un_804A1F48_t saved_payload;
    int saved_language, saved_saved_language;
    int scene_entered, drawing, transition;
};
static MeleeWebPrizeContext* owner;
static int fail(char* e, size_t n, const char* text)
{ if (e && n) snprintf(e, n, "%s", text); return 0; }
static int ok(char* e, size_t n)
{ if (e && n) *e = 0; return 1; }
static int live(const MeleeWebPrizeContext* c, char* e, size_t n)
{
    if (!c || c != owner || !c->generation ||
        c->generation != melee_web_gameplay_generation() ||
        seed_ptr != &c->seed || !melee_web_audio_is_active(c->audio) ||
        c->audio_generation != melee_web_audio_generation(c->audio))
        return fail(e, n, "Original Prize world, RNG or audio ownership changed");
    return 1;
}

MeleeWebPrizeContext* melee_web_prize_context_begin(MeleeWebMenuHost* host,
    uint32_t seed, const MeleeWebPadState* input, MeleeWebAudio* audio,
    char* e, size_t n)
{
    if (!host || !input || owner || !seed_ptr || !melee_web_gameplay_generation() ||
        !melee_web_audio_is_active(audio) || !melee_web_audio_generation(audio) ||
        melee_web_gameplay_stats().objects)
        return fail(e, n, "Prize requires an empty prepared world and active audio"), NULL;
    MeleeWebPrizeContext* c = calloc(1, sizeof(*c));
    if (!c) return fail(e, n, "Cannot allocate original Prize context"), NULL;
    if (!melee_web_prize_source_begin()) {
        free(c); return fail(e, n, "Original Prize source state is already owned"), NULL;
    }
    if (!melee_web_menu_clock_begin()) {
        if (!melee_web_prize_source_end()) abort();
        free(c); return fail(e, n, "Original Prize scene clock is already owned"), NULL;
    }
    c->audio = audio; c->audio_generation = melee_web_audio_generation(audio);
    c->generation = melee_web_gameplay_generation();
    c->seed = seed; c->saved_seed = seed_ptr;
    c->saved_language = lbLang_GetLanguageSetting();
    c->saved_saved_language = lbLang_GetSavedLanguage();
    c->saved_payload = if_Scene_Prize_EnterData;
    c->saved_library = HSD_PadLibData;
    memcpy(c->saved_game, HSD_PadGameStatus, sizeof(c->saved_game));
    memcpy(c->saved_master, HSD_PadMasterStatus, sizeof(c->saved_master));
    memcpy(c->saved_copy, HSD_PadCopyStatus, sizeof(c->saved_copy));
    memcpy(c->saved_rumble, HSD_Rumble_804C22E0, sizeof(c->saved_rumble));
    owner = c; seed_ptr = &c->seed;
    lbLang_SetLanguageSetting(LANG_US); lbLang_SetSavedLanguage(LANG_US);
    HSD_PadLibData = default_libinfo_data;
    HSD_PadLibData.rumble_info = c->saved_library.rumble_info;
    HSD_PadRumbleInit(12, c->rumble_lists);
    HSD_PadLibData.qnum = 1; HSD_PadLibData.queue = &c->queue;
    for (unsigned i = 0; i < 4; ++i)
        HSD_PadGameStatus[i] = HSD_PadMasterStatus[i] = HSD_PadCopyStatus[i] = default_status_data;
    /* Typed history includes the original clamping, edge and repeat policy. */
    melee_web_pad_state_apply(input);
    lbAudioAx_8002835C();
    HSD_ZListInitAllocData();
    /* gm_1A3F preloadState uses 0x4800 for GS_PRIZE_INTERFACE. */
    HSD_SisLib_803A6048(0x4800);
    lb_8001C5A4(); lb_8001D1F4();
    Toy_803127D4(); tyDisplay_8031C8B8();
    if (!melee_web_menu_host_prize_enter(host, e, n)) {
        char close_error[256];
        if (!melee_web_prize_context_end(c, close_error, sizeof(close_error))) abort();
        return NULL;
    }
    ifPrize_Scene_OnEnter(&if_Scene_Prize_EnterData);
    c->scene_entered = 1;
    ok(e, n); return c;
}

int melee_web_prize_context_tick(MeleeWebPrizeContext* c,
    const PADStatus raw[4], char* e, size_t n)
{
    if (!live(c, e, n) || !c->scene_entered || c->drawing || !raw ||
        HSD_PadLibData.queue != &c->queue || HSD_PadLibData.qnum != 1 || HSD_PadLibData.qcount)
        return fail(e, n, "Prize tick requires an idle owned source PAD queue");
    memset(&c->queue, 0, sizeof(c->queue));
    memcpy(c->queue.stat, raw, sizeof(c->queue.stat));
    HSD_PadLibData.qread = HSD_PadLibData.qwrite = 0; HSD_PadLibData.qcount = 1;
    HSD_PadRumbleInterpret(); HSD_PadRenewMasterStatus();
    HSD_PadRenewCopyStatus(); HSD_PadRenewGameStatus();
    if (HSD_PadLibData.qcount) return fail(e, n, "Prize PAD sample was not consumed");
    gm_EvaluateAllControllerInputs(); lbAudioAx_80027DF8();
    if (!melee_web_gameplay_step(e, n)) return 0;
    if (!melee_web_menu_clock_tick() || !melee_web_menu_clock_request(&c->transition))
        return fail(e, n, "Original Prize scene clock lost ownership");
    ++c->ticks; return ok(e, n);
}
int melee_web_prize_context_draw(MeleeWebPrizeContext* c, char* e, size_t n)
{
    if (!live(c, e, n) || !c->scene_entered || c->drawing)
        return fail(e, n, "Prize draw requires an idle live scene");
    if (c->transition == 2) return ok(e, n);
    c->drawing = 1;
    GXRenderModeObj saved = *HSD_VIGetRenderMode();
    *HSD_VIGetRenderMode() = GXNtsc480IntDf;
    GXInvalidateVtxCache(); GXInvalidateTexAll();
    HSD_StartRender(HSD_RP_SCREEN); HSD_StateInvalidate(HSD_STATE_ALL);
    HSD_GObj_80390FC0(); HSD_Init_803755A8();
    *HSD_VIGetRenderMode() = saved; c->drawing = 0;
    if (!melee_web_menu_clock_present()) return fail(e, n, "Prize presentation clock lost ownership");
    return ok(e, n);
}
int melee_web_prize_context_requested(const MeleeWebPrizeContext* c)
{ return c && c == owner ? c->transition : 0; }
uint32_t melee_web_prize_context_random_seed(const MeleeWebPrizeContext* c)
{ return c && c == owner ? c->seed : 0; }
uint32_t melee_web_prize_context_ticks(const MeleeWebPrizeContext* c)
{ return c && c == owner ? c->ticks : 0; }
int melee_web_prize_context_exit(MeleeWebPrizeContext* c, char* e, size_t n)
{
    if (!live(c, e, n) || c->drawing) return fail(e, n, "Prize exit requires an idle live scene");
    if (c->scene_entered) { ifPrize_Scene_OnExit(NULL); c->scene_entered = 0; }
    return ok(e, n);
}
int melee_web_prize_context_exit_ready(void)
{ return owner && !owner->scene_entered && !owner->drawing && owner->transition && live(owner, NULL, 0); }
int melee_web_prize_context_end(MeleeWebPrizeContext* c, char* e, size_t n)
{
    if (!c) return ok(e, n);
    if (!melee_web_prize_context_exit(c, e, n)) return 0;
    if (HSD_GObj_804D781C || HSD_GObj_804D7814 || HSD_GObj_804D7818)
        return fail(e, n, "Prize teardown entered during an active source callback");
    /* Normally the source process has destroyed its three scene objects.
     * Abort/unload must also release them, plus the source SIS objects. */
    HSD_SisLib_803A5FBC();
    for (unsigned link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link)
        while (((HSD_GObj**) HSD_GObj_Entities)[link])
            HSD_GObjPLink_80390228(((HSD_GObj**) HSD_GObj_Entities)[link]);
    Toy_803127D4(); tyDisplay_8031C8B8();
    lb_8001D1F4(); lb_8001C5A4();
    if_Scene_Prize_EnterData = c->saved_payload;
    if (!melee_web_prize_source_end()) return fail(e, n, "Prize source state cannot be restored");
    HSD_PadRumbleRemoveAll();
    for (unsigned i = 0; i < 4; ++i) HSD_PadRumbleOffN(i);
    HSD_PadRumbleInterpret();
    HSD_PadLibData = c->saved_library;
    memcpy(HSD_PadGameStatus, c->saved_game, sizeof(c->saved_game));
    memcpy(HSD_PadMasterStatus, c->saved_master, sizeof(c->saved_master));
    memcpy(HSD_PadCopyStatus, c->saved_copy, sizeof(c->saved_copy));
    memcpy(HSD_Rumble_804C22E0, c->saved_rumble, sizeof(c->saved_rumble));
    for (unsigned i = 0; i < 4; ++i) PADControlMotor(i, PAD_MOTOR_STOP_HARD);
    seed_ptr = c->saved_seed;
    lbLang_SetLanguageSetting(c->saved_language); lbLang_SetSavedLanguage(c->saved_saved_language);
    if (!melee_web_menu_clock_end()) return fail(e, n, "Prize scene clock cannot be restored");
    owner = NULL; free(c); return ok(e, n);
}
