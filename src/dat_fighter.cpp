#include "dat_fighter.hpp"
#include <algorithm>

namespace melee_web {
namespace {
void region(const DatArchive& a, uint32_t offset, size_t bytes, bool aligned = true)
{
    if (aligned && offset % 4) throw DatError("Fighter visibility descriptor is unaligned");
    (void) a.range(offset, bytes);
    if (bytes > a.next_target_offset(offset) - offset)
        throw DatError("Fighter visibility data crosses a referenced region");
}
uint32_t required(const DatArchive& a, uint32_t slot, size_t bytes)
{
    const auto target = a.pointer(slot, bytes);
    if (!target) throw DatError("Required fighter visibility pointer is null");
    return *target;
}
}

DatFighterParts::DatFighterParts(const DatArchive& a, const std::string& symbol,
                                uint32_t costume)
    : costume_index(costume)
{
    const auto& roots = a.public_symbols();
    const auto root = std::find_if(roots.begin(), roots.end(),
        [&](const auto& entry) { return entry.name == symbol; });
    if (root == roots.end()) throw DatError("Fighter metadata public symbol is missing");
    root_offset = root->data_offset;
    region(a, root_offset, 12);
    descriptor_offset = required(a, root_offset + 8, 8);
    region(a, descriptor_offset, 8);
    model_count = a.be32(descriptor_offset);
    if (!model_count || model_count > 11)
        throw DatError("Fighter visibility group count must be between one and eleven");
    if (costume > 31) throw DatError("Fighter costume index exceeds the supported table budget");
    const auto table = required(a, descriptor_offset + 4, 16);
    region(a, table, (size_t(costume) + 1) * 16);
    for (uint32_t category = 0; category < 4; ++category) {
        auto lookup = a.pointer(table + costume * 16 + category * 4, model_count * 8);
        if (!lookup) lookup = a.pointer(table + category * 4, model_count * 8);
        if (!lookup) continue;
        region(a, *lookup, model_count * 8);
        DatFighterRepresentation representation;
        representation.descriptor_offset = *lookup;
        for (uint32_t group = 0; group < model_count; ++group) {
            const auto entry = *lookup + group * 8;
            const auto count = a.be32(entry);
            if (count > 128) throw DatError("Fighter visibility variant count exceeds signed selector range");
            DatFighterGroup decoded;
            const auto variants = a.pointer(entry + 4, count ? count * 8 : 1);
            if (count && !variants) throw DatError("Fighter visibility variants are missing");
            if (count) region(a, *variants, count * 8);
            for (uint32_t variant = 0; variant < count; ++variant) {
                DatFighterVariant item;
                item.descriptor_offset = *variants + variant * 8;
                const auto indices_count = a.be32(item.descriptor_offset);
                const uint32_t limit = category == 2 ? 32 : 124;
                if (indices_count > limit) throw DatError("Fighter visibility DObj list exceeds source capacity");
                const auto indices = a.pointer(item.descriptor_offset + 4, indices_count ? indices_count : 1);
                if (indices_count && !indices) throw DatError("Fighter visibility DObj indices are missing");
                if (indices_count) {
                    region(a, *indices, indices_count, false);
                    const auto bytes = a.range(*indices, indices_count);
                    for (const auto index : bytes) {
                        if (index >= limit) throw DatError("Fighter visibility DObj index exceeds source capacity");
                        item.dobj_indices.push_back(index);
                    }
                }
                decoded.variants.push_back(std::move(item));
            }
            representation.groups.push_back(std::move(decoded));
        }
        representations[category] = std::move(representation);
    }
}

std::vector<uint32_t> DatFighterParts::normal_dobj_indices(uint32_t count) const
{
    if (!count || count > 124) throw DatError("Fighter model DObj count exceeds source capacity");
    if (!representations[0]) throw DatError("Fighter normal representation is missing");
    std::vector<bool> visible(count, true);
    // ftParts_8007487C hides all listed categories. Main rendering then enables
    // category0; category2 belongs to the separate metal model and cannot index
    // this costume's DObj array.
    for (const auto category : {0U, 1U, 3U}) {
        if (!representations[category]) continue;
        for (const auto& group : representations[category]->groups) {
            for (const auto& variant : group.variants) {
                for (const auto index : variant.dobj_indices) {
                    if (index >= count) throw DatError("Fighter visibility index is outside the loaded model");
                    visible[index] = false;
                }
            }
        }
    }
    for (const auto& group : representations[0]->groups) {
        if (group.variants.size() != 1)
            throw DatError("Fighter normal visibility requires an explicit variant selector");
        for (const auto index : group.variants[0].dobj_indices) visible[index] = true;
    }
    std::vector<uint32_t> result;
    for (uint32_t index = 0; index < count; ++index) if (visible[index]) result.push_back(index);
    return result;
}

} // namespace melee_web
