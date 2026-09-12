#ifndef MELEE_WEB_BOXX_INPUT_H
#define MELEE_WEB_BOXX_INPUT_H

#include <cstdint>

// The mapping rules and coordinate tables are derived from b0xx-ahk by
// tlandegger, pinned locally at agirardeau/b0xx-ahk commit
// 7c070f8e0f135c8108cfb0af9a37dc6809070b15.  See licenses/b0xx-ahk.txt.
//
// This is a bounded, platform-independent mapping core.  It does not model
// vJoy, keyboard hooks, timing, macros, or automatic actions.  In particular,
// the caller owns key event delivery and must send a release for each held key.
// Coordinates are raw GameCube stick units: the reference's normalized values
// in [-1, 1] are multiplied directly by 80.

enum class BoxxKey {
    Up,
    Down,
    Left,
    Right,
    ModX,
    ModY,
    A,
    B,
    L,
    R,
    X,
    Y,
    Z,
    CUp,
    CDown,
    CLeft,
    CRight,
    LightShield,
    MidShield,
    Start,
    DUp,
    DDown,
    DLeft,
    DRight,
};

// GameCube PADStatus button bits.  C-stick keys are analog inputs except when
// both modifiers are held, where the reference maps them to these D-pad bits.
enum class BoxxButton : unsigned {
    Left = 0x0001,
    Right = 0x0002,
    Down = 0x0004,
    Up = 0x0008,
    Z = 0x0010,
    R = 0x0020,
    L = 0x0040,
    A = 0x0100,
    B = 0x0200,
    X = 0x0400,
    Y = 0x0800,
    Start = 0x1000,
};

struct BoxxSample {
    unsigned buttons = 0;
    int stickX = 0;
    int stickY = 0;
    int cstickX = 0;
    int cstickY = 0;
    unsigned triggerL = 0;
    unsigned triggerR = 0;
};

class BoxxInput {
public:
    // A key transition is idempotent.  A repeated key-down therefore does not
    // change directional priority, matching the reference's held-key state.
    void key(BoxxKey input, bool down)
    {
        switch (input) {
        case BoxxKey::Up:
            if (up_ == down) return;
            up_ = down;
            if (down) {
                lastVertical_ = BoxxKey::Up;
                haveVertical_ = true;
            }
            return;
        case BoxxKey::Down:
            if (down_ == down) return;
            down_ = down;
            if (down) {
                lastVertical_ = BoxxKey::Down;
                haveVertical_ = true;
            }
            return;
        case BoxxKey::Left:
            if (left_ == down) return;
            left_ = down;
            if (down) {
                lastHorizontal_ = BoxxKey::Left;
                haveHorizontal_ = true;
                if (right_) horizontalModifierLockout_ = true;
            } else {
                // The reference clears this lockout on either horizontal
                // release; priority itself intentionally remains unchanged.
                horizontalModifierLockout_ = false;
            }
            return;
        case BoxxKey::Right:
            if (right_ == down) return;
            right_ = down;
            if (down) {
                lastHorizontal_ = BoxxKey::Right;
                haveHorizontal_ = true;
                if (left_) horizontalModifierLockout_ = true;
            } else {
                horizontalModifierLockout_ = false;
            }
            return;
        case BoxxKey::ModX:
            if (modX_ == down) return;
            modX_ = down;
            // ModX is the order-resetting modifier in the reference.
            horizontalModifierLockout_ = false;
            return;
        case BoxxKey::ModY:
            if (modY_ == down) return;
            modY_ = down;
            return;
        case BoxxKey::A:
            if (a_ == down) return;
            a_ = down;
            return;
        case BoxxKey::B:
            if (b_ == down) return;
            b_ = down;
            return;
        case BoxxKey::L:
            if (l_ == down) return;
            l_ = down;
            return;
        case BoxxKey::R:
            if (r_ == down) return;
            r_ = down;
            return;
        case BoxxKey::X:
            if (x_ == down) return;
            x_ = down;
            return;
        case BoxxKey::Y:
            if (y_ == down) return;
            y_ = down;
            return;
        case BoxxKey::Z:
            if (z_ == down) return;
            z_ = down;
            return;
        case BoxxKey::CUp:
            if (cUp_ == down) return;
            cUp_ = down;
            if (down) {
                lastCVertical_ = BoxxKey::CUp;
                haveCVertical_ = true;
            }
            return;
        case BoxxKey::CDown:
            if (cDown_ == down) return;
            cDown_ = down;
            if (down) {
                lastCVertical_ = BoxxKey::CDown;
                haveCVertical_ = true;
            }
            return;
        case BoxxKey::CLeft:
            if (cLeft_ == down) return;
            cLeft_ = down;
            if (down) {
                lastCHorizontal_ = BoxxKey::CLeft;
                haveCHorizontal_ = true;
            }
            return;
        case BoxxKey::CRight:
            if (cRight_ == down) return;
            cRight_ = down;
            if (down) {
                lastCHorizontal_ = BoxxKey::CRight;
                haveCHorizontal_ = true;
            }
            return;
        case BoxxKey::LightShield:
            if (lightShield_ == down) return;
            lightShield_ = down;
            return;
        case BoxxKey::MidShield:
            if (midShield_ == down) return;
            midShield_ = down;
            return;
        case BoxxKey::Start:
            if (start_ == down) return;
            start_ = down;
            return;
        case BoxxKey::DUp:
            if (dUp_ == down) return;
            dUp_ = down;
            return;
        case BoxxKey::DDown:
            if (dDown_ == down) return;
            dDown_ = down;
            return;
        case BoxxKey::DLeft:
            if (dLeft_ == down) return;
            dLeft_ = down;
            return;
        case BoxxKey::DRight:
            if (dRight_ == down) return;
            dRight_ = down;
            return;
        }
    }

    void reset() { *this = BoxxInput{}; }

    BoxxSample sample() const
    {
        BoxxSample result;

        if (a_) result.buttons |= button(BoxxButton::A);
        if (b_) result.buttons |= button(BoxxButton::B);
        if (l_) result.buttons |= button(BoxxButton::L);
        if (r_) result.buttons |= button(BoxxButton::R);
        if (x_) result.buttons |= button(BoxxButton::X);
        if (y_) result.buttons |= button(BoxxButton::Y);
        if (z_) result.buttons |= button(BoxxButton::Z);
        if (start_) result.buttons |= button(BoxxButton::Start);

        if (dUp_) result.buttons |= button(BoxxButton::Up);
        if (dDown_) result.buttons |= button(BoxxButton::Down);
        if (dLeft_) result.buttons |= button(BoxxButton::Left);
        if (dRight_) result.buttons |= button(BoxxButton::Right);

        // C keys become D-pad buttons while both modifiers are down.  This is
        // evaluated from current held state, so releasing a modifier cannot
        // leave a stale D-pad bit behind.
        if (bothMods()) {
            if (cUp_) result.buttons |= button(BoxxButton::Up);
            if (cDown_) result.buttons |= button(BoxxButton::Down);
            if (cLeft_) result.buttons |= button(BoxxButton::Left);
            if (cRight_) result.buttons |= button(BoxxButton::Right);
        }

        const bool shield = l_ || r_ || lightShield_ || midShield_;
        const Coord analog = shield ? airdodgeCoords()
                                    : (anyMod() && anyQuadrant() && (anyC() || b_)
                                           ? firefoxCoords()
                                           : noShieldCoords());
        result.stickX = activeLeft() ? -analog.x : analog.x;
        result.stickY = activeDown() ? -analog.y : analog.y;

        const Coord cstick = cstickCoords();
        result.cstickX = activeCLeft() ? -cstick.x : cstick.x;
        result.cstickY = activeCDown() ? -cstick.y : cstick.y;

        // A digital shoulder click is a full raw trigger.  The dedicated
        // shield keys provide the reference's partial right-trigger values;
        // a held digital R click takes precedence over those values.
        result.triggerL = l_ ? 255U : 0U;
        if (r_)
            result.triggerR = 255;
        else if (midShield_)
            result.triggerR = 94;
        else if (lightShield_)
            result.triggerR = 49;

        return result;
    }

private:
    struct Coord {
        int x;
        int y;
    };

    static constexpr unsigned button(BoxxButton value)
    {
        return static_cast<unsigned>(value);
    }

    bool activeUp() const
    {
        return up_ && haveVertical_ && lastVertical_ == BoxxKey::Up;
    }
    bool activeDown() const
    {
        return down_ && haveVertical_ && lastVertical_ == BoxxKey::Down;
    }
    bool activeLeft() const
    {
        return left_ && haveHorizontal_ && lastHorizontal_ == BoxxKey::Left;
    }
    bool activeRight() const
    {
        return right_ && haveHorizontal_ && lastHorizontal_ == BoxxKey::Right;
    }
    bool activeCUp() const
    {
        return cUp_ && haveCVertical_ && lastCVertical_ == BoxxKey::CUp && !bothMods();
    }
    bool activeCDown() const
    {
        return cDown_ && haveCVertical_ && lastCVertical_ == BoxxKey::CDown && !bothMods();
    }
    bool activeCLeft() const
    {
        return cLeft_ && haveCHorizontal_ && lastCHorizontal_ == BoxxKey::CLeft && !bothMods();
    }
    bool activeCRight() const
    {
        return cRight_ && haveCHorizontal_ && lastCHorizontal_ == BoxxKey::CRight && !bothMods();
    }
    bool anyVertical() const { return activeUp() || activeDown(); }
    bool anyHorizontal() const { return activeLeft() || activeRight(); }
    bool anyQuadrant() const { return anyVertical() && anyHorizontal(); }
    bool anyCVertical() const { return activeCUp() || activeCDown(); }
    bool anyCHorizontal() const { return activeCLeft() || activeCRight(); }
    bool anyC() const { return anyCVertical() || anyCHorizontal(); }
    bool bothMods() const { return modX_ && modY_; }
    bool modXActive() const
    {
        return modX_ && !modY_ && !(horizontalModifierLockout_ && !anyVertical());
    }
    bool modYActive() const
    {
        return modY_ && !modX_ && !(horizontalModifierLockout_ && !anyVertical());
    }
    bool anyMod() const { return modXActive() || modYActive(); }

    static constexpr Coord origin() { return {0, 0}; }

    Coord noShieldCoords() const
    {
        if (!anyVertical() && !anyHorizontal()) return origin();
        if (anyQuadrant()) {
            if (modXActive()) return {59, 25};       // .7375, .3125
            if (modYActive()) return {25, 59};       // .3125, .7375
            return {56, 56};                         // .7, .7
        }
        if (anyVertical()) {
            if (modXActive()) return {0, 43};         // 0, .5375
            if (modYActive()) return {0, 59};         // 0, .7375
            return {0, 80};                           // 0, 1
        }
        if (modXActive()) return {53, 0};             // .6625, 0
        if (modYActive()) return b_ ? Coord{80, 0} : Coord{27, 0};
        return {80, 0};                               // 1, 0
    }

    Coord airdodgeCoords() const
    {
        if (!anyVertical() && !anyHorizontal()) return origin();
        if (anyQuadrant()) {
            if (modXActive()) return {51, 30};       // .6375, .375
            if (modYActive()) return activeUp() ? Coord{38, 70} : Coord{40, 68};
            return activeUp() ? Coord{56, 56} : Coord{56, 55};
        }
        if (anyVertical()) {
            if (modXActive()) return {0, 43};
            if (modYActive()) return {0, 59};
            return {0, 80};
        }
        if (modXActive()) return {53, 0};
        if (modYActive()) return b_ ? Coord{80, 0} : Coord{27, 0};
        return {80, 0};
    }

    Coord firefoxCoords() const
    {
        if (modXActive()) {
            if (activeCUp()) return b_ ? Coord{59, 43} : Coord{56, 41};
            if (activeCDown()) return b_ ? Coord{70, 36} : Coord{56, 29};
            if (activeCLeft()) return b_ ? Coord{68, 42} : Coord{63, 39};
            if (activeCRight()) return b_ ? Coord{51, 43} : Coord{49, 42};
            return {73, 31};
        }
        if (modYActive()) {
            if (activeCUp()) return b_ ? Coord{47, 64} : Coord{41, 56};
            if (activeCDown()) return b_ ? Coord{36, 70} : Coord{29, 56};
            if (activeCLeft()) return b_ ? Coord{42, 68} : Coord{39, 63};
            if (activeCRight()) return b_ ? Coord{47, 57} : Coord{51, 61};
            return {31, 73};
        }
        return origin();
    }

    Coord cstickCoords() const
    {
        if (!anyCVertical() && !anyCHorizontal()) return origin();
        if (anyCVertical() && anyCHorizontal()) return {42, 68}; // .525, .85
        if (anyCVertical()) return {0, 80};
        if (modXActive() && activeUp()) return {72, 40};         // .9, .5
        if (modXActive() && activeDown()) return {72, -40};      // .9, -.5
        return {80, 0};
    }

    bool up_ = false;
    bool down_ = false;
    bool left_ = false;
    bool right_ = false;
    bool modX_ = false;
    bool modY_ = false;
    bool a_ = false;
    bool b_ = false;
    bool l_ = false;
    bool r_ = false;
    bool x_ = false;
    bool y_ = false;
    bool z_ = false;
    bool cUp_ = false;
    bool cDown_ = false;
    bool cLeft_ = false;
    bool cRight_ = false;
    bool lightShield_ = false;
    bool midShield_ = false;
    bool start_ = false;
    bool dUp_ = false;
    bool dDown_ = false;
    bool dLeft_ = false;
    bool dRight_ = false;

    bool haveVertical_ = false;
    bool haveHorizontal_ = false;
    bool haveCVertical_ = false;
    bool haveCHorizontal_ = false;
    BoxxKey lastVertical_ = BoxxKey::Up;
    BoxxKey lastHorizontal_ = BoxxKey::Left;
    BoxxKey lastCVertical_ = BoxxKey::CUp;
    BoxxKey lastCHorizontal_ = BoxxKey::CLeft;
    bool horizontalModifierLockout_ = false;
};

#endif
