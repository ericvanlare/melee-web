// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

namespace Core
{
class System;
}
namespace PowerPC
{
struct PowerPCState;
}
namespace DiscIO
{
class VolumeDisc;
}

namespace ReferenceCapture
{

// This observer is deliberately a JITARM64-only, read-only diagnostic hook.
// It is inactive unless all MWRC_* activation variables are present and match
// the pinned reference identity; normal Dolphin runs pay no observer cost.
class Observer final
{
public:
  static bool IsEnabled();
  static bool IsBoundary(u32 guest_pc);
  static void OnBoundary(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state);
  static void Fail(const char* reason);
  // The environment hash is only an activation key.  Boot calls this helper
  // to hash the DOL actually read from the selected disc before emulation.
  static bool ValidateDiscDOL(const DiscIO::VolumeDisc& volume);

private:
  Observer();
  ~Observer();
  Observer(const Observer&) = delete;
  Observer& operator=(const Observer&) = delete;

  static Observer& Instance();
  void Observe(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state);
  void Stop();

  struct Impl;
  Impl* m_impl;
};

}  // namespace ReferenceCapture
