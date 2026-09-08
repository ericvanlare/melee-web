#include "browser_input.h"

#include <SDL3/SDL_keyboard.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {
MeleeWebInputSnapshot state{};
const char* initialization_error = "";
const char* configuration_error = "";

void neutral(PADStatus& pad, int error)
{
    pad = {};
    pad.err = static_cast<s8>(error);
}

bool has_input(const PADStatus& pad)
{
    return pad.err == PAD_ERR_NONE && (pad.button || pad.extButton || pad.stickX || pad.stickY ||
        pad.substickX || pad.substickY || pad.triggerLeft || pad.triggerRight || pad.analogA || pad.analogB);
}

void neutral_snapshot()
{
    for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        const bool source = (state.physical_mask & (1U << port)) ||
                            (state.keyboard_requested_mask & (1U << port));
        neutral(state.raw[port], !state.ready ? PAD_ERR_NOT_READY
                                              : source ? PAD_ERR_NONE
                                                       : PAD_ERR_NO_CONTROLLER);
        state.clamped[port] = state.raw[port];
    }
}

void apply_policy()
{
    state.active = state.ready && state.focused && state.visible;
    state.keyboard_requested = (state.keyboard_requested_mask & 1U) != 0;
    state.keyboard_active_mask = state.active
                                     ? state.keyboard_requested_mask & ~state.physical_mask
                                     : 0;
    state.keyboard_active = (state.keyboard_active_mask & 1U) != 0;
    if (!state.ready) return;
    PADBlockInput(!state.active);
    for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port)
        PADSetKeyboardActive(port, (state.keyboard_active_mask & (1U << port)) != 0);
}

const PADKeyButtonBinding p1_default_buttons[] = {
    {SDL_SCANCODE_J, PAD_BUTTON_A}, {SDL_SCANCODE_K, PAD_BUTTON_B},
    {SDL_SCANCODE_U, PAD_BUTTON_X}, {SDL_SCANCODE_I, PAD_BUTTON_Y},
    {SDL_SCANCODE_RETURN, PAD_BUTTON_START}, {SDL_SCANCODE_O, PAD_TRIGGER_Z},
    {SDL_SCANCODE_Q, PAD_TRIGGER_L}, {SDL_SCANCODE_E, PAD_TRIGGER_R},
    {SDL_SCANCODE_UP, PAD_BUTTON_UP}, {SDL_SCANCODE_DOWN, PAD_BUTTON_DOWN},
    {SDL_SCANCODE_LEFT, PAD_BUTTON_LEFT}, {SDL_SCANCODE_RIGHT, PAD_BUTTON_RIGHT},
};

const PADKeyButtonBinding p1_p2_profile_buttons[] = {
    {SDL_SCANCODE_J, PAD_BUTTON_A}, {SDL_SCANCODE_K, PAD_BUTTON_B},
    {SDL_SCANCODE_U, PAD_BUTTON_X}, {SDL_SCANCODE_I, PAD_BUTTON_Y},
    {SDL_SCANCODE_RETURN, PAD_BUTTON_START}, {SDL_SCANCODE_O, PAD_TRIGGER_Z},
    {SDL_SCANCODE_Q, PAD_TRIGGER_L}, {SDL_SCANCODE_E, PAD_TRIGGER_R},
    {SDL_SCANCODE_Z, PAD_BUTTON_UP}, {SDL_SCANCODE_X, PAD_BUTTON_DOWN},
    {SDL_SCANCODE_C, PAD_BUTTON_LEFT}, {SDL_SCANCODE_V, PAD_BUTTON_RIGHT},
};

const PADKeyAxisBinding p1_axes[] = {
    {SDL_SCANCODE_D, PAD_AXIS_LEFT_X_POS, 1}, {SDL_SCANCODE_A, PAD_AXIS_LEFT_X_NEG, 1},
    {SDL_SCANCODE_W, PAD_AXIS_LEFT_Y_POS, 1}, {SDL_SCANCODE_S, PAD_AXIS_LEFT_Y_NEG, 1},
    {SDL_SCANCODE_H, PAD_AXIS_RIGHT_X_POS, 1}, {SDL_SCANCODE_F, PAD_AXIS_RIGHT_X_NEG, 1},
    {SDL_SCANCODE_T, PAD_AXIS_RIGHT_Y_POS, 1}, {SDL_SCANCODE_G, PAD_AXIS_RIGHT_Y_NEG, 1},
    {SDL_SCANCODE_Q, PAD_AXIS_TRIGGER_L, 1}, {SDL_SCANCODE_E, PAD_AXIS_TRIGGER_R, 1},
};

const PADKeyButtonBinding p2_buttons[] = {
    {SDL_SCANCODE_RSHIFT, PAD_BUTTON_A},
    {SDL_SCANCODE_RCTRL, PAD_BUTTON_B},
    {SDL_SCANCODE_END, PAD_BUTTON_START},
    {SDL_SCANCODE_DELETE, PAD_BUTTON_X},
    {SDL_SCANCODE_PAGEDOWN, PAD_TRIGGER_Z},
    {SDL_SCANCODE_HOME, PAD_TRIGGER_L},
    {SDL_SCANCODE_PAGEUP, PAD_TRIGGER_R},
};

const PADKeyAxisBinding p2_axes[] = {
    {SDL_SCANCODE_RIGHT, PAD_AXIS_LEFT_X_POS, 1},
    {SDL_SCANCODE_LEFT, PAD_AXIS_LEFT_X_NEG, 1},
    {SDL_SCANCODE_UP, PAD_AXIS_LEFT_Y_POS, 1},
    {SDL_SCANCODE_DOWN, PAD_AXIS_LEFT_Y_NEG, 1},
    {SDL_SCANCODE_KP_6, PAD_AXIS_RIGHT_X_POS, 1},
    {SDL_SCANCODE_KP_4, PAD_AXIS_RIGHT_X_NEG, 1},
    {SDL_SCANCODE_KP_8, PAD_AXIS_RIGHT_Y_POS, 1},
    {SDL_SCANCODE_KP_2, PAD_AXIS_RIGHT_Y_NEG, 1},
};

bool install_bindings(unsigned port, const PADKeyButtonBinding* buttons,
                      std::size_t button_count, const PADKeyAxisBinding* axes,
                      std::size_t axis_count)
{
    for (std::size_t i = 0; i < button_count; ++i)
        if (!PADSetKeyButtonBinding(port, buttons[i])) return false;
    for (std::size_t i = 0; i < axis_count; ++i)
        if (!PADSetKeyAxisBinding(port, axes[i])) return false;
    return true;
}

bool bind_keyboard(uint32_t requested_mask)
{
    PADClearKeyBindings(0);
    PADClearKeyBindings(1);
    const bool p2_profile = (requested_mask & (1U << 1)) != 0;
    const auto* p1_buttons = p2_profile ? p1_p2_profile_buttons : p1_default_buttons;
    const std::size_t p1_button_count =
        p2_profile ? sizeof(p1_p2_profile_buttons) / sizeof(p1_p2_profile_buttons[0])
                   : sizeof(p1_default_buttons) / sizeof(p1_default_buttons[0]);
    if (!install_bindings(0, p1_buttons, p1_button_count, p1_axes,
                          sizeof(p1_axes) / sizeof(p1_axes[0])))
        return false;
    if (requested_mask & (1U << 1)) {
        if (!install_bindings(1, p2_buttons, sizeof(p2_buttons) / sizeof(p2_buttons[0]),
                              p2_axes, sizeof(p2_axes) / sizeof(p2_axes[0])))
            return false;
    }
    return true;
}
} // namespace

extern "C" int melee_web_input_startup(void)
{
    if (state.ready) return 1;
    initialization_error = "";
    if (!PADInit()) {
        initialization_error = "Aurora PADInit failed";
        neutral_snapshot();
        return 0;
    }
    configuration_error = "";
    if (!bind_keyboard(state.keyboard_requested_mask)) {
        initialization_error = "Aurora rejected a keyboard binding";
        for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port)
            PADSetKeyboardActive(port, false);
        PADBlockInput(true);
        neutral_snapshot();
        return 0;
    }
    state.ready = 1;
    apply_policy();
    neutral_snapshot();
    return 1;
}

extern "C" const MeleeWebInputSnapshot* melee_web_input_poll(void)
{
    if (!state.ready) return &state;
    uint32_t physical = 0;
    for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port)
        if (PADGetIndexForPort(port) >= 0) physical |= 1U << port;
    // A new source must not inherit keys held during a previous source's use.
    if (physical != state.physical_mask) SDL_ResetKeyboard();
    state.physical_mask = physical;
    apply_policy();
    (void) PADRead(state.raw); // Return value is rumble support, not connectivity.
    for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        const bool source = (physical & (1U << port)) ||
                            (state.keyboard_requested_mask & (1U << port));
        if (!source) neutral(state.raw[port], PAD_ERR_NO_CONTROLLER);
        else if (!state.active && (state.keyboard_requested_mask & ~physical & (1U << port)))
            // A configured virtual keyboard stays connected while its input is
            // blocked; losing focus is not a source controller disconnect.
            neutral(state.raw[port], PAD_ERR_NONE);
        else if (!state.active || state.raw[port].err != PAD_ERR_NONE)
            neutral(state.raw[port], state.raw[port].err);
        state.clamped[port] = state.raw[port];
    }
    PADClamp(state.clamped);
    ++state.samples;
    for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        if (!has_input(state.raw[port])) continue;
        state.last_non_neutral = {state.samples, port, 1, bool(physical & (1U << port)),
                                  state.raw[port], state.clamped[port]};
        break;
    }
    return &state;
}

extern "C" const MeleeWebInputSnapshot* melee_web_input_snapshot(void) { return &state; }

extern "C" void melee_web_input_set_activity(int focused, int visible)
{
    const bool changed = state.focused != bool(focused) || state.visible != bool(visible);
    state.focused = focused != 0;
    state.visible = visible != 0;
    if (changed && state.ready) SDL_ResetKeyboard();
    apply_policy();
    if (!state.active) neutral_snapshot(); // Do not wait for a suspended browser tick.
}

extern "C" int melee_web_input_set_keyboard_port(unsigned port, int enabled)
{
    if (port > 1) {
        configuration_error = "Keyboard fallback supports ports 0 and 1 only";
        return 0;
    }
    const uint32_t bit = 1U << port;
    const uint32_t next = enabled ? state.keyboard_requested_mask | bit
                                  : state.keyboard_requested_mask & ~bit;
    if (next == state.keyboard_requested_mask) return 1;
    if (state.ready && !bind_keyboard(next)) {
        configuration_error = "Aurora rejected a keyboard profile binding";
        return 0;
    }
    if (state.ready) SDL_ResetKeyboard();
    state.keyboard_requested_mask = next;
    configuration_error = "";
    apply_policy();
    neutral_snapshot();
    return 1;
}

extern "C" void melee_web_input_set_keyboard(int enabled)
{
    (void) melee_web_input_set_keyboard_port(0, enabled);
}

extern "C" const char* melee_web_input_message(void)
{
    static char text[4096];
    int length = std::snprintf(text, sizeof(text),
        "{\"ready\":%d,\"focused\":%d,\"visible\":%d,\"active\":%d,"
        "\"keyboard_requested\":%d,\"keyboard_active\":%d,"
        "\"keyboard_requested_mask\":%u,\"keyboard_active_mask\":%u,"
        "\"physical_mask\":%u,"
        "\"samples\":%llu,\"error\":\"%s\",\"pads\":[",
        state.ready, state.focused, state.visible, state.active,
        state.keyboard_requested, state.keyboard_active, state.keyboard_requested_mask,
        state.keyboard_active_mask, state.physical_mask,
        static_cast<unsigned long long>(state.samples),
        initialization_error[0] ? initialization_error : configuration_error);
    for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        const auto& raw = state.raw[port];
        const auto& clamped = state.clamped[port];
        length += std::snprintf(text + length, sizeof(text) - std::size_t(length),
            "%s{\"source\":\"%s\",\"err\":%d,\"buttons\":%u,\"stick\":[%d,%d],"
            "\"cstick\":[%d,%d],\"triggers\":[%u,%u],\"analog\":[%u,%u],"
            "\"clamped\":{\"stick\":[%d,%d],\"cstick\":[%d,%d],\"triggers\":[%u,%u]}}",
            port ? "," : "", state.physical_mask & (1U << port) ? "gamepad" :
                state.keyboard_requested_mask & (1U << port) ? "keyboard" : "none",
            raw.err, raw.button, raw.stickX, raw.stickY, raw.substickX, raw.substickY,
            raw.triggerLeft, raw.triggerRight, raw.analogA, raw.analogB,
            clamped.stickX, clamped.stickY, clamped.substickX, clamped.substickY,
            clamped.triggerLeft, clamped.triggerRight);
    }
    length += std::snprintf(text + length, sizeof(text) - std::size_t(length), "],\"last_non_neutral\":");
    const auto& last = state.last_non_neutral;
    if (!last.valid) {
        std::snprintf(text + length, sizeof(text) - std::size_t(length), "null}");
    } else {
        std::snprintf(text + length, sizeof(text) - std::size_t(length),
            "{\"port\":%u,\"sample\":%llu,\"source\":\"%s\",\"err\":%d,\"buttons\":%u,"
            "\"ext_buttons\":%u,\"stick\":[%d,%d],\"cstick\":[%d,%d],\"triggers\":[%u,%u],"
            "\"analog\":[%u,%u],\"clamped\":{\"stick\":[%d,%d],\"cstick\":[%d,%d],\"triggers\":[%u,%u]}}}",
            last.port, static_cast<unsigned long long>(last.sample), last.physical ? "gamepad" : "keyboard",
            last.raw.err, last.raw.button, last.raw.extButton, last.raw.stickX, last.raw.stickY,
            last.raw.substickX, last.raw.substickY, last.raw.triggerLeft, last.raw.triggerRight,
            last.raw.analogA, last.raw.analogB, last.clamped.stickX, last.clamped.stickY,
            last.clamped.substickX, last.clamped.substickY, last.clamped.triggerLeft, last.clamped.triggerRight);
    }
    return text;
}

extern "C" void melee_web_input_shutdown(void)
{
    if (state.ready) {
        PADBlockInput(true);
        for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port)
            PADSetKeyboardActive(port, false);
        SDL_ResetKeyboard();
    }
    state = {};
    initialization_error = "";
    configuration_error = "";
    neutral_snapshot();
}

// Menu navigation reuses the same SDL/GameCube mapping as gameplay. Keyboard
// focus/navigation stays in the DOM; only physical ports contribute here.
extern "C" unsigned melee_web_input_menu_buttons(void)
{
    if (!state.active) return 0;
    unsigned buttons = 0;
    for (unsigned port = 0; port < 2; ++port) {
        if (!(state.physical_mask & (1U << port)) || state.clamped[port].err != PAD_ERR_NONE) continue;
        const auto& pad = state.clamped[port];
        buttons |= pad.button;
        if (pad.stickX < -40) buttons |= 1;
        if (pad.stickX > 40) buttons |= 2;
        if (pad.stickY < -40) buttons |= 4;
        if (pad.stickY > 40) buttons |= 8;
    }
    return buttons;
}
