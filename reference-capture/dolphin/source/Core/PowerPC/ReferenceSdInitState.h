// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace ReferenceCapture {
// Diagnostic prefix ownership only; never a whole-session completion owner.
struct SdInitState {
  enum class Phase { Menu, VsSetup, VsActive, VsExited, VsRetired, SdSetup, Complete };
  Phase phase = Phase::Menu;
  std::uint32_t setup_pointer = 0;
  std::uint32_t consumed = 0;
  static constexpr std::uint32_t sample_cap = 4323;
  bool Entry(std::uint32_t pointer, bool sudden) {
    if (!pointer || phase != (sudden ? Phase::VsRetired : Phase::Menu)) return false;
    setup_pointer = pointer;
    phase = sudden ? Phase::SdSetup : Phase::VsSetup;
    return true;
  }
  bool Ready(bool sudden, bool profile_entry = false) {
    if (profile_entry && sudden) return false;
    if (phase != (sudden ? Phase::SdSetup : Phase::VsSetup)) return false;
    phase = (sudden || profile_entry) ? Phase::Complete : Phase::VsActive;
    return true;
  }
  bool Exit() {
    if (phase != Phase::VsActive) return false;
    phase = Phase::VsExited;
    return true;
  }
  bool Retire() {
    if (phase != Phase::VsExited) return false;
    setup_pointer = 0;
    phase = Phase::VsRetired;
    return true;
  }
  bool Consume(std::uint32_t cap = sample_cap) {
    if (phase == Phase::Menu || phase == Phase::Complete || consumed >= cap) return false;
    ++consumed;
    return true;
  }
};
}
