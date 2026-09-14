// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Common/CommonTypes.h"
struct GCPadStatus;
namespace Core { class System; }
namespace ReferenceCapture
{
// Host SI input only. No guest addresses, gameplay state, or CPU decisions.
class InputStream final
{
public:
  static bool Initialize();
  static bool IsReplaying();
  static bool IsRecording();
  static void Record(Core::System& system, u32 port, const GCPadStatus& pad);
  static GCPadStatus Replay(Core::System& system, u32 port);
  static bool AdapterConnected(Core::System& system, u32 port, bool connected);
  static void RequestFinish(bool natural);
  static bool WaitComplete();
};
}
