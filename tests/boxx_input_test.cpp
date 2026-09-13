#include "boxx_input.h"

#include <cstdlib>
#include <iostream>

namespace {

void expect(const BoxxSample& actual, unsigned buttons, int stickX, int stickY,
            int cstickX, int cstickY, unsigned triggerL, unsigned triggerR,
            const char* label)
{
    if (actual.buttons == buttons && actual.stickX == stickX && actual.stickY == stickY &&
        actual.cstickX == cstickX && actual.cstickY == cstickY &&
        actual.triggerL == triggerL && actual.triggerR == triggerR)
        return;
    std::cerr << label << ": got {buttons=" << actual.buttons << ", stick="
              << actual.stickX << "," << actual.stickY << ", cstick="
              << actual.cstickX << "," << actual.cstickY << ", triggers="
              << actual.triggerL << "," << actual.triggerR << "}\n";
    std::exit(1);
}

void neutral_and_buttons()
{
    BoxxInput input;
    expect(input.sample(), 0, 0, 0, 0, 0, 0, 0, "initial neutral");

    input.key(BoxxKey::A, true);
    input.key(BoxxKey::B, true);
    input.key(BoxxKey::L, true);
    input.key(BoxxKey::R, true);
    input.key(BoxxKey::X, true);
    input.key(BoxxKey::Y, true);
    input.key(BoxxKey::Z, true);
    input.key(BoxxKey::Start, true);
    // The mask is the original PADStatus layout: A/B/X/Y, L/R/Z and Start.
    expect(input.sample(), 0x1f70, 0, 0, 0, 0, 255, 255, "digital buttons");

    input.key(BoxxKey::DUp, true);
    input.key(BoxxKey::DLeft, true);
    expect(input.sample(), 0x1f79, 0, 0, 0, 0, 255, 255, "direct dpad");
}

void directional_priority_and_release()
{
    BoxxInput input;
    input.key(BoxxKey::Up, true);
    expect(input.sample(), 0, 0, 80, 0, 0, 0, 0, "up");
    input.key(BoxxKey::Down, true);
    expect(input.sample(), 0, 0, -80, 0, 0, 0, 0, "newer down wins");
    input.key(BoxxKey::Down, false);
    // The older held direction does not reactivate after the newer release.
    expect(input.sample(), 0, 0, 0, 0, 0, 0, 0, "released newer direction stays neutral");
    input.key(BoxxKey::Up, true); // repeated key-down is ignored
    expect(input.sample(), 0, 0, 0, 0, 0, 0, 0, "repeated held key does not reactivate");
    input.key(BoxxKey::Up, false);
    input.key(BoxxKey::Up, true);
    expect(input.sample(), 0, 0, 80, 0, 0, 0, 0, "fresh press restores priority");

    input.reset();
    input.key(BoxxKey::Left, true);
    input.key(BoxxKey::Right, true);
    expect(input.sample(), 0, 80, 0, 0, 0, 0, 0, "newer right wins");
    input.key(BoxxKey::Right, false);
    expect(input.sample(), 0, 0, 0, 0, 0, 0, 0, "released newer horizontal stays neutral");
}

void modifier_coordinates_and_lockout()
{
    BoxxInput input;
    input.key(BoxxKey::Up, true);
    input.key(BoxxKey::Right, true);
    expect(input.sample(), 0, 56, 56, 0, 0, 0, 0, "quadrant");

    input.key(BoxxKey::ModX, true);
    expect(input.sample(), 0, 59, 25, 0, 0, 0, 0, "quadrant modx");
    input.key(BoxxKey::ModY, true);
    // Both modifiers cancel the analog modifier and put C keys in the D-pad
    // channel; with no C key the ordinary quadrant remains.
    expect(input.sample(), 0, 56, 56, 0, 0, 0, 0, "both modifiers");
    input.key(BoxxKey::ModY, false);
    input.key(BoxxKey::ModX, false);

    input.reset();
    input.key(BoxxKey::Left, true);
    input.key(BoxxKey::ModX, true);
    expect(input.sample(), 0, -53, 0, 0, 0, 0, 0, "horizontal modx");
    input.key(BoxxKey::Right, true);
    // Pressing the opposing horizontal direction after ModX activates SOCD
    // modifier lockout and leaves the newer right direction at full scale.
    expect(input.sample(), 0, 80, 0, 0, 0, 0, 0, "horizontal lockout");
    input.key(BoxxKey::Right, false);
    expect(input.sample(), 0, 0, 0, 0, 0, 0, 0, "lockout release preserves priority rule");

    input.reset();
    input.key(BoxxKey::Left, true);
    input.key(BoxxKey::Right, true);
    input.key(BoxxKey::ModX, true);
    expect(input.sample(), 0, 53, 0, 0, 0, 0, 0, "modifier pressed after SOCD pair");
}

void shield_and_triggers()
{
    BoxxInput input;
    input.key(BoxxKey::Up, true);
    input.key(BoxxKey::Right, true);
    input.key(BoxxKey::L, true);
    expect(input.sample(), 0x40, 56, 56, 0, 0, 255, 0, "digital shield uses airdodge table");

    input.key(BoxxKey::LightShield, true);
    expect(input.sample(), 0x40, 56, 56, 0, 0, 255, 49, "light shield raw trigger");
    input.key(BoxxKey::MidShield, true);
    expect(input.sample(), 0x40, 56, 56, 0, 0, 255, 94, "mid shield raw trigger");
    input.key(BoxxKey::MidShield, false);
    expect(input.sample(), 0x40, 56, 56, 0, 0, 255, 49, "held light shield survives mid release");
    input.key(BoxxKey::LightShield, false);
    expect(input.sample(), 0x40, 56, 56, 0, 0, 255, 0, "shield trigger release");

    input.key(BoxxKey::L, false);
    input.key(BoxxKey::R, true);
    expect(input.sample(), 0x20, 56, 56, 0, 0, 0, 255, "digital right shield is a full raw trigger");
    input.key(BoxxKey::MidShield, true);
    expect(input.sample(), 0x20, 56, 56, 0, 0, 0, 255, "digital right trigger wins over partial shield");
    input.key(BoxxKey::R, false);
    expect(input.sample(), 0, 56, 56, 0, 0, 0, 94, "partial shield returns after digital release");
}

void firefox_and_cstick()
{
    BoxxInput input;
    input.key(BoxxKey::Up, true);
    input.key(BoxxKey::Right, true);
    input.key(BoxxKey::ModX, true);
    input.key(BoxxKey::CUp, true);
    expect(input.sample(), 0, 56, 41, 0, 80, 0, 0, "firefox modx cup");

    input.key(BoxxKey::B, true);
    expect(input.sample(), 0x200, 59, 43, 0, 80, 0, 0, "extended firefox modx cup");
    input.key(BoxxKey::CUp, false);
    input.key(BoxxKey::CLeft, true);
    expect(input.sample(), 0x200, 68, 42, -72, 40, 0, 0, "extended firefox modx cleft");

    input.reset();
    input.key(BoxxKey::Up, true);
    input.key(BoxxKey::ModX, true);
    input.key(BoxxKey::CLeft, true);
    // A lone horizontal C key uses the ModX/up angled C-stick table.
    expect(input.sample(), 0, 0, 43, -72, 40, 0, 0, "angled cstick with up");
    input.key(BoxxKey::Up, false);
    input.key(BoxxKey::Down, true);
    expect(input.sample(), 0, 0, -43, -72, -40, 0, 0, "angled cstick with down");

    input.reset();
    input.key(BoxxKey::CUp, true);
    input.key(BoxxKey::CRight, true);
    expect(input.sample(), 0, 0, 0, 42, 68, 0, 0, "diagonal cstick");
}

void both_modifiers_dpad_and_reset()
{
    BoxxInput input;
    input.key(BoxxKey::ModX, true);
    input.key(BoxxKey::ModY, true);
    input.key(BoxxKey::CUp, true);
    input.key(BoxxKey::CLeft, true);
    expect(input.sample(), 0x9, 0, 0, 0, 0, 0, 0, "both modifiers map C keys to dpad");
    input.key(BoxxKey::ModY, false);
    expect(input.sample(), 0, 0, 0, -42, 68, 0, 0, "modifier release removes dpad bits");

    input.key(BoxxKey::B, true);
    input.key(BoxxKey::LightShield, true);
    input.key(BoxxKey::DRight, true);
    input.key(BoxxKey::Up, true);
    input.reset();
    expect(input.sample(), 0, 0, 0, 0, 0, 0, 0, "reset clears held state and priority");
}

} // namespace

int main()
{
    neutral_and_buttons();
    directional_priority_and_release();
    modifier_coordinates_and_lockout();
    shield_and_triggers();
    firefox_and_cstick();
    both_modifiers_dpad_and_reset();
    std::cout << "boxx input vectors passed\n";
    return 0;
}
