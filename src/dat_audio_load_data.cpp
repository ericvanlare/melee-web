#include "dat_audio_load_data.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace melee_web {
namespace {

constexpr std::string_view kRootName = "lbAudioLoadData";
constexpr std::size_t kGroupCount = 30;
constexpr std::uint32_t kTerminator = 0x00083D60;

void require(bool value, const char* message)
{
    if (!value) throw DatError(message);
}

struct AudioLoadDataRoot {
    std::int32_t** x0;
    std::int32_t** x4;
    std::int32_t** x8;
    std::int32_t** xC;
};

#ifdef __wasm__
static_assert(sizeof(AudioLoadDataRoot) == 16);
#endif

} // namespace

struct DatAudioLoadData::Storage {
    std::shared_ptr<const DatArchive> archive;
    std::array<std::array<std::int32_t*, kGroupCount>, 4> groups{};
    AudioLoadDataRoot root{};
    std::map<std::uint32_t, std::unique_ptr<std::vector<std::int32_t>>> lists;

    explicit Storage(std::shared_ptr<const DatArchive> input)
        : archive(std::move(input))
    {
        require(archive != nullptr, "LbAd audio table archive is null");
        const auto symbol = std::find_if(
            archive->public_symbols().begin(), archive->public_symbols().end(),
            [](const DatPublicSymbol& value) { return value.name == kRootName; });
        require(symbol != archive->public_symbols().end(),
                "LbAd.dat is missing lbAudioLoadData");
        (void) archive->range(symbol->data_offset, 16);

        for (std::size_t language = 0; language < groups.size(); ++language) {
            const std::uint32_t slot = symbol->data_offset +
                                       static_cast<std::uint32_t>(language * 4);
            const auto table = archive->pointer(slot, kGroupCount * 4);
            require(table.has_value(), "LbAd language lookup table is missing");
            require(archive->next_target_offset(*table) - *table == kGroupCount * 4,
                    "LbAd language lookup table differs from the source 30-group bound");
            for (std::size_t group = 0; group < kGroupCount; ++group) {
                const std::uint32_t group_slot = *table +
                    static_cast<std::uint32_t>(group * 4);
                const auto row = archive->pointer(group_slot, 4);
                require(row.has_value(), "LbAd sound group row is missing");
                groups[language][group] = list(*row);
            }
        }
        root = {groups[0].data(), groups[1].data(),
                groups[2].data(), groups[3].data()};
    }

    std::int32_t* list(std::uint32_t offset)
    {
        if (auto found = lists.find(offset); found != lists.end())
            return found->second->data();
        require((offset & 3U) == 0, "LbAd sound group row is unaligned");
        auto values = std::make_unique<std::vector<std::int32_t>>();
        const auto bytes = archive->data();
        require(offset < bytes.size(), "LbAd sound group row is outside the data section");
        for (std::size_t slot = offset; slot + 4 <= bytes.size(); slot += 4) {
            require(!archive->has_relocation(static_cast<std::uint32_t>(slot)),
                    "LbAd sound candidate row contains a pointer relocation");
            const std::uint32_t value = archive->be32(static_cast<std::uint32_t>(slot));
            values->push_back(static_cast<std::int32_t>(value));
            if (value == kTerminator) {
                auto* result = values->data();
                lists.emplace(offset, std::move(values));
                return result;
            }
        }
        throw DatError("LbAd sound candidate row has no source terminator");
    }
};

DatAudioLoadData::DatAudioLoadData(std::shared_ptr<const DatArchive> archive)
    : storage_(std::make_unique<Storage>(std::move(archive)))
{}

DatAudioLoadData::~DatAudioLoadData() = default;

void* DatAudioLoadData::descriptor() noexcept
{
    return &storage_->root;
}

std::size_t DatAudioLoadData::group_count() const noexcept
{
    return kGroupCount;
}

} // namespace melee_web
