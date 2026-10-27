from __future__ import annotations

import gzip
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tools.generate_reference_allocation_profile import (
    FUNCTIONS,
    GLOBALS,
    LEGACY_FUNCTIONS,
    LEGACY_PROFILE_VERSION,
    PROFILE_SCHEMA,
    PROFILE_VERSION,
    render_header,
    validate_profile,
)
from tools.retail_allocation_profile import DOL_SHA1, SOURCE_REVISION


ROOT = Path(__file__).parents[1]
OBSERVER = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceAllocationObserver.cpp"
sys.path.insert(0, str(ROOT / "scripts"))
from capture_allocation_history import (
    read_stadium_paired_allocation_rows,
    validate_stadium_paired_allocation_inventory,
    validate_stadium_paired_allocation_scope,
)
from tools.allocation_lifetime_replay import validate_original_rng_scene_boundaries_stream


def synthetic_profile() -> dict:
    functions = []
    for index, (name, argc) in enumerate(FUNCTIONS.items()):
        address = 0x80010000 + index * 0x100
        functions.append({
            "name": name,
            "address": address,
            "size": 8,
            "entry_word": 0x12340000,
            "argc": argc,
            "returns": [address + 4],
            "body_sha256": "0" * 64,
        })
    globals_value = {
        name: {"address": 0x81000000 + index * 0x100, "size": 4}
        for index, name in enumerate(GLOBALS)
    }
    return {
        "schema": PROFILE_SCHEMA,
        "version": PROFILE_VERSION,
        "dol_sha1": DOL_SHA1,
        "source_revision": SOURCE_REVISION,
        "symbols_sha256": "0" * 64,
        "entry": functions[0]["address"],
        "entry_word": 0x12340000,
        "functions": functions,
        "globals": globals_value,
        "initial_dol_words": {
            "seed_ptr": 0, "__OSArenaLo": 0, "current_heap": 0,
            "iparam_audio_heap_size": 0, "iparam_heap_max_num": 0,
        },
    }


COMPONENT = r"""
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

#include "Common/DirectIOFile.h"
#include "Core/System.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/ReferenceAllocationObserver.h"

using ReferenceAllocation::BoundProfile;
using ReferenceAllocation::FunctionIdentity;
using ReferenceAllocation::GlobalIdentity;
using ReferenceAllocation::Observer;
using u8 = std::uint8_t;
using u32 = std::uint32_t;

namespace {
constexpr const char* kDol = "08e0bf20134dfcb260699671004527b2d6bb1a45";
constexpr const char* kRevision = "b43912cc78606f96c9569f5d6229bc9d7e265ea5";
constexpr std::array<const char*, 52> kNames = {{
  "ARInit", "ARAlloc", "ARFree", "ARGetSize", "OSInitAlloc", "OSCreateHeap",
  "OSDestroyHeap", "OSSetCurrentHeap", "OSAllocFromHeap", "OSFreeToHeap",
  "OSSetArenaLo", "OSSetArenaHi", "OSAllocFromArenaLo", "OSAllocFromArenaHi",
  "HSD_OSInit", "HSD_AllocateXFB", "HSD_AllocateFifo", "HSD_CreateMainHeap",
  "HSD_SetHeap", "HSD_ObjSetHeap", "HSD_ObjAllocInit", "HSD_ObjAllocAddFree",
  "HSD_ObjAlloc", "HSD_ObjFree", "_HSD_ObjAllocForgetMemory", "HSD_MemAlloc",
  "HSD_Free", "lbHeap_80015F3C", "lbHeap_800158D0", "lbHeap_80015900",
  "lbHeap_80015BD0", "lbHeap_80015CA8", "lbHeap_80015D6C", "lbMemory_8001564C",
  "lbMemory_80014E24", "lbMemory_80014EEC", "lbMemory_80014FC8", "lbMemFreeToHeap",
  "lbMemory_8001529C", "lbMemory_800154D4", "lbMemory_800155A4",
  "Fighter_FirstInitialize_80067A84", "Fighter_Create", "gm_Scene_Vs_OnEnter",
  "gm_Scene_Vs_OnExit", "fn_80015184", "lbMemory_80015320", "lbDvd_80017A80",
  "HSD_DevComARAMCallback", "HSD_DevComRequest", "ftDemo_CreateFighter",
  "ftDemo_ObjAllocInit"
}};
constexpr std::array<const char*, 23> kGlobals = {{
  "HeapArray", "NumHeaps", "ArenaStart", "ArenaEnd", "__OSArenaLo", "__OSArenaHi",
  "seed", "seed_ptr", "fighter_alloc_data", "obj_heap", "current_heap", "__OSCurrHeap",
  "lbHeap_80431FA0", "lbMemory_804318B0", "iparam_audio_heap_size",
  "iparam_heap_max_num", "hsd_heap_next_arena_lo", "hsd_heap_next_arena_hi",
  "__AR_Size", "__AR_StackPointer", "__AR_FreeBlocks", "__AR_BlockLength",
  "__AR_init_flag"
}};
constexpr std::array<u32, 52> kArgc = {{
  2, 1, 1, 0, 3, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 2, 1, 2, 1, 2, 3, 2, 1, 2, 2,
  1, 1, 0, 2, 0, 2, 2, 3, 0, 2, 1, 2, 2, 3, 2, 0, 0, 1, 1, 1, 2, 4, 1, 1, 8,
  1, 0
}};

struct Fixture {
  Core::System system;
  PowerPC::PowerPCState cpu;
  std::array<FunctionIdentity, 52> functions{};
  std::array<GlobalIdentity, 23> globals{};
  std::array<u32, 52> returns{};
  std::array<u32, 52> args{};
  BoundProfile profile{};

  Fixture() {
    for (size_t i = 0; i < functions.size(); ++i) {
      const u32 address = 0x80010000u + static_cast<u32>(i * 0x100);
      returns[i] = address + 4;
      functions[i] = {kNames[i], address, 8, 0x12340000u, kArgc[i], &returns[i], 1,
                      "0000000000000000000000000000000000000000000000000000000000000000"};
      system.memory.Write(address, 0x12340000u);
      system.memory.Write(returns[i], 0x4e800020u);
    }
    system.memory.Write(0x8000f000u, 0x12340000u);
    for (size_t i = 0; i < globals.size(); ++i) {
      globals[i] = {kGlobals[i], 0x81000000u + static_cast<u32>(i * 0x100),
                    std::string_view(kGlobals[i]) == "lbMemory_804318B0" ? 0x6f0u : 4u};
      system.memory.Write(globals[i].address, 0x1000u + static_cast<u32>(i));
    }
    system.memory.Write(globals[7].address, globals[6].address);
    system.memory.Write(globals[6].address, 0x89abcdefu);
    system.memory.Write(0x800000e4u, 0x80000100u);
    system.memory.Write(0x80479d58u, 1);
    system.memory.Write(0x804d7420u, 1);
    system.memory.Write(0x80000028u, 0x01800000u);
    system.memory.Write(0x80000030u, 0x80400000u);
    system.memory.Write(0x80000034u, 0x817fffe0u);
    system.memory.Write(0x800000f4u, 0x12345678u);
    cpu.pc = 0x8000f000u;
    cpu.gpr[1] = 0x81700000u;
    cpu.gpr[2] = 0x81200000u;
    cpu.gpr[3] = 0x80020000u;
    cpu.gpr[13] = 0x81300000u;
    cpu.spr[8] = 0x8000abcd;
    cpu.spr[9] = 7;
    profile = {kDol, kRevision,
               "0000000000000000000000000000000000000000000000000000000000000000",
               0x8000f000u, 0x12340000u, functions.data(),
               static_cast<u32>(functions.size()),
               globals.data(), static_cast<u32>(globals.size()), true};
  }
};

int Run(std::string_view mode, const char* output) {
  Fixture fixture;
  if (mode == "range") {
    fixture.globals[0].address = 0x81800000u;
    assert(!Observer::Arm(fixture.profile, output));
    return 0;
  }
  if (mode == "invalid-config") {
    assert(!Observer::Arm(fixture.profile, output));
    return 0;
  }
  if (mode == "paired-refuse") {
    assert(!Observer::Arm(fixture.profile, output));
    assert(Observer::Error().find("scene endpoint profile requires exactly two VS exits") !=
           std::string::npos);
    return 0;
  }
  if (mode == "paired-cap-refuse") {
    assert(!Observer::Arm(fixture.profile, output));
    assert(Observer::Error().find("scene endpoint profile fixes MWRC_ALLOCATION_MAX_EVENTS at 64") !=
           std::string::npos);
    return 0;
  }
  if (mode == "paired-unknown") {
    assert(!Observer::Arm(fixture.profile, output));
    assert(Observer::Error().find("unknown profile") != std::string::npos);
    return 0;
  }
  if (mode == "paired-duplicate") {
    fixture.functions[45].name = "gm_Scene_Vs_OnEnter";
    assert(!Observer::Arm(fixture.profile, output));
    assert(Observer::Error().find("requires one exact generated scene callback identity") !=
           std::string::npos);
    return 0;
  }
  assert(Observer::Arm(fixture.profile, output));
  assert(Observer::IsBoundary(fixture.profile.entry));
  assert(!Observer::IsInitialized());
  if (mode == "wrong-instruction") {
    fixture.system.memory.Write(fixture.profile.entry, 0xdeadbeefu);
    Observer::Observe(&fixture.system, fixture.profile.entry, &fixture.cpu);
    assert(!Observer::IsInitialized());
    assert(Observer::Error().find("entry instruction") != std::string::npos);
    return 0;
  }
  fixture.cpu.pc = 0xcafebabeu;  // The callback PC is authoritative at a JIT boundary.
  assert(Observer::Start(&fixture.system, fixture.profile.entry, &fixture.cpu));
  assert(Observer::IsInitialized());
  if (mode == "null-state") {
    Observer::Observe(&fixture.system, fixture.functions[0].address, nullptr);
    assert(Observer::Error().find("null CPU state") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  if (mode == "finish-race") {
    fixture.system.memory.block_reads.store(true);
    std::atomic<bool> finished{false};
    bool finish_result = false;
    std::thread observer([&] {
      Observer::Observe(&fixture.system, fixture.functions[0].address, &fixture.cpu);
    });
    while (!fixture.system.memory.read_started.load())
      std::this_thread::yield();
    std::thread finisher([&] {
      finish_result = Observer::Finish(false);
      finished.store(true);
    });
    for (int i = 0; i < 1000 && !finished.load(); ++i)
      std::this_thread::yield();
    assert(!finished.load());
    fixture.system.memory.release_reads.store(true);
    observer.join();
    finisher.join();
    assert(!finish_result);
    assert(Observer::Error().find("pending source calls") != std::string::npos);
    return 0;
  }
  if (mode == "missing-return") {
    fixture.cpu.pc = fixture.returns[0];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("no source frame") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  if (mode == "unmatched-return") {
    Observer::Observe(&fixture.system, fixture.functions[0].address, &fixture.cpu);
    fixture.cpu.gpr[1] += 4;
    fixture.cpu.pc = fixture.returns[0];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("function, sp, and lr") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  if (mode == "valid-aram" || mode == "ordered-output") {
    Observer::Observe(&fixture.system, fixture.functions[0].address, &fixture.cpu);
    fixture.cpu.pc = fixture.returns[0];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    if (!Observer::Finish(false)) {
      std::fprintf(stderr, "valid-aram error: %s\n", Observer::Error().c_str());
      return 3;
    }
    if (mode == "ordered-output")
      assert(File::DirectIOFile::write_calls.load() <= 3);
    return 0;
  }
  if (mode == "heap-metadata") {
    fixture.system.memory.Write(fixture.globals[0].address, 0x80020000u);
    fixture.system.memory.Write(fixture.globals[1].address, 2);
    fixture.system.memory.Write(fixture.globals[2].address, 0x80400000u);
    fixture.system.memory.Write(fixture.globals[3].address, 0x817fffe0u);
    fixture.system.memory.Write(fixture.globals[4].address, 0x80500000u);
    fixture.system.memory.Write(fixture.globals[5].address, 0x81700000u);
    fixture.system.memory.Write(fixture.globals[10].address, 3);
    fixture.system.memory.Write(fixture.globals[11].address, 4);
    for (u32 index = 0; index < 6; ++index)
      fixture.system.memory.Write(0x80020000u + index * 4, 0x10000000u + index);
    fixture.cpu.pc = fixture.functions[4].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.pc = fixture.returns[4];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Finish(false));
    return 0;
  }
  if (mode == "compaction-metadata") {
    const u32 manager = fixture.globals[13].address;
    const auto write_manager = [&](u32 offset, u32 value) {
      fixture.system.memory.Write(manager + offset, value);
    };
    write_manager(0x6c8, 0x80040000u);
    write_manager(0x6cc, 0x80060000u);
    write_manager(0x6d0, 0x22000u);
    write_manager(0x6d4, 0x1000u);
    write_manager(0x6d8, 0x80021000u);
    write_manager(0x6dc, 0x80015320u);
    write_manager(0x6e0, 0x80022000u);
    write_manager(0x6e4, 0x80041000u);
    write_manager(0x6e8, 0x80017a80u);
    const u32 handle = 0x80020000u;
    fixture.system.memory.Write(handle + 0x0, 0x80020010u);
    fixture.system.memory.Write(handle + 0x4, 0x80040000u);
    fixture.system.memory.Write(handle + 0x8, 0x1000u);
    fixture.system.memory.Write(handle + 0xc, 0x80020020u);
    const auto invoke = [&](size_t index) {
      fixture.cpu.pc = fixture.functions[index].address;
      Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
      fixture.cpu.pc = fixture.returns[index];
      Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
      assert(Observer::Error().empty());
    };
    fixture.cpu.gpr[3] = handle;
    fixture.cpu.gpr[4] = 0x80025000u;
    fixture.cpu.gpr[5] = 0x80026000u;
    invoke(38);
    fixture.cpu.gpr[3] = 0;
    fixture.cpu.gpr[4] = handle;
    fixture.cpu.gpr[5] = 0x80027000u;
    fixture.cpu.gpr[6] = 0;
    invoke(46);
    fixture.cpu.gpr[4] = 0;
    invoke(46);
    fixture.cpu.gpr[3] = 0x80028000u;
    fixture.cpu.gpr[4] = 0x80029000u;
    invoke(45);
    fixture.cpu.gpr[3] = 0x8002a000u;
    invoke(47);
    fixture.cpu.gpr[3] = 0x8002b000u;
    invoke(48);
    for (u32 index = 0; index < 8; ++index)
      fixture.cpu.gpr[3 + index] = 0x80030000u + index * 0x100u;
    invoke(49);
    assert(Observer::Finish(false));
    return 0;
  }
  if (mode == "compaction-invalid") {
    const u32 manager = fixture.globals[13].address;
    fixture.system.memory.Write(manager + 0x6d0, 0x100u);
    fixture.system.memory.Write(manager + 0x6d4, 0x200u);
    fixture.cpu.gpr[3] = 0x80028000u;
    fixture.cpu.gpr[4] = 0x80029000u;
    fixture.cpu.pc = fixture.functions[45].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("metadata escaped") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  if (mode == "write-failure") {
    Observer::Observe(&fixture.system, fixture.functions[0].address, &fixture.cpu);
    fixture.cpu.pc = fixture.returns[0];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(!Observer::Finish(false));
    assert(Observer::Error().find("write") != std::string::npos);
    return 0;
  }
  if (mode == "fighter-invalid") {
    const size_t fighter = 42;
    fixture.cpu.pc = fixture.functions[fighter].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.gpr[3] = 0xffffffffu;
    fixture.cpu.pc = fixture.returns[fighter];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("metadata escaped") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  if (mode == "demo-fighter") {
    const u32 gobj = 0x8002c000u;
    const u32 fighter = 0x8002d000u;
    fixture.system.memory.Write(gobj + 0x2cu, fighter);
    fixture.system.memory.Write(fighter + 0x4u, 7u);
    fixture.system.memory.Write(fighter + 0xcu, 0x02000000u);
    fixture.cpu.gpr[3] = 0x8002a000u;
    fixture.cpu.pc = fixture.functions[50].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.gpr[3] = gobj;
    fixture.cpu.pc = fixture.returns[50];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.pc = fixture.functions[51].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.pc = fixture.returns[51];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Finish(false));
    return 0;
  }
  if (mode == "demo-fighter-invalid") {
    fixture.cpu.pc = fixture.functions[50].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.gpr[3] = 0xffffffffu;
    fixture.cpu.pc = fixture.returns[50];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("metadata escaped") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  const auto emit_burst_pair = [&](size_t index) {
    fixture.cpu.gpr[3] = 0x80020000u + static_cast<u32>(index * 4);
    fixture.cpu.pc = fixture.functions[0].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.pc = fixture.returns[0];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
  };
  if (mode == "paused-burst" || mode == "queue-overflow") {
    const size_t initial_pairs = mode == "paused-burst" ? 3000 : 2000;
    if (mode == "paused-burst") {
      for (size_t index = 0; index < initial_pairs; ++index)
        emit_burst_pair(index);
    } else {
      // Use the bounded descriptor form to make the byte cap, rather than
      // the slot-count cap, the first failure in this stress fixture.
      fixture.system.memory.Write(fixture.globals[0].address, 0x80020000u);
      fixture.system.memory.Write(fixture.globals[1].address, 32);
      fixture.system.memory.Write(fixture.globals[2].address, 0x80400000u);
      fixture.system.memory.Write(fixture.globals[3].address, 0x817fffe0u);
      fixture.system.memory.Write(fixture.globals[4].address, 0x80500000u);
      fixture.system.memory.Write(fixture.globals[5].address, 0x81700000u);
      fixture.system.memory.Write(fixture.globals[10].address, 3);
      fixture.system.memory.Write(fixture.globals[11].address, 4);
      for (u32 index = 0; index < 96; ++index)
        fixture.system.memory.Write(0x80020000u + index * 4, 0x10000000u + index);
      const auto emit_heap_pair = [&](size_t index) {
        fixture.cpu.gpr[3] = 0x80020000u + static_cast<u32>(index * 4);
        fixture.cpu.pc = fixture.functions[4].address;
        Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
        fixture.cpu.pc = fixture.returns[4];
        Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
      };
      for (size_t index = 0; index < initial_pairs; ++index)
        emit_heap_pair(index);
      for (size_t index = initial_pairs; index < 20000 && Observer::Error().empty(); ++index)
        emit_heap_pair(index);
    }
    for (size_t index = 0; index < 100000 && !File::DirectIOFile::write_started.load(); ++index)
      std::this_thread::yield();
    assert(File::DirectIOFile::write_started.load());
    if (mode == "queue-overflow") {
      assert(Observer::Error().find("queued byte bound") != std::string::npos);
    }
    File::DirectIOFile::release_writes.store(true);
    if (mode == "paused-burst")
      assert(Observer::Finish(false));
    else
      assert(!Observer::Finish(false));
    return 0;
  }
  if (mode == "vs-boundary") {
    const size_t vs = 43;
    fixture.cpu.pc = fixture.functions[vs].address;
    fixture.cpu.spr[8] = 0x8000beefu;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.pc = fixture.returns[vs];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Finish(true));
    return 0;
  }
  if (mode == "rng-null" || mode == "rng-outside-mem1" || mode == "rng-unaligned") {
    const u32 pointer = mode == "rng-null" ? 0u :
                        mode == "rng-outside-mem1" ? 0x81800000u : 0x81000601u;
    fixture.system.memory.Write(fixture.globals[7].address, pointer);
    fixture.cpu.pc = fixture.functions[43].address;
    fixture.cpu.spr[8] = 0x8000beefu;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("metadata escaped a bounded source identity") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  const auto invoke_vs = [&](size_t index) {
    fixture.cpu.pc = fixture.functions[index].address;
    fixture.cpu.spr[8] = 0x8000beefu;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.pc = fixture.returns[index];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().empty());
  };
  if (mode == "paired-scene") {
    assert(Observer::IsBoundary(fixture.profile.entry));
    assert(Observer::IsBoundary(fixture.functions[43].address));
    assert(Observer::IsBoundary(fixture.returns[43]));
    assert(Observer::IsBoundary(fixture.functions[44].address));
    assert(Observer::IsBoundary(fixture.returns[44]));
    assert(!Observer::IsBoundary(fixture.functions[0].address));
    assert(!Observer::IsBoundary(fixture.returns[0]));
    // The endpoint-only profile must ignore unrelated admitted identities before
    // any frame or machine snapshot is created.
    fixture.cpu.pc = fixture.functions[0].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().empty());
    invoke_vs(43); invoke_vs(44); invoke_vs(43); invoke_vs(44);
    assert(Observer::Finish(true));
    return 0;
  }
  if (mode == "vs-reentry") {
    invoke_vs(43);
    invoke_vs(44);
    invoke_vs(43);
    assert(Observer::Finish(false));
    return 0;
  }
  if (mode == "vs-interrupted") {
    invoke_vs(43);
    invoke_vs(44);
    assert(Observer::Finish(false));
    return 0;
  }
  if (mode == "vs-three-exits" || mode == "vs-two-exits") {
    const int count = mode == "vs-two-exits" ? 2 : 3;
    for (int generation = 0; generation < count; ++generation) {
      invoke_vs(43);
      invoke_vs(44);
    }
    assert(Observer::Finish(true));
    return 0;
  }
  if (mode == "vs-two-then-third") {
    invoke_vs(43); invoke_vs(44); invoke_vs(43); invoke_vs(44);
    assert(Observer::Finish(true));
    std::ifstream before_file(output,std::ios::binary);
    const std::string before((std::istreambuf_iterator<char>(before_file)),{});
    invoke_vs(43); invoke_vs(44);
    assert(Observer::Finish(true));
    std::ifstream after_file(output,std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(after_file)),{});
    assert(!before.empty() && before==after);
    return 0;
  }
  if (mode == "vs-unmatched-exit") {
    fixture.cpu.pc = fixture.functions[44].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("no active source owner") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  if (mode == "vs-reentrant-entry") {
    fixture.cpu.pc = fixture.functions[43].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.cpu.gpr[1] -= 4;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("reentrant") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  if (mode == "vs-duplicate-exit") {
    invoke_vs(43);
    invoke_vs(44);
    fixture.cpu.pc = fixture.functions[44].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("no active source owner") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  if (mode == "vs-overlap-return") {
    fixture.cpu.pc = fixture.functions[43].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.system.memory.Write(0x800000e4u, 0x80000200u);
    fixture.cpu.pc = fixture.functions[0].address;
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    fixture.system.memory.Write(0x800000e4u, 0x80000100u);
    fixture.cpu.pc = fixture.returns[43];
    Observer::Observe(&fixture.system, fixture.cpu.pc, &fixture.cpu);
    assert(Observer::Error().find("other source calls active") != std::string::npos);
    assert(!Observer::Finish(false));
    return 0;
  }
  return 2;
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 3);
  return Run(argv[1], argv[2]);
}
"""


STUBS = {
    "Common/CommonTypes.h": r"""
#pragma once
#include <cstddef>
#include <cstdint>
using u8 = std::uint8_t; using u32 = std::uint32_t; using u64 = std::uint64_t;
""",
    "Common/DirectIOFile.h": r"""
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
using u8 = std::uint8_t;
namespace File {
enum class AccessMode { Write };
enum class OpenMode { Create };
class DirectIOFile {
 public:
  DirectIOFile(const std::string& path, AccessMode, OpenMode) : stream(path, std::ios::binary | std::ios::trunc) {
    write_calls.store(0);
    fail_writes.store(path.find("write-failure") != std::string::npos);
    pause_writes.store(path.find("paused-burst") != std::string::npos ||
                       path.find("queue-overflow") != std::string::npos);
    release_writes.store(!pause_writes.load());
    write_started.store(false);
  }
  bool IsOpen() const { return stream.is_open(); }
  bool Write(const u8* data, size_t size) {
    ++write_calls;
    if (fail_writes.load()) return false;
    if (pause_writes.load()) {
      write_started.store(true);
      while (!release_writes.load()) std::this_thread::yield();
    }
    stream.write(reinterpret_cast<const char*>(data), size); return stream.good();
  }
  bool Flush() { stream.flush(); return stream.good(); }
  bool Close() { stream.close(); return !stream.fail(); }
  inline static std::atomic<size_t> write_calls{0};
  inline static std::atomic<bool> fail_writes{false};
  inline static std::atomic<bool> pause_writes{false};
  inline static std::atomic<bool> release_writes{false};
  inline static std::atomic<bool> write_started{false};
 private:
  std::ofstream stream;
};
}
""",
    "Core/HW/Memmap.h": r"""
    #pragma once
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <thread>
#include <vector>
using u8 = std::uint8_t; using u32 = std::uint32_t;
namespace Core {
class Memory {
 public:
  Memory() : bytes(0x1800000) {}
  const u8* GetPointerForRange(u32 address, size_t size) const {
    if (block_reads.load()) {
      read_started.store(true);
      while (!release_reads.load())
        std::this_thread::yield();
    }
    if (address < 0x80000000u || address - 0x80000000u > bytes.size() ||
        size > bytes.size() - (address - 0x80000000u)) return nullptr;
    return bytes.data() + address - 0x80000000u;
  }
  void Write(u32 address, u32 value) {
    const size_t offset = address - 0x80000000u;
    bytes[offset] = static_cast<u8>(value >> 24); bytes[offset + 1] = static_cast<u8>(value >> 16);
    bytes[offset + 2] = static_cast<u8>(value >> 8); bytes[offset + 3] = static_cast<u8>(value);
  }
  mutable std::atomic<bool> block_reads{false};
  mutable std::atomic<bool> read_started{false};
  mutable std::atomic<bool> release_reads{false};
 private:
  std::vector<u8> bytes;
};
}
""",
    "Core/PowerPC/PowerPC.h": r"""
#pragma once
#include <cstdint>
using u32 = std::uint32_t;
namespace PowerPC {
struct ConditionRegister { u32 value = 0; u32 Get() const { return value; } };
struct PowerPCState { u32 pc = 0; u32 gpr[32]{}; u32 spr[1024]{}; ConditionRegister cr{}; };
}
""",
    "Core/System.h": r"""
#pragma once
#include "Core/HW/Memmap.h"
namespace Core {
class System { public: Memory memory; Memory& GetMemory() { return memory; } };
}
""",
    "mbedtls/sha256.h": r"""
#pragma once
#include <cstddef>
#include <cstdint>
struct mbedtls_sha256_context {};
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context*, int) { return 0; }
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context*, const unsigned char*, size_t) { return 0; }
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context*, unsigned char* out) {
  for (size_t i = 0; i < 32; ++i) out[i] = 0; return 0;
}
""",
}


class ReferenceAllocationObserverTests(unittest.TestCase):
    def test_profile_validation_and_identity_only_header(self) -> None:
        profile = synthetic_profile()
        self.assertIs(validate_profile(profile), profile)
        rendered = render_header(profile, "a" * 64)
        self.assertIn("std::array<FunctionIdentity, 52>", rendered)
        self.assertIn("std::array<GlobalIdentity, 23>", rendered)
        self.assertIn("a" * 64, rendered)
        self.assertIn("0x12340000", rendered)

    def test_profile_generator_keeps_version_two_inventory_replay_readable(self) -> None:
        profile = synthetic_profile()
        profile["version"] = LEGACY_PROFILE_VERSION
        profile["functions"] = [function for function in profile["functions"]
                                if function["name"] in LEGACY_FUNCTIONS]
        self.assertEqual(len(profile["functions"]), 50)
        self.assertIs(validate_profile(profile), profile)
        with self.assertRaisesRegex(ValueError, "cannot render the current observer header"):
            render_header(profile, "a" * 64)

    def test_profile_rejects_unpinned_body_or_return_shape(self) -> None:
        profile = synthetic_profile()
        profile["functions"][0]["body_sha256"] = "bad"
        with self.assertRaises(ValueError):
            validate_profile(profile)
        profile = synthetic_profile()
        profile["functions"][0]["returns"] = [profile["functions"][0]["address"] + 2]
        with self.assertRaises(ValueError):
            validate_profile(profile)
        profile = synthetic_profile()
        profile["source_revision"] = "unverified"
        with self.assertRaises(ValueError):
            validate_profile(profile)
        profile = synthetic_profile()
        profile["functions"][0]["argc"] += 1
        with self.assertRaises(ValueError):
            validate_profile(profile)

    def test_generator_check_mode_is_deterministic(self) -> None:
        profile = synthetic_profile()
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            profile_path = directory / "profile.json"
            header_path = directory / "profile.h"
            profile_path.write_text(json.dumps(profile), encoding="utf-8")
            command = [sys.executable, str(ROOT / "tools/generate_reference_allocation_profile.py"),
                       "--profile", str(profile_path), "--output", str(header_path)]
            self.assertEqual(subprocess.run(command, capture_output=True, text=True).returncode, 0)
            self.assertEqual(
                subprocess.run(command + ["--check"], capture_output=True, text=True).returncode, 0)
            header_path.write_text(header_path.read_text(encoding="utf-8") + "\n", encoding="utf-8")
            self.assertNotEqual(
                subprocess.run(command + ["--check"], capture_output=True, text=True).returncode, 0)

    def test_compiled_component_protocol_and_json(self) -> None:
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("A native C++ compiler is not installed")
        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary)
            for name, content in STUBS.items():
                target = temp / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(content)
            component = temp / "component.cpp"
            component.write_text(COMPONENT)
            binary = temp / "component"
            built = subprocess.run(
                [compiler, "-std=c++20", "-fno-exceptions", "-Wall", "-Wextra", "-Werror",
                 "-I", str(temp), "-I", str(ROOT / "reference-capture/dolphin/source"),
                 str(OBSERVER), str(component), "-lz", "-o", str(binary)],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            def run_mode(mode: str, output_name: str | None = None,
                         **environment: str) -> subprocess.CompletedProcess[str]:
                env = os.environ.copy()
                for key in ("MWRC_ALLOCATION_VS_TARGET", "MWRC_ALLOCATION_STOP_AT",
                            "MWRC_ALLOCATION_MAX_EVENTS",
                            "MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE"):
                    env.pop(key, None)
                env.update(environment)
                output = temp / (output_name or (mode + ".jsonl"))
                return subprocess.run([str(binary), mode, str(output)],
                                      capture_output=True, text=True, timeout=20, env=env)

            for mode in ("range", "wrong-instruction", "missing-return", "unmatched-return",
                         "null-state", "finish-race", "write-failure", "fighter-invalid",
                         "demo-fighter", "demo-fighter-invalid",
                         "paused-burst", "queue-overflow", "compaction-invalid",
                         "vs-unmatched-exit", "vs-reentrant-entry", "vs-overlap-return",
                         "rng-null", "rng-outside-mem1", "rng-unaligned"):
                checked = run_mode(mode)
                self.assertEqual(checked.returncode, 0, f"{mode}: {checked.stderr}")
            demo_rows = [json.loads(line) for line in
                         (temp / "demo-fighter.jsonl").read_text().splitlines()]
            demo_return = next(row for row in demo_rows
                               if row["record"] == "return" and row["function"] == "ftDemo_CreateFighter")
            self.assertEqual(demo_return["observed"]["fighter"],
                             {"gobj": 0x8002c000, "address": 0x8002d000,
                              "slot": 2, "kind": 7})
            checked = run_mode("write-failure", output_name="write-failure.jsonl.gz")
            self.assertEqual(checked.returncode, 0, f"write-failure gzip: {checked.stderr}")
            checked = run_mode("vs-duplicate-exit", MWRC_ALLOCATION_VS_TARGET="2")
            self.assertEqual(checked.returncode, 0, f"vs-duplicate-exit: {checked.stderr}")
            overlap = [json.loads(line) for line in
                       (temp / "vs-overlap-return.jsonl").read_text().splitlines()]
            overlap_end = overlap[-1]
            self.assertEqual(overlap_end["status"], "incomplete")
            self.assertFalse(overlap_end["target_complete"])
            self.assertFalse(overlap_end["boundary_complete"])
            overflow_rows = [json.loads(line) for line in
                             (temp / "queue-overflow.jsonl").read_text().splitlines()]
            overflow_sequences = [row["sequence"] for row in overflow_rows]
            self.assertEqual(overflow_sequences, list(range(len(overflow_sequences))))
            self.assertLess(len(overflow_rows), 8192)
            self.assertEqual([row["record"] for row in overflow_rows[-2:]], ["error", "end"])
            invalid_rows = [json.loads(line) for line in
                            (temp / "missing-return.jsonl").read_text().splitlines()]
            invalid_sequences = [row["sequence"] for row in invalid_rows]
            self.assertEqual(invalid_sequences, list(range(len(invalid_sequences))))
            self.assertEqual([row["record"] for row in invalid_rows[-2:]], ["error", "end"])
            output = temp / "valid-aram.jsonl"
            checked = subprocess.run([str(binary), "valid-aram", str(output)],
                                     capture_output=True, text=True, timeout=20)
            self.assertEqual(checked.returncode, 0, f"valid-aram: {checked.stderr}")
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertEqual(rows[0]["record"], "header")
            aram = next(row for row in rows if row["record"] == "enter")["observed"]["aram"]
            self.assertIn("__AR_Size", aram)
            self.assertNotIn("aram\":,", output.read_text())
            output = temp / "ordered-output.jsonl"
            checked = subprocess.run([str(binary), "ordered-output", str(output)],
                                     capture_output=True, text=True, timeout=20)
            self.assertEqual(checked.returncode, 0, f"ordered-output: {checked.stderr}")
            sequences = [json.loads(line)["sequence"] for line in output.read_text().splitlines()]
            self.assertEqual(sequences, sorted(sequences))
            output = temp / "heap-metadata.jsonl"
            checked = subprocess.run([str(binary), "heap-metadata", str(output)],
                                     capture_output=True, text=True, timeout=20)
            self.assertEqual(checked.returncode, 0, f"heap-metadata: {checked.stderr}")
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            heaps = next(row["observed"]["heaps"] for row in rows if row["record"] == "return")
            self.assertEqual(
                {key for key in heaps if key != "descriptors"},
                {"HeapArray", "NumHeaps", "ArenaStart", "ArenaEnd", "__OSCurrHeap",
                 "current_heap", "__OSArenaLo", "__OSArenaHi"},
            )
            self.assertEqual(heaps["descriptors"],
                             [[0x10000000 + index for index in range(3)],
                              [0x10000003 + index for index in range(3)]])
            output = temp / "compaction-metadata.jsonl"
            checked = subprocess.run([str(binary), "compaction-metadata", str(output)],
                                     capture_output=True, text=True, timeout=20)
            self.assertEqual(checked.returncode, 0, f"compaction-metadata: {checked.stderr}")
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            observed = [row["observed"] for row in rows if row["record"] in ("enter", "return")]
            compact = [row["compaction"] for row in observed if "compaction" in row]
            self.assertEqual({item["phase"] for item in compact}, {"entry", "return"})
            memory = next(item for item in compact if "handle" in item)
            self.assertEqual(memory["handle"]["x4_lo"], 0x80040000)
            self.assertEqual(memory["manager"]["chunk"], 0x19000)
            self.assertTrue(any(item.get("handle") is None for item in compact))
            alarm = next(item for item in compact if "alarm" in item)
            self.assertEqual(alarm["manager"]["remaining"], 0x21000)
            self.assertTrue(any("devcom" in item and "callback_arg" in item["devcom"]
                               for item in observed))
            request = next(item["devcom"] for item in observed
                           if "devcom" in item and "dest" in item["devcom"])
            self.assertEqual(request["callback_arg"], 0x80030700)
            output = temp / "vs-boundary.jsonl"
            checked = subprocess.run([str(binary), "vs-boundary", str(output)],
                                     capture_output=True, text=True, timeout=20)
            self.assertEqual(checked.returncode, 0, checked.stderr)
            end = [json.loads(line) for line in output.read_text().splitlines()][-1]
            self.assertTrue(end["boundary_complete"])
            self.assertFalse(end["ownership_complete"])
            self.assertEqual(end["status"], "captured")
            self.assertEqual(end["scope"], "vs_ownership")
            self.assertEqual(end["vs_target"], 1)
            self.assertEqual(end["stop_at"], "entry")
            self.assertTrue(end["target_complete"])
            self.assertEqual(end["vs_entries"], 1)
            self.assertEqual(end["vs_exits"], 0)
            self.assertTrue(end["active_owner"])
            self.assertEqual(end["max_events"], 2_000_000)

            output = temp / "vs-gzip.jsonl.gz"
            checked = run_mode("vs-reentry", output_name=output.name,
                               MWRC_ALLOCATION_VS_TARGET="2",
                               MWRC_ALLOCATION_MAX_EVENTS="10000")
            self.assertEqual(checked.returncode, 0, checked.stderr)
            with gzip.open(output, "rt", encoding="utf-8") as compressed:
                rows = [json.loads(line) for line in compressed]
            self.assertEqual(rows[0]["record"], "header")
            self.assertEqual(rows[0]["compression"], "gzip")
            self.assertEqual(rows[0]["max_events"], 10_000)
            self.assertEqual(rows[-1]["record"], "end")
            self.assertEqual(rows[-1]["compression"], "gzip")
            self.assertEqual(rows[-1]["max_events"], 10_000)
            self.assertEqual([row["sequence"] for row in rows], list(range(len(rows))))

            output = temp / "vs-reentry.jsonl"
            checked = run_mode("vs-reentry", MWRC_ALLOCATION_VS_TARGET="2")
            self.assertEqual(checked.returncode, 0, checked.stderr)
            end = [json.loads(line) for line in output.read_text().splitlines()][-1]
            self.assertEqual(end["status"], "captured")
            self.assertEqual(end["vs_target"], 2)
            self.assertEqual(end["stop_at"], "entry")
            self.assertTrue(end["target_complete"])
            self.assertEqual(end["vs_entries"], 2)
            self.assertEqual(end["vs_exits"], 1)
            self.assertTrue(end["active_owner"])
            self.assertFalse(end["ownership_complete"])

            output = temp / "vs-interrupted.jsonl"
            checked = run_mode("vs-interrupted", MWRC_ALLOCATION_VS_TARGET="2")
            self.assertEqual(checked.returncode, 0, checked.stderr)
            end = [json.loads(line) for line in output.read_text().splitlines()][-1]
            self.assertEqual(end["status"], "incomplete")
            self.assertFalse(end["target_complete"])
            self.assertEqual(end["vs_entries"], 1)
            self.assertEqual(end["vs_exits"], 1)
            self.assertFalse(end["active_owner"])
            self.assertFalse(end["ownership_complete"])

            output = temp / "vs-three-exits.jsonl"
            checked = run_mode("vs-three-exits", MWRC_ALLOCATION_VS_TARGET="3",
                               MWRC_ALLOCATION_STOP_AT="exit")
            self.assertEqual(checked.returncode, 0, checked.stderr)
            end = [json.loads(line) for line in output.read_text().splitlines()][-1]
            self.assertEqual(end["status"], "captured")
            self.assertEqual(end["vs_target"], 3)
            self.assertEqual(end["stop_at"], "exit")
            self.assertTrue(end["target_complete"])
            self.assertEqual(end["vs_entries"], 3)
            self.assertEqual(end["vs_exits"], 3)
            self.assertFalse(end["active_owner"])
            self.assertTrue(end["ownership_complete"])

            output = temp / "vs-two-exits.jsonl"
            checked = run_mode("vs-two-exits", MWRC_ALLOCATION_VS_TARGET="2",
                               MWRC_ALLOCATION_STOP_AT="exit")
            self.assertEqual(checked.returncode, 0, checked.stderr)
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertEqual(rows[0]["scope"], "vs_ownership")
            self.assertNotIn("scene_endpoint_profile", rows[0])
            self.assertNotIn("allocation_history_complete", rows[0])
            self.assertIs(rows[0]['original_rng_scene_boundaries_v1'], True)
            self.assertIs(rows[-1]['original_rng_scene_boundaries_v1'], True)
            samples = [row['observed']['original_rng'] for row in rows
                       if row.get('record') in ('enter', 'return') and
                       row.get('function') in ('gm_Scene_Vs_OnEnter', 'gm_Scene_Vs_OnExit')]
            self.assertEqual(len(samples), 8)
            from tools.allocation_lifetime_replay import validate_original_rng_scene_boundaries
            decoded = validate_original_rng_scene_boundaries(synthetic_profile(), rows[0], rows[-1], rows)
            self.assertEqual(decoded['sample_count'], 8)
            self.assertEqual([row['value'] for row in decoded['samples']], [0x89abcdef] * 8)
            for sample in samples:
                self.assertEqual(sample['pointer_slot_address'], 0x81000700)
                self.assertEqual(sample['pointer_value'], 0x81000600)
                self.assertEqual(sample['value_address'], 0x81000600)
                self.assertEqual(sample['value'], 0x89abcdef)
                self.assertEqual(sample['pointer_bytes'], [0x81, 0, 6, 0])
                self.assertEqual(sample['value_bytes'], [0x89, 0xab, 0xcd, 0xef])

            paired_env = {
                "MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE": "paired_idle_scene_endpoints_v1",
                "MWRC_ALLOCATION_VS_TARGET": "2",
                "MWRC_ALLOCATION_STOP_AT": "exit",
            }
            checked = run_mode("paired-scene", **paired_env)
            self.assertEqual(checked.returncode, 0, f"paired-scene: {checked.stderr}")
            paired_rows = [json.loads(line) for line in
                           (temp / "paired-scene.jsonl").read_text().splitlines()]
            header, end = paired_rows[0], paired_rows[-1]
            self.assertEqual(header["scope"], "scene-rng-endpoints-only")
            self.assertEqual(header["scene_endpoint_profile"], "paired_idle_scene_endpoints_v1")
            self.assertEqual(header["selected_functions"],
                             ["gm_Scene_Vs_OnEnter", "gm_Scene_Vs_OnExit"])
            self.assertEqual(header["max_events"], 64)
            self.assertIs(header["allocation_history_complete"], False)
            self.assertIs(header["registration_lifetime_complete"], False)
            self.assertEqual(end["scope"], "scene-rng-endpoints-only")
            self.assertEqual(end["scene_endpoint_profile"], "paired_idle_scene_endpoints_v1")
            self.assertIs(end["allocation_history_complete"], False)
            self.assertIs(end["registration_lifetime_complete"], False)
            self.assertEqual(end["vs_target"], 2)
            self.assertEqual(end["stop_at"], "exit")
            self.assertEqual((end["vs_entries"], end["vs_exits"]), (2, 2))
            self.assertIs(end["target_complete"], True)
            self.assertIs(end["active_owner"], False)
            self.assertEqual(end["pending_calls"], [])
            endpoint_rows = [row for row in paired_rows if row.get("record") in ("enter", "return")]
            self.assertEqual(len(endpoint_rows), 8)
            self.assertEqual({row["function"] for row in endpoint_rows},
                             {"gm_Scene_Vs_OnEnter", "gm_Scene_Vs_OnExit"})
            self.assertEqual([row["function"] for row in endpoint_rows],
                             ["gm_Scene_Vs_OnEnter", "gm_Scene_Vs_OnEnter",
                              "gm_Scene_Vs_OnExit", "gm_Scene_Vs_OnExit"] * 2)
            self.assertEqual([row["record"] for row in endpoint_rows],
                             ["enter", "return"] * 4)
            paired_samples = [row["observed"]["original_rng"] for row in endpoint_rows]
            self.assertEqual(len(paired_samples), 8)
            self.assertEqual([sample["value"] for sample in paired_samples],
                             [0x89abcdef] * 8)
            self.assertEqual({sample["pointer_slot_address"] for sample in paired_samples},
                             {0x81000700})
            actual_path = temp / "paired-scene.jsonl"
            actual_rows = read_stadium_paired_allocation_rows(actual_path)
            decoded, actual_header, actual_footer, actual_selected = \
                validate_original_rng_scene_boundaries_stream(synthetic_profile(), actual_rows)
            self.assertEqual(decoded["sample_count"], 8)
            self.assertEqual(len(actual_selected), 8)
            self.assertEqual(
                validate_stadium_paired_allocation_scope(
                    actual_header, actual_footer, actual_header["profile_sha256"]),
                actual_header["profile_sha256"],
            )
            inventory = validate_stadium_paired_allocation_inventory(
                actual_rows, actual_header, actual_footer)
            self.assertEqual(inventory["endpoint_rows"], 8)
            self.assertEqual(inventory["repeated_stop_rows"], 0)

            with self.assertRaisesRegex(ValueError, "scope/profile"):
                validate_stadium_paired_allocation_scope(
                    actual_header, dict(actual_footer, profile_sha256="1" * 64),
                    actual_header["profile_sha256"],
                )
            with self.assertRaisesRegex(ValueError, "scope/profile"):
                validate_stadium_paired_allocation_scope(
                    dict(actual_header, max_events=65), actual_footer,
                    actual_header["profile_sha256"],
                )
            injected = [dict(row) for row in actual_rows]
            injected.insert(1, dict(actual_rows[1], sequence=1, function="HSD_Unknown"))
            for sequence, row in enumerate(injected[:-1]):
                row["sequence"] = sequence
            with self.assertRaisesRegex(ValueError, "non-endpoint callback"):
                validate_stadium_paired_allocation_inventory(
                    injected, injected[0], injected[-1],
                )

            # The real producer positive above covers source->reader->parser. Add
            # one bounded duplicate marker from an actual emitted endpoint solely
            # to exercise the consumer's repeated-stop rule; do not synthesize a
            # second source callback/re-entry in the C++ fixture.
            repeated_rows = [dict(row) for row in actual_rows]
            repeated_header = repeated_rows[0]
            original = next(row for row in repeated_rows if row.get("record") == "enter")
            repeated_footer = repeated_rows[-1]
            repeated_footer["repeated_stops"] = 1
            repeated_row = {
                "record": "repeated_stop",
                "sequence": repeated_footer["sequence"],
                "original_sequence": original["sequence"],
                "function": original["function"],
                "phase": original["record"],
                "machine": original["machine"],
                "observed": original["observed"],
            }
            repeated_rows.insert(-1, repeated_row)
            repeated_footer["sequence"] += 1
            repeated_decoded, _, _, repeated_selected = validate_original_rng_scene_boundaries_stream(
                synthetic_profile(), repeated_rows)
            self.assertEqual(repeated_decoded["sample_count"], 8)
            self.assertEqual(len(repeated_selected), 8)
            repeated_inventory = validate_stadium_paired_allocation_inventory(
                repeated_rows, repeated_header, repeated_footer)
            self.assertEqual(repeated_inventory["repeated_stop_rows"], 1)
            bad_repeat = [dict(row) for row in repeated_rows]
            repeat_row = next(row for row in bad_repeat if row.get("record") == "repeated_stop")
            repeat_row["phase"] = "return"
            with self.assertRaisesRegex(ValueError, "repeated-stop row differs"):
                validate_stadium_paired_allocation_inventory(
                    bad_repeat, bad_repeat[0], bad_repeat[-1],
                )
            incomplete = run_mode("vs-interrupted", **paired_env)
            self.assertEqual(incomplete.returncode, 0,
                             f"paired incomplete-terminal refusal: {incomplete.stderr}")
            incomplete_rows = [json.loads(line) for line in
                               (temp / "vs-interrupted.jsonl").read_text().splitlines()]
            incomplete_end = incomplete_rows[-1]
            self.assertEqual(incomplete_end["scope"], "scene-rng-endpoints-only")
            self.assertEqual(incomplete_end["status"], "incomplete")
            self.assertIs(incomplete_end["target_complete"], False)
            self.assertEqual((incomplete_end["vs_entries"], incomplete_end["vs_exits"]), (1, 1))
            self.assertIs(incomplete_end["active_owner"], False)
            refused = run_mode("paired-refuse",
                               MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE="paired_idle_scene_endpoints_v1",
                               MWRC_ALLOCATION_VS_TARGET="3", MWRC_ALLOCATION_STOP_AT="exit")
            self.assertEqual(refused.returncode, 0, f"paired target refusal: {refused.stderr}")
            self.assertFalse((temp / "paired-refuse.jsonl").exists())
            refused = run_mode("paired-refuse",
                               MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE="paired_idle_scene_endpoints_v1",
                               MWRC_ALLOCATION_VS_TARGET="2", MWRC_ALLOCATION_STOP_AT="entry")
            self.assertEqual(refused.returncode, 0, f"paired stop refusal: {refused.stderr}")
            self.assertFalse((temp / "paired-refuse.jsonl").exists())
            refused = run_mode("paired-cap-refuse",
                               MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE="paired_idle_scene_endpoints_v1",
                               MWRC_ALLOCATION_VS_TARGET="2", MWRC_ALLOCATION_STOP_AT="exit",
                               MWRC_ALLOCATION_MAX_EVENTS="65")
            self.assertEqual(refused.returncode, 0, f"paired event-cap refusal: {refused.stderr}")
            self.assertFalse((temp / "paired-cap-refuse.jsonl").exists())
            refused = run_mode("paired-unknown",
                               MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE="paired_idle_scene_endpoints_v2",
                               MWRC_ALLOCATION_VS_TARGET="2", MWRC_ALLOCATION_STOP_AT="exit")
            self.assertEqual(refused.returncode, 0, f"paired unknown-profile refusal: {refused.stderr}")
            self.assertFalse((temp / "paired-unknown.jsonl").exists())
            refused = run_mode("paired-duplicate",
                               MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE="paired_idle_scene_endpoints_v1",
                               MWRC_ALLOCATION_VS_TARGET="2", MWRC_ALLOCATION_STOP_AT="exit")
            self.assertEqual(refused.returncode, 0, f"paired duplicate-identity refusal: {refused.stderr}")
            self.assertFalse((temp / "paired-duplicate.jsonl").exists())

            checked = run_mode("vs-three-exits",output_name="three-route-target-two.jsonl",
                               MWRC_ALLOCATION_VS_TARGET="2",MWRC_ALLOCATION_STOP_AT="exit")
            self.assertEqual(checked.returncode,0,checked.stderr)
            retained=[json.loads(line) for line in (temp/"three-route-target-two.jsonl").read_text().splitlines()]
            self.assertEqual(retained[-1]['status'],'captured')
            self.assertEqual((retained[-1]['vs_entries'],retained[-1]['vs_exits']),(2,2))
            from tools.allocation_lifetime_replay import validate_original_rng_scene_boundaries
            self.assertEqual(validate_original_rng_scene_boundaries(synthetic_profile(),retained[0],retained[-1],retained)['sample_count'],8)

            checked = run_mode("vs-two-then-third",MWRC_ALLOCATION_VS_TARGET="2",
                               MWRC_ALLOCATION_STOP_AT="exit")
            self.assertEqual(checked.returncode,0,checked.stderr)

            invalid = run_mode("invalid-config", MWRC_ALLOCATION_VS_TARGET="17")
            self.assertEqual(invalid.returncode, 0, invalid.stderr)
            invalid = run_mode("invalid-config", MWRC_ALLOCATION_STOP_AT="middle")
            self.assertEqual(invalid.returncode, 0, invalid.stderr)
            invalid = run_mode("invalid-config", MWRC_ALLOCATION_MAX_EVENTS="0")
            self.assertEqual(invalid.returncode, 0, invalid.stderr)
            invalid = run_mode("invalid-config", MWRC_ALLOCATION_MAX_EVENTS="100000001")
            self.assertEqual(invalid.returncode, 0, invalid.stderr)
            invalid = run_mode("invalid-config", MWRC_ALLOCATION_MAX_EVENTS="not-a-count")
            self.assertEqual(invalid.returncode, 0, invalid.stderr)


if __name__ == "__main__":
    unittest.main()
