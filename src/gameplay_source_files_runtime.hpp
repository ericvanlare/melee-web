#pragma once

#include "gameplay_source_files.h"

#include <cstddef>
#include <string>
#include <vector>

namespace melee_web {

// RuntimeFiles owns byte vectors. The C service copies names and borrows those
// vectors until its scope closes, so source lbFile copies bytes into its own
// original lbHeap allocation before HSD_Archive relocation can mutate them.
template <class RuntimeFiles>
MeleeWebSourceFileScope* begin_source_files(const RuntimeFiles& files,
                                           char* error,
                                           std::size_t error_size)
{
    std::vector<MeleeWebSourceFileInput> input;
    input.reserve(files.size());
    for (const auto& [name, bytes] : files) {
        input.push_back({name.c_str(), bytes.data(), bytes.size()});
    }
    return melee_web_source_files_begin(input.data(), input.size(), error,
                                        error_size);
}

} // namespace melee_web
