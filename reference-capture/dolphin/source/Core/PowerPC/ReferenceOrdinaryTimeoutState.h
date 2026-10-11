// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace ReferenceCapture {
// Opt-in original competitive workload, not the fixed-neutral SD contract.
struct OrdinaryTimeoutState {
  static constexpr const char* policy_hash = "c58eb6635d8f7115db86bc3601372fcf685ccecbd6c15d59d570a207e6d4d91d";
  enum class Phase { NeutralReady, FirstLoss, Drain, NeutralTimeout, Exited, Retired };
  Phase phase = Phase::NeutralReady;
  std::uint32_t samples = 0, ticks = 0, preceding_samples = 0, directional = 0, frame = 0;
  std::uint32_t setup_samples = 0;
  bool previous_direction = false;
  bool Input(bool neutral, bool direction) {
    if ((!neutral && !direction) || phase == Phase::Exited || phase == Phase::Retired ||
        setup_samples > 123 || samples + setup_samples >= 29523)
      return false;
    if (direction && ((phase != Phase::FirstLoss && phase != Phase::Drain) || directional >= 600))
      return false;
    if (neutral && phase == Phase::FirstLoss && directional) return false;
    ++samples;
    if (direction) ++directional;
    else if (phase == Phase::Drain) phase = Phase::NeutralTimeout;
    previous_direction = direction;
    return true;
  }
  bool Tick(std::uint32_t counter, std::uint32_t current_frame, std::uint32_t seconds,
            std::uint32_t subframe, unsigned p1, unsigned p2, std::uint32_t x1, std::uint32_t x2) {
    if (phase == Phase::Exited || phase == Phase::Retired || counter != ticks || ticks >= 29523 ||
        samples <= preceding_samples || current_frame > 28800 || (!ticks && current_frame != 0) ||
        (current_frame != frame && current_frame != frame + 1) ||
        seconds != 480 - (current_frame + 59) / 60 || subframe != (current_frame + 59) % 60 ||
        p2 != 4 || (p1 != 3 && p1 != 4)) return false;
    if (phase == Phase::NeutralReady) {
      if (p1 != 4 || current_frame > 180) return false;
      if (current_frame == 180) {
        if (x1 != 0xc2700000 || x2 != 0x42700000) return false;
        phase = Phase::FirstLoss;
      }
    } else if (phase == Phase::FirstLoss && p1 == 3) {
      if (!directional || !previous_direction) return false;
      phase = Phase::Drain;
    } else if ((phase == Phase::Drain || phase == Phase::NeutralTimeout) && p1 != 3) return false;
    frame = current_frame; ++ticks; preceding_samples = samples;
    return true;
  }
  bool Exit(std::uint32_t counter, std::uint32_t current_frame, std::uint32_t seconds,
            std::uint32_t subframe, unsigned p1, unsigned p2) {
    if (phase != Phase::NeutralTimeout || counter != ticks || frame != 28800 ||
        current_frame != 28800 || seconds || subframe != 59 || p1 != 3 || p2 != 4) return false;
    phase = Phase::Exited; return true;
  }
  bool Retire() {
    if (phase != Phase::Exited) return false;
    phase = Phase::Retired; return true;
  }
};
}
