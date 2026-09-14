#pragma once

namespace melee_web {

// The native menu has one preparation boundary for each source transition.
// The state is deliberately small so callers cannot arm simulation or source
// drawing until construction has completed. A newly entered scene is rendered
// with simulation stopped until its WebGPU pipelines have drained and remained
// quiet for two callbacks. Submitted GPU work must then complete before the
// source clock can be armed; that wait adds no source ticks or draws.
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

    // Upload statistics describe work that has already completed. Only a
    // pipeline still queued on the asynchronous renderer can benefit from
    // stopping an otherwise live source scene.
    static bool needs_live_render_settle(unsigned queued_pipelines) noexcept
    {
        return queued_pipelines != 0;
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

    bool arm(bool submitted_work_complete) noexcept
    {
        if (phase_ != Phase::Arming || !submitted_work_complete) return false;
        phase_ = Phase::Idle;
        quiet_frames_ = 0;
        return true;
    }

    bool busy() const noexcept { return phase_ != Phase::Idle; }

    // Priming and settling intentionally draw an unchanged source scene so
    // WebGPU can discover and compile its pipelines before simulation starts.
    // A pending transition must not starve a first-use settle discovered by
    // the outgoing scene's final draw. Finish that frozen scene before the
    // transition takes ownership on the next idle callback.
    bool suppress_source_draw(bool pending_transition = false) const noexcept
    {
        return phase_ == Phase::WaitingForAudio || phase_ == Phase::Constructing ||
               phase_ == Phase::Arming || (pending_transition && !warming());
    }

    Phase phase() const noexcept { return phase_; }

private:
    Phase phase_ = Phase::Idle;
    unsigned quiet_frames_ = 0;
};

} // namespace melee_web
