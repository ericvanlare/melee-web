/* Adapter contract tests with an injected provider. Real SDL/PAD sampling and
 * the provider's clamp algorithm are verified separately in the browser. */
#include "browser_input.h"
#include <SDL3/SDL_keyboard.h>

#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::array<PADStatus, 4> supplied{};
std::array<int, 4> connected{-1, -1, -1, -1};
std::array<bool, 4> keyboard{};
std::array<std::vector<PADKeyButtonBinding>, 4> buttons;
std::array<std::vector<PADKeyAxisBinding>, 4> axes;
bool init_ok = true, bindings_ok = true, blocked = false;
int reads = 0, clamps = 0, key_resets = 0;
PADStatus* read_destination = nullptr;

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void neutral(const PADStatus& pad)
{
    check(!pad.button && !pad.stickX && !pad.stickY && !pad.substickX && !pad.substickY &&
          !pad.triggerLeft && !pad.triggerRight && !pad.analogA && !pad.analogB && !pad.extButton,
          "input must be neutral");
}

void startup()
{
    check(melee_web_input_startup() == 1, "provider startup succeeds");
    melee_web_input_set_activity(1, 1);
    melee_web_input_set_keyboard(1);
}

void keyboard_preference()
{
    check(melee_web_input_startup() == 1, "provider starts independently of fallback");
    melee_web_input_set_activity(1, 1);
    check(!melee_web_input_snapshot()->keyboard_requested && !keyboard[0],
          "keyboard fallback defaults off until explicitly enabled");
    melee_web_input_shutdown();
    melee_web_input_set_keyboard(1);
    check(melee_web_input_startup() == 1, "provider can restart");
    melee_web_input_set_activity(1, 1);
    check(melee_web_input_snapshot()->keyboard_requested && keyboard[0],
          "a frontend choice before runtime startup is preserved");
}

void binding_mapping()
{
    startup();
    check(buttons[0].size() == PAD_BUTTON_COUNT && axes[0].size() == PAD_AXIS_COUNT,
          "all default P1 keyboard slots explicitly configured");
    check(buttons[1].empty() && axes[1].empty(),
          "P2 keyboard bindings stay absent until explicitly requested");
    std::map<int, unsigned> actual_buttons;
    for (const auto& binding : buttons[0]) actual_buttons[binding.scancode] = binding.padButton;
    const std::map<int, unsigned> expected_buttons{
        {SDL_SCANCODE_J, PAD_BUTTON_A}, {SDL_SCANCODE_K, PAD_BUTTON_B},
        {SDL_SCANCODE_U, PAD_BUTTON_X}, {SDL_SCANCODE_I, PAD_BUTTON_Y},
        {SDL_SCANCODE_RETURN, PAD_BUTTON_START}, {SDL_SCANCODE_O, PAD_TRIGGER_Z},
        {SDL_SCANCODE_Q, PAD_TRIGGER_L}, {SDL_SCANCODE_E, PAD_TRIGGER_R},
        {SDL_SCANCODE_UP, PAD_BUTTON_UP}, {SDL_SCANCODE_DOWN, PAD_BUTTON_DOWN},
        {SDL_SCANCODE_LEFT, PAD_BUTTON_LEFT}, {SDL_SCANCODE_RIGHT, PAD_BUTTON_RIGHT},
    };
    check(actual_buttons == expected_buttons, "documented keys map to GameCube digital buttons");
    std::map<int, unsigned> actual_axes;
    for (const auto& binding : axes[0]) actual_axes[binding.scancode] = binding.padAxis;
    const std::map<int, unsigned> expected_axes{
        {SDL_SCANCODE_D, PAD_AXIS_LEFT_X_POS}, {SDL_SCANCODE_A, PAD_AXIS_LEFT_X_NEG},
        {SDL_SCANCODE_W, PAD_AXIS_LEFT_Y_POS}, {SDL_SCANCODE_S, PAD_AXIS_LEFT_Y_NEG},
        {SDL_SCANCODE_H, PAD_AXIS_RIGHT_X_POS}, {SDL_SCANCODE_F, PAD_AXIS_RIGHT_X_NEG},
        {SDL_SCANCODE_T, PAD_AXIS_RIGHT_Y_POS}, {SDL_SCANCODE_G, PAD_AXIS_RIGHT_Y_NEG},
        {SDL_SCANCODE_Q, PAD_AXIS_TRIGGER_L}, {SDL_SCANCODE_E, PAD_AXIS_TRIGGER_R},
    };
    check(actual_axes == expected_axes, "keyboard axis signs follow GameCube coordinates");
    check(keyboard[0] && !keyboard[1] && !keyboard[2] && !keyboard[3],
          "keyboard fallback is explicit and only occupies port zero");
}

void keyboard_port_two_profile()
{
    startup();
    const auto resets = key_resets;
    check(melee_web_input_set_keyboard_port(1, 1) == 1,
          "explicit P2 keyboard profile can be enabled");
    const auto* snapshot = melee_web_input_snapshot();
    check(snapshot->keyboard_requested_mask == 3 && snapshot->keyboard_active_mask == 3 &&
              keyboard[0] && keyboard[1] && key_resets > resets,
          "P1 and P2 keyboard profiles become active independently");

    std::map<int, unsigned> p1_buttons;
    for (const auto& binding : buttons[0]) p1_buttons[binding.scancode] = binding.padButton;
    check(p1_buttons.at(SDL_SCANCODE_Z) == PAD_BUTTON_UP &&
              p1_buttons.at(SDL_SCANCODE_X) == PAD_BUTTON_DOWN &&
              p1_buttons.at(SDL_SCANCODE_C) == PAD_BUTTON_LEFT &&
              p1_buttons.at(SDL_SCANCODE_V) == PAD_BUTTON_RIGHT &&
              !p1_buttons.contains(SDL_SCANCODE_UP),
          "P1 arrows move to non-overlapping D-pad keys in the P2 profile");

    std::map<int, unsigned> p2_buttons;
    for (const auto& binding : buttons[1]) p2_buttons[binding.scancode] = binding.padButton;
    check(p2_buttons.size() == 7 && p2_buttons.at(SDL_SCANCODE_RSHIFT) == PAD_BUTTON_A &&
              p2_buttons.at(SDL_SCANCODE_RCTRL) == PAD_BUTTON_B &&
              p2_buttons.at(SDL_SCANCODE_END) == PAD_BUTTON_START &&
              p2_buttons.at(SDL_SCANCODE_DELETE) == PAD_BUTTON_X &&
              p2_buttons.at(SDL_SCANCODE_PAGEDOWN) == PAD_TRIGGER_Z &&
              p2_buttons.at(SDL_SCANCODE_HOME) == PAD_TRIGGER_L &&
              p2_buttons.at(SDL_SCANCODE_PAGEUP) == PAD_TRIGGER_R,
          "P2 buttons use the documented laptop-friendly keys");

    std::map<int, unsigned> p2_axes;
    for (const auto& binding : axes[1]) p2_axes[binding.scancode] = binding.padAxis;
    check(p2_axes.size() == 8 && p2_axes.at(SDL_SCANCODE_RIGHT) == PAD_AXIS_LEFT_X_POS &&
              p2_axes.at(SDL_SCANCODE_LEFT) == PAD_AXIS_LEFT_X_NEG &&
              p2_axes.at(SDL_SCANCODE_UP) == PAD_AXIS_LEFT_Y_POS &&
              p2_axes.at(SDL_SCANCODE_DOWN) == PAD_AXIS_LEFT_Y_NEG &&
              p2_axes.at(SDL_SCANCODE_KP_6) == PAD_AXIS_RIGHT_X_POS &&
              p2_axes.at(SDL_SCANCODE_KP_4) == PAD_AXIS_RIGHT_X_NEG &&
              p2_axes.at(SDL_SCANCODE_KP_8) == PAD_AXIS_RIGHT_Y_POS &&
              p2_axes.at(SDL_SCANCODE_KP_2) == PAD_AXIS_RIGHT_Y_NEG,
          "P2 arrows and keypad map to left and c-stick axes");

    const auto focus_resets = key_resets;
    melee_web_input_set_activity(0, 1);
    check(melee_web_input_snapshot()->keyboard_active_mask == 0 && !keyboard[0] &&
              !keyboard[1] && key_resets > focus_resets,
          "focus loss clears both keyboard profiles immediately");
    for(unsigned port=0;port<2;port++) {
        check(melee_web_input_snapshot()->raw[port].err==PAD_ERR_NONE,
              "focus loss preserves configured keyboard connectivity");
        check(melee_web_input_poll()->raw[port].err==PAD_ERR_NONE,
              "blocked polling preserves configured keyboard connectivity");
    }
    melee_web_input_set_activity(1, 1);
    check(melee_web_input_snapshot()->keyboard_active_mask == 3 && keyboard[0] &&
              keyboard[1], "both keyboard profiles resume after focus returns");

    check(melee_web_input_set_keyboard_port(2, 1) == 0 &&
              std::string(melee_web_input_message()).find("ports 0 and 1") !=
                  std::string::npos,
          "keyboard profile rejects unsupported ports truthfully");
    check(melee_web_input_set_keyboard_port(1, 0) == 1 &&
              melee_web_input_snapshot()->keyboard_requested_mask == 1 &&
              buttons[1].empty() && buttons[0].at(8).scancode == SDL_SCANCODE_UP,
          "disabling P2 restores the original P1 arrow-D-pad profile");
}

void per_port_physical_priority()
{
    startup();
    check(melee_web_input_set_keyboard_port(1, 1) == 1, "enable P2 keyboard");
    connected[1] = 12;
    supplied[1].button = PAD_BUTTON_A;
    (void) melee_web_input_poll();
    const auto* snapshot = melee_web_input_snapshot();
    check(snapshot->physical_mask == 2 && snapshot->keyboard_active_mask == 1 &&
              keyboard[0] && !keyboard[1] && snapshot->raw[1].button == PAD_BUTTON_A,
          "a physical P2 takes precedence without disabling keyboard P1");
    connected[1] = -1;
    supplied[1] = {};
    (void) melee_web_input_poll();
    check(melee_web_input_snapshot()->keyboard_active_mask == 3 && keyboard[1],
          "P2 keyboard resumes after its physical device disconnects");
}

void raw_and_clamped()
{
    connected[0] = 7;
    supplied[0].button = PAD_BUTTON_A;
    supplied[0].stickX = 127;
    supplied[0].substickY = -127;
    supplied[0].triggerLeft = 255;
    supplied[0].analogA = 99;
    startup();
    const auto* sample = melee_web_input_poll();
    check(sample->samples == 1 && reads == 1 && clamps == 1, "one provider read and diagnostic clamp per poll");
    check(sample->raw[0].stickX == 127 && sample->raw[0].substickY == -127 &&
          sample->raw[0].triggerLeft == 255 && sample->raw[0].button == PAD_BUTTON_A &&
          sample->raw[0].analogA == 99, "raw PADStatus reaches future game code unchanged");
    check(sample->clamped[0].stickX == 41 && sample->clamped[0].substickY == -37 &&
          sample->clamped[0].triggerLeft == 137, "diagnostic copy uses the injected provider's clamp result");
    check(sample->physical_mask == 1 && !sample->keyboard_active,
          "physical provider occupies the port without mixing keyboard fallback");
    (void) melee_web_input_poll();
    check(reads == 2 && clamps == 2, "no hidden input loop or multiple samples per poll");
}

void focus_and_visibility()
{
    startup();
    supplied[0].button = PAD_BUTTON_B;
    supplied[0].stickY = 127;
    supplied[0].substickX = -127;
    supplied[0].triggerRight = 255;
    supplied[0].analogB = 200;
    supplied[0].extButton = PAD_BUTTON_GUIDE;
    (void) melee_web_input_poll();
    const auto before = reads;
    const auto reset_before = key_resets;
    melee_web_input_set_activity(0, 1);
    check(!melee_web_input_snapshot()->active && blocked && !keyboard[0], "blur blocks provider input");
    neutral(melee_web_input_snapshot()->raw[0]);
    neutral(melee_web_input_snapshot()->clamped[0]);
    check(reads == before && key_resets > reset_before, "blur clears held keys immediately without waiting for a tick");
    (void) melee_web_input_poll(); // Even a stale provider result is gated.
    neutral(melee_web_input_snapshot()->raw[0]);
    melee_web_input_set_activity(1, 0);
    check(!melee_web_input_snapshot()->active && blocked, "hidden pages stay neutral even if focus is reported");
    neutral(melee_web_input_snapshot()->raw[0]);
    melee_web_input_set_activity(1, 1);
    check(melee_web_input_snapshot()->active && !blocked && keyboard[0], "visible focused canvas can resume sampling");
    const auto stable_resets = key_resets;
    melee_web_input_set_activity(1, 1);
    check(key_resets == stable_resets, "unchanged focus state does not erase new key presses");
}

void last_non_neutral()
{
    startup();
    check(!melee_web_input_snapshot()->last_non_neutral.valid, "history starts empty");
    check(std::string(melee_web_input_message()).find("\"last_non_neutral\":null") != std::string::npos,
          "empty history is explicit in the diagnostic");
    supplied[0].button = PAD_BUTTON_A;
    supplied[0].stickX = 127;
    (void) melee_web_input_poll();
    auto last = melee_web_input_snapshot()->last_non_neutral;
    check(last.valid && last.sample == 1 && last.port == 0 && !last.physical &&
          last.raw.button == PAD_BUTTON_A && last.raw.stickX == 127 && last.clamped.stickX == 41,
          "history captures an actual sampled port, source, raw values, and clamp diagnostic");
    supplied[0] = {};
    (void) melee_web_input_poll();
    neutral(melee_web_input_snapshot()->raw[0]);
    melee_web_input_set_activity(0, 0);
    (void) melee_web_input_poll();
    neutral(melee_web_input_snapshot()->raw[0]);
    last = melee_web_input_snapshot()->last_non_neutral;
    check(last.valid && last.sample == 1 && last.raw.button == PAD_BUTTON_A && last.raw.stickX == 127,
          "release and focus loss preserve diagnostic history without retaining current input");
}

void disconnect_and_errors()
{
    startup();
    melee_web_input_set_keyboard(0);
    connected[1] = 10;
    supplied[1].button = PAD_BUTTON_X;
    (void) melee_web_input_poll();
    check(melee_web_input_snapshot()->physical_mask == 2, "port mask comes from connected providers, not rumble bits");
    connected[1] = -1;
    (void) melee_web_input_poll();
    for (const auto& pad : melee_web_input_snapshot()->raw) {
        neutral(pad);
        check(pad.err == PAD_ERR_NO_CONTROLLER, "disconnected ports cannot retain a previous sample");
    }
    connected[1] = 10;
    supplied[1].err = PAD_ERR_TRANSFER;
    (void) melee_web_input_poll();
    neutral(melee_web_input_snapshot()->raw[1]);
    check(melee_web_input_snapshot()->raw[1].err == PAD_ERR_TRANSFER, "provider errors remain visible with neutral controls");
}

void keyboard_source_changes()
{
    startup();
    (void) melee_web_input_poll();
    check(melee_web_input_snapshot()->keyboard_active, "keyboard fallback has its own source identity");
    connected[0] = 42;
    const auto before = key_resets;
    (void) melee_web_input_poll();
    check(!keyboard[0] && !melee_web_input_snapshot()->keyboard_active && key_resets > before,
          "physical connection replaces fallback and clears inherited keyboard state");
    connected[0] = -1;
    const auto disconnected = key_resets;
    (void) melee_web_input_poll();
    check(keyboard[0] && key_resets > disconnected, "fallback starts clean after physical disconnection");
    melee_web_input_set_keyboard(0);
    neutral(melee_web_input_snapshot()->raw[0]);
    check(!keyboard[0] && !melee_web_input_snapshot()->keyboard_requested, "fallback can be disabled explicitly");
}

void startup_failure_and_shutdown()
{
    init_ok = false;
    check(!melee_web_input_startup(), "provider initialization failure is not reported as success");
    (void) melee_web_input_poll();
    check(!reads && !melee_web_input_snapshot()->ready &&
          std::string(melee_web_input_message()).find("PADInit failed") != std::string::npos,
          "uninitialized provider is not sampled and its error is visible");
    init_ok = true;
    bindings_ok = false;
    check(!melee_web_input_startup(), "rejected binding fails setup explicitly");
    bindings_ok = true;
    startup();
    supplied[0].button = PAD_BUTTON_A;
    (void) melee_web_input_poll();
    melee_web_input_shutdown();
    check(!melee_web_input_snapshot()->ready && blocked && !keyboard[0], "shutdown relinquishes keyboard and blocks stale provider input");
    for (const auto& pad : melee_web_input_snapshot()->raw) neutral(pad);
    check(!melee_web_input_snapshot()->last_non_neutral.valid, "shutdown clears diagnostic history");
    const auto stopped_reads = reads;
    (void) melee_web_input_poll();
    check(reads == stopped_reads, "poll after shutdown never accesses the provider");
}
} // namespace

extern "C" BOOL PADInit() { return init_ok; }
extern "C" void PADClearKeyBindings(u32 port)
{
    check(port < buttons.size(), "keyboard binding port is in range");
    buttons.at(port).clear();
    axes.at(port).clear();
}
extern "C" BOOL PADSetKeyButtonBinding(u32 port, PADKeyButtonBinding binding)
{
    check(port < buttons.size(), "button binding port is in range");
    buttons.at(port).push_back(binding);
    return bindings_ok;
}
extern "C" BOOL PADSetKeyAxisBinding(u32 port, PADKeyAxisBinding binding)
{
    check(port < axes.size(), "axis binding port is in range");
    axes.at(port).push_back(binding);
    return bindings_ok;
}
extern "C" void PADSetKeyboardActive(u32 port, BOOL active) { keyboard.at(port) = active != 0; }
extern "C" void PADBlockInput(bool block) { blocked = block; }
extern "C" s32 PADGetIndexForPort(u32 port) { return connected.at(port); }
extern "C" u32 PADRead(PADStatus* output)
{
    ++reads;
    read_destination = output;
    std::memcpy(output, supplied.data(), sizeof(PADStatus) * supplied.size());
    return 0xffffffffU; // Deliberately unlike connected ports: this is rumble support.
}
extern "C" void PADClamp(PADStatus* output)
{
    ++clamps;
    check(output != read_destination, "clamping must never mutate the future game's raw sample");
    for (unsigned port = 0; port < 4; ++port) {
        if (output[port].err != PAD_ERR_NONE) continue;
        // Distinct sentinel values test routing, not a copy of Aurora's algorithm.
        if (output[port].stickX) output[port].stickX = 41;
        if (output[port].substickY) output[port].substickY = -37;
        if (output[port].triggerLeft) output[port].triggerLeft = 137;
    }
}
extern "C" void SDL_ResetKeyboard(void) { ++key_resets; }

int main(int argc, char** argv)
{
    const std::map<std::string, void (*)()> cases{
        {"binding_mapping", binding_mapping},
        {"keyboard_port_two_profile", keyboard_port_two_profile},
        {"per_port_physical_priority", per_port_physical_priority},
        {"raw_and_clamped", raw_and_clamped},
        {"keyboard_preference", keyboard_preference},
        {"last_non_neutral", last_non_neutral},
        {"focus_and_visibility", focus_and_visibility}, {"disconnect_and_errors", disconnect_and_errors},
        {"keyboard_source_changes", keyboard_source_changes},
        {"startup_failure_and_shutdown", startup_failure_and_shutdown},
    };
    if (argc != 2 || !cases.contains(argv[1])) return 2;
    try {
        cases.at(argv[1])();
        std::cout << melee_web_input_message() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
