// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "Common/CommonTypes.h"

namespace Core
{
class System;
}
namespace PowerPC
{
struct PowerPCState;
}

namespace ReferenceAllocation
{

// Identity metadata generated from the owned DOL and pinned source symbols.
// The observer never treats an observed allocation address as a model input.
struct FunctionIdentity
{
  const char* name = nullptr;
  u32 address = 0;
  u32 size = 0;
  u32 entry_word = 0;
  u32 argc = 0;
  const u32* returns = nullptr;
  u32 return_count = 0;
  const char* body_sha256 = nullptr;
};

struct GlobalIdentity
{
  const char* name = nullptr;
  u32 address = 0;
  u32 size = 0;
};

struct BoundProfile
{
  const char* dol_sha1 = nullptr;
  const char* source_revision = nullptr;
  const char* profile_sha256 = nullptr;
  u32 entry = 0;
  u32 entry_word = 0;
  const FunctionIdentity* functions = nullptr;
  u32 function_count = 0;
  const GlobalIdentity* globals = nullptr;
  u32 global_count = 0;
  bool generated_and_verified = false;
};

// This backend is intentionally separate from the gameplay reference
// observer. It is a read-only, source-owned allocation-history diagnostic that
// can later be wired into Dolphin's JIT boundary without changing MWRI/MWRO.
class Observer final
{
public:
  // Arm before the original DOL starts. This performs profile/path checks only;
  // it does not touch guest memory or create the output file.
  static bool Arm(const BoundProfile& profile, std::string_view output_path);
  // Start at the first observed original DOL entry boundary after Arm().
  static bool Start(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state);
  static bool Start(Core::System* system, PowerPC::PowerPCState* state);
  // Compatibility convenience for callers that already have the entry state.
  static bool Initialize(const BoundProfile& profile, Core::System* system,
                         PowerPC::PowerPCState* state, std::string_view output_path);
  static bool IsBoundary(u32 guest_pc);
  static void Observe(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state);
  static bool Finish(bool natural);
  static bool IsInitialized();
  static std::string Error();

private:
  Observer() = delete;
};

}  // namespace ReferenceAllocation
