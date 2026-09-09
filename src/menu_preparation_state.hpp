#pragma once

namespace melee_web {

// The native menu has one preparation boundary for each source transition.
// The state is deliberately small so callers cannot arm simulation or source
// drawing until construction has completed. A newly entered scene is rendered
// with simulation stopped until its WebGPU pipelines have drained and remained
// quiet for two callbacks; only then may the source clock be armed.
class MenuPreparationState {
public:
    enum class Phase { Idle, WaitingForAudio, Constructing, Priming, Settling, Arming };

    void reset() noexcept { phase_ = Phase::Idle; quiet_frames_ = 0; }

    bool request() noexcept
    {
        if (phase_ != Phase::Idle) return false;
        quiet_frames_ = 0;
        phase_ = Phase::WaitingForAudio;
        return true;
    }

    bool request_render_settle() noexcept
    {
        if (phase_ != Phase::Idle) return false;
        quiet_frames_ = 0;
        phase_ = Phase::Settling;
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
        quiet_frames_ = 0;
        phase_ = has_live_scene ? Phase::Priming : Phase::Idle;
    }

    bool warming() const noexcept
    {
        return phase_ == Phase::Priming || phase_ == Phase::Settling;
    }

    bool observe_render(bool drew_source, unsigned queued_pipelines, bool render_preparation_activity) noexcept
    {
        if (!warming() || !drew_source) return false;
        if (phase_ == Phase::Priming) phase_ = Phase::Settling;
        quiet_frames_ = queued_pipelines == 0 && !render_preparation_activity ? quiet_frames_ + 1 : 0;
        if (quiet_frames_ < 2) return false;
        phase_ = Phase::Arming;
        return true;
    }

    bool arming() const noexcept { return phase_ == Phase::Arming; }

    bool arm() noexcept
    {
        if (phase_ != Phase::Arming) return false;
        phase_ = Phase::Idle;
        quiet_frames_ = 0;
        return true;
    }

    bool busy() const noexcept { return phase_ != Phase::Idle; }

    // Priming and settling intentionally draw an unchanged source scene so
    // WebGPU can discover and compile its pipelines before simulation starts.
    bool suppress_source_draw() const noexcept
    {
        return phase_ == Phase::WaitingForAudio || phase_ == Phase::Constructing;
    }

    Phase phase() const noexcept { return phase_; }

private:
    Phase phase_ = Phase::Idle;
    unsigned quiet_frames_ = 0;
};

} // namespace melee_web
