#include "gameplay_bootstrap.h"
#include "gameplay_compat.h"
#include <melee/gm/gm_1A36.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lblanguage.h>
#include <sysdolphin/baselib/synth.h>

/* This fixture drives only the original audio startup and menu-bank paths.
 * It deliberately does not enter a menu scene or provide renderer services. */
int melee_web_test_audio_start(void)
{
    lbLang_SetLanguageSetting(1);
    lbLang_SetSavedLanguage(1);
    gm_801A3E88();

    lbAudioAx_8002835C();
    lbAudioAx_8002838C();
    lbAudioAx_80028690();
    return 1;
}

/* Exact mnCharSel_Scene_OnEnter request sequence (mncharsel.c:5340-5344). */
void melee_web_test_audio_request_css_banks(void)
{
    lbAudioAx_80026F2C(0x12);
    lbAudioAx_8002702C(2, 8);
    lbAudioAx_80027168();
}

/* Exact mnCharSel_Scene_OnExit character-bank switch sequence
 * (mncharsel.c:5544-5546), restricted to the authored Mario fixture. */
void melee_web_test_audio_request_mario_bank(void)
{
    lbAudioAx_80026F2C(0x14);
    lbAudioAx_8002702C(4, lbAudioAx_80026E84(CKIND_MARIO));
    lbAudioAx_80027168();
}

void melee_web_test_audio_wait_for_banks(void)
{
    lbAudioAx_80027648();
}

int melee_web_test_audio_pending_loads(void)
{
    return HSD_SynthSFXGetPendingLoadCount();
}

/* This is the original shutdown/cancellation path used by reset and scene
 * teardown. It cancels pending SSM requests through fn_800269AC. */
void melee_web_test_audio_cancel_or_shutdown(void)
{
    lbAudioAx_80027DBC();
}
