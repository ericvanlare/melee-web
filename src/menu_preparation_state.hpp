#pragma once

namespace melee_web {

// The native menu has one preparation boundary for each source transition.
// The state is deliberately small so callers cannot arm simulation or source
// drawing until construction has completed on a later callback.
class MenuPreparationState {
public:
    enum class Phase { Idle, WaitingForAudio, Constructing, Arming };

    void reset() noexcept { phase_ = Phase::Idle; }

    bool request() noexcept
    {
        if (phase_ != Phase::Idle) return false;
        phase_ = Phase::WaitingForAudio;
        return true;
    }

    bool waiting_for_audio() const noexcept
    {
        return phase_ == Phase::WaitingForAudio;
    }

    bool begin_construction(bool audio_acknowledged) noexcept
    {
        if (phase_ != Phase::WaitingForAudio || !audio_acknowledged) return false;
        phase_ = Phase::Constructing;
        return true;
    }

    void finish_construction(bool has_live_scene) noexcept
    {
        if (phase_ != Phase::Constructing) return;
        phase_ = has_live_scene ? Phase::Arming : Phase::Idle;
    }

    bool arming() const noexcept { return phase_ == Phase::Arming; }

    bool arm() noexcept
    {
        if (phase_ != Phase::Arming) return false;
        phase_ = Phase::Idle;
        return true;
    }

    bool busy() const noexcept { return phase_ != Phase::Idle; }

    // Source draw is suppressed for the request, audio wait, and construction
    // callback. The arm callback is the first callback allowed to draw the
    // newly entered scene.
    bool suppress_source_draw() const noexcept
    {
        return phase_ != Phase::Idle && phase_ != Phase::Arming;
    }

    Phase phase() const noexcept { return phase_; }

private:
    Phase phase_ = Phase::Idle;
};

} // namespace melee_web
