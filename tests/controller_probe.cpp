#include "browser_input.h"
#include <SDL3/SDL.h>
#include <emscripten/emscripten.h>
#include <cstdio>

extern "C" EMSCRIPTEN_KEEPALIVE const char* controller_probe_sample()
{
    SDL_PumpEvents();
    melee_web_input_poll();
    return melee_web_input_message();
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "Controller probe SDL startup: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_CreateWindow("Controller input", 320, 100, 0)) return 2;
    if (!melee_web_input_startup()) return 3;
    melee_web_input_set_activity(1, 1);
    return 0;
}
