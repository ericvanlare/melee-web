#pragma once

#include "dat_archive.hpp"
#include "gameplay_world.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace melee_web {

// Disc bytes are immutable for one browser import. Decode and validate each
// archive/policy pair once, then share only its const representation. Source
// objects hydrated from it remain fresh owners for every scene entry.
class RuntimeArchiveCache {
public:
    explicit RuntimeArchiveCache(const RuntimeFiles& files) : files_(files) {}

    [[nodiscard]] std::shared_ptr<const DatArchive>
    archive(std::string_view name,
            DatExternalPolicy policy = DatExternalPolicy::Reject);
    void verify(const std::shared_ptr<const DatArchive>& archive) const;

private:
    struct Key {
        std::string name;
        DatExternalPolicy policy;
        bool operator<(const Key& other) const noexcept
        {
            if (name != other.name) return name < other.name;
            return policy < other.policy;
        }
    };
    struct Entry {
        std::shared_ptr<const DatArchive> archive;
        std::vector<std::uint8_t> baseline;
        const std::uint8_t* source_data = nullptr;
        std::size_t source_size = 0;
    };

    const RuntimeFiles& files_;
    std::map<Key, Entry> entries_;
};

} // namespace melee_web
