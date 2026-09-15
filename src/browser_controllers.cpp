#include "browser_controllers.h"
#include <dolphin/pad.h>
#include <cstdint>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

namespace {
int32_t samples[4][8]{};
bool installed = false;
EM_JS(int, sample_browser_controllers, (int32_t* output), {
    const owner = Module['meleeControllers'];
    if (!owner) return 0;
    owner.writeSamples(HEAP32, output);
    return 1;
});

BOOL physical_input(u32 port, PADStatus* pad)
{
    if (port >= 4 || !samples[port][0]) return false;
    const auto* input = samples[port];
    *pad = {};
    pad->button = static_cast<u16>(input[1]);
    pad->stickX = static_cast<s8>(input[2]);
    pad->stickY = static_cast<s8>(input[3]);
    pad->substickX = static_cast<s8>(input[4]);
    pad->substickY = static_cast<s8>(input[5]);
    pad->triggerLeft = static_cast<u8>(input[6]);
    pad->triggerRight = static_cast<u8>(input[7]);
    pad->err = PAD_ERR_NONE;
    return true;
}
}

int melee_web_controllers_poll()
{
    if (!sample_browser_controllers(&samples[0][0])) return -1;
    if (!installed) {
        PADSetPhysicalInputProvider(physical_input);
        installed = true;
    }
    int mask = 0;
    for (unsigned i = 0; i < 4; ++i) if (samples[i][0]) mask |= 1U << i;
    return mask;
}

void melee_web_controllers_shutdown()
{
    if (installed) PADSetPhysicalInputProvider(nullptr);
    installed = false;
}
#else
int melee_web_controllers_poll() { return -1; }
void melee_web_controllers_shutdown() {}
#endif
