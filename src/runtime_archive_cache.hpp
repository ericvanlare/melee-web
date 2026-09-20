#pragma once

#include "dat_archive.hpp"
#include "dat_audio.hpp"
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
    [[nodiscard]] std::shared_ptr<const DatAudioBank>
    audio_bank(std::string_view name);
    void verify(const std::shared_ptr<const DatArchive>& archive) const;
    [[nodiscard]] std::size_t archive_count() const { return entries_.size(); }
    [[nodiscard]] std::size_t audio_bank_count() const { return audio_entries_.size(); }

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
    struct AudioEntry {
        std::shared_ptr<const DatAudioBank> bank;
        const std::uint8_t* source_data = nullptr;
        std::size_t source_size = 0;
    };

    const RuntimeFiles& files_;
    std::map<Key, Entry> entries_;
    std::map<std::string, AudioEntry, std::less<>> audio_entries_;
};

} // namespace melee_web
