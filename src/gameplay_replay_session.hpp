#pragma once

#include "gameplay_match_session.hpp"
#include "gameplay_replay_transport.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace melee_web {

class GameplayReplaySession {
public:
    GameplayReplaySession(const RuntimeFiles&, const GameplayReplayTransport&);
    GameplayReplaySession(const GameplayReplaySession&) = delete;
    GameplayReplaySession& operator=(const GameplayReplaySession&) = delete;

    bool step();
    bool complete() const noexcept;
    std::size_t frames_consumed() const noexcept { return cursor_; }
    bool failed() const noexcept { return failure_.has_value(); }
    const std::optional<std::string>& failure() const noexcept { return failure_; }
    GameplayMatchSession& match() noexcept { return match_; }
private:
    void fail(std::string);

    const GameplayReplayTransport& replay_;
    GameplayMatchSession match_;
    std::size_t cursor_ = 0;
    std::optional<std::string> failure_;
};

} // namespace melee_web
