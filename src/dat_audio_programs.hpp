#pragma once
#include "dat_archive.hpp"
#include <array>
namespace melee_web {
// SEM tables and instruction words stay source values; pointer tables retain
// validated file offsets until the Wasm source bridge publishes native pointers.
class DatAudioPrograms {
public:
 explicit DatAudioPrograms(std::span<const uint8_t>);
 std::vector<uint32_t> words;
 std::array<std::vector<uint32_t>,5> tables;
 uint32_t program_index(uint32_t sound_id)const;
};
}
