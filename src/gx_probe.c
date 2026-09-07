/* Original project code. Synthetic geometry; contains no game assets. */
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <stdio.h>
#include <stdlib.h>
#include "hsd_probe.h"
#include "asset_scene.h"
#include "browser_input.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <SDL3/SDL_hints.h>
#endif

static int exiting;
static unsigned int frames;
_Alignas(32) static unsigned char fifo_buffer[64 * 1024];

static void log_message(AuroraLogLevel level, const char* module,
                        const char* message, unsigned int length)
{
    fprintf(level >= LOG_ERROR ? stderr : stdout, "[%s] %.*s\n", module,
            (int)length, message);
    if (level == LOG_FATAL) {
        abort();
    }
}

static void draw(void)
{
    /* GX clip depth is [-w, 0]; Aurora converts this to WebGPU's depth range. */
    const float projection[4][4] = {
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const float model[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    GXSetCopyClear((GXColor){16, 20, 30, 255}, GX_MAX_Z24);
    GXSetViewport(0, 0, 640, 480, 0, 1);
    GXSetScissor(0, 0, 640, 480);
    GXSetProjection((void*)projection, GX_ORTHOGRAPHIC);
    GXLoadPosMtxImm((void*)model, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetCullMode(GX_CULL_NONE);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_TRUE);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX,
                  GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    melee_web_hsd_apply_render_state();
    if (melee_web_asset_draw()) return;
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(-0.7f, -0.6f, -0.5f);
    GXColor4u8(255, 96, 96, 255);
    GXPosition3f32(0.7f, -0.6f, -0.5f);
    GXColor4u8(96, 255, 144, 255);
    GXPosition3f32(0.0f, 0.7f, -0.5f);
    GXColor4u8(96, 144, 255, 255);
    GXEnd();
}

static void tick(void)
{
#ifdef __EMSCRIPTEN__
    const double started = emscripten_get_now();
#endif
    const AuroraEvent* event = aurora_update();
    for (; event && event->type != AURORA_NONE; ++event) {
        if (event->type == AURORA_EXIT) exiting = 1;
    }
    melee_web_input_poll();
#ifdef __EMSCRIPTEN__
    melee_web_asset_tick(started);
#endif
    if (exiting) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        melee_web_input_shutdown();
        aurora_shutdown();
        return;
    }
    if (!aurora_begin_frame()) return;
    draw();
    aurora_end_frame();
    ++frames;
#ifdef __EMSCRIPTEN__
    const double finished = emscripten_get_now();
    EM_ASM({ window.probeFrame($0, $1, $2); }, frames, started, finished - started);
#endif
}

int main(int argc, char** argv)
{
    const AuroraConfig config = {
        .appName = "Melee web feasibility",
        .desiredBackend = BACKEND_WEBGPU,
        .windowWidth = 640,
        .windowHeight = 480,
        .msaa = 1,
        .vsync = true,
        .logCallback = log_message,
        .logLevel = LOG_INFO,
    };
#ifdef __EMSCRIPTEN__
    /* SDL must not consume keyboard events from the surrounding import UI. */
    if (!SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas")) {
        fputs("Unable to bind browser keyboard events to the canvas\n", stderr);
        return 1;
    }
#endif
    aurora_initialize(argc, argv, &config);
    /* Aurora owns transport; GXInit initializes the SDK's shadow registers. */
    GXInit(fifo_buffer, sizeof(fifo_buffer));
    melee_web_input_startup();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(tick, 0, 1);
#else
    while (!exiting) tick();
#endif
    return 0;
}
