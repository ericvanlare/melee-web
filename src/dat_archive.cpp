#include "dat_archive.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace melee_web {
namespace {

constexpr std::size_t header_size = 0x20;

void require_range(std::size_t size, std::size_t offset, std::size_t length)
{
    // Subtraction after the offset check handles even SIZE_MAX lengths safely.
    if (offset > size || length > size - offset) {
        throw DatError("DAT range is outside its section");
    }
}

std::uint32_t read_be32(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    require_range(bytes.size(), offset, 4);
    return (std::uint32_t{bytes[offset]} << 24U) |
           (std::uint32_t{bytes[offset + 1]} << 16U) |
           (std::uint32_t{bytes[offset + 2]} << 8U) |
           std::uint32_t{bytes[offset + 3]};
}

std::size_t table_end(std::size_t file_size, std::size_t offset,
                      std::uint32_t count, std::size_t entry_size)
{
    if (count > DatArchive::max_table_entries) {
        throw DatError("DAT table entry limit exceeded");
    }
    // Check by division before multiplication, including on a 32-bit host.
    if (offset > file_size || count > (file_size - offset) / entry_size) {
        throw DatError("DAT table is truncated");
    }
    return offset + std::size_t{count} * entry_size;
}

} // namespace

DatArchive::DatArchive(std::span<const std::uint8_t> input, DatExternalPolicy external_policy)
{
    if (input.size() > max_archive_bytes) {
        throw DatError("DAT archive exceeds the 64 MiB limit");
    }
    if (input.size() < header_size) {
        throw DatError("DAT header is truncated");
    }
    if (read_be32(input, 0) != input.size()) {
        throw DatError("DAT header file size differs from the input size");
    }
    data_size_ = read_be32(input, 4);
    const auto relocation_count = read_be32(input, 8);
    const auto public_count = read_be32(input, 12);
    const auto external_count = read_be32(input, 16);
    require_range(input.size(), header_size, data_size_);
    const auto relocation_start = header_size + std::size_t{data_size_};
    const auto public_start = table_end(input.size(), relocation_start,
                                        relocation_count, 4);
    const auto external_start = table_end(input.size(), public_start,
                                          public_count, 8);
    const auto names_start = table_end(input.size(), external_start,
                                       external_count, 8);
    if (external_count != 0 && external_policy == DatExternalPolicy::Reject) {
        throw DatError("DAT external links are unsupported; resolve them explicitly");
    }

    bytes_.assign(input.begin(), input.end());
    relocation_slots_.reserve(relocation_count);
    referenced_targets_.reserve(std::size_t{relocation_count} + public_count + 1);
    for (std::uint32_t i = 0; i < relocation_count; ++i) {
        const auto slot = read_be32(bytes_, relocation_start + std::size_t{i} * 4);
        if (slot % 4 != 0) {
            throw DatError("DAT relocation slot is not four-byte aligned");
        }
        const auto target = be32(slot);
        // Relocation may name an empty region at the end of the data section.
        // Typed pointer reads still require their requested number of bytes.
        (void) range(target, 0);
        referenced_targets_.push_back(target);
        relocation_slots_.push_back(slot);
    }
    std::sort(relocation_slots_.begin(), relocation_slots_.end());
    if (std::adjacent_find(relocation_slots_.begin(), relocation_slots_.end()) !=
        relocation_slots_.end()) {
        throw DatError("DAT relocation slot is duplicated");
    }

    public_symbols_.reserve(public_count);
    // Views reference immutable bytes_, so hashing does not require a second
    // copy of every symbol name and is independent of the symbol vector growth.
    std::unordered_set<std::string_view> names;
    names.reserve(public_count);
    std::size_t copied_name_bytes = 0;
    for (std::uint32_t i = 0; i < public_count; ++i) {
        const auto entry = public_start + std::size_t{i} * 8;
        const auto target = read_be32(bytes_, entry);
        (void) range(target, 1);
        const auto name_offset = read_be32(bytes_, entry + 4);
        require_range(bytes_.size() - names_start, name_offset, 1);
        const auto name_start = names_start + name_offset;
        const auto available = bytes_.size() - name_start;
        const auto search_size = std::min(available, max_symbol_name_bytes + 1);
        const auto* begin = bytes_.data() + name_start;
        const auto* end = std::find(begin, begin + search_size, std::uint8_t{0});
        if (end == begin + search_size) {
            throw DatError("DAT public name is unterminated or exceeds the name limit");
        }
        const auto length = static_cast<std::size_t>(end - begin);
        if (length == 0) {
            throw DatError("DAT public name is empty");
        }
        if (length > max_archive_bytes - copied_name_bytes) {
            throw DatError("DAT total symbol-name byte limit exceeded");
        }
        copied_name_bytes += length;
        const std::string_view name(reinterpret_cast<const char*>(begin), length);
        if (!names.insert(name).second) {
            throw DatError("DAT public name is duplicated");
        }
        public_symbols_.push_back({std::string{name}, target});
        referenced_targets_.push_back(target);
    }
    // Original HSD_ArchiveLocateExtern follows a linked list of data-section
    // pointer slots. Their raw words are next-slot offsets, not usable pointers.
    // Validate all chains before exposing any unresolved archive to a decoder.
    external_symbols_.reserve(external_count);
    std::unordered_set<std::string_view> external_names;
    std::unordered_set<std::uint32_t> used_external_slots;
    for (std::uint32_t i = 0; i < external_count; ++i) {
        const auto entry = external_start + std::size_t{i} * 8;
        auto slot = read_be32(bytes_, entry);
        const auto name_offset = read_be32(bytes_, entry + 4);
        require_range(bytes_.size() - names_start, name_offset, 1);
        const auto name_start = names_start + name_offset;
        const auto available = bytes_.size() - name_start;
        const auto search_size = std::min(available, max_symbol_name_bytes + 1);
        const auto* begin = bytes_.data() + name_start;
        const auto* end = std::find(begin, begin + search_size, std::uint8_t{0});
        if (end == begin + search_size)
            throw DatError("DAT external name is unterminated or exceeds the name limit");
        const auto length = static_cast<std::size_t>(end - begin);
        if (!length) throw DatError("DAT external name is empty");
        if (length > max_archive_bytes - copied_name_bytes)
            throw DatError("DAT total symbol-name byte limit exceeded");
        copied_name_bytes += length;
        const std::string_view name(reinterpret_cast<const char*>(begin), length);
        if (!external_names.insert(name).second) throw DatError("DAT external name is duplicated");
        DatExternalSymbol symbol{std::string{name}, {}};
        while (slot != UINT32_MAX) {
            if (slot % 4) throw DatError("DAT external slot is not four-byte aligned");
            require_range(data_size_, slot, 4);
            if (used_external_slots.size() >= max_table_entries)
                throw DatError("DAT external linked-slot limit exceeded");
            if (std::binary_search(relocation_slots_.begin(), relocation_slots_.end(), slot))
                throw DatError("DAT external slot overlaps an internal relocation");
            if (!used_external_slots.insert(slot).second)
                throw DatError("DAT external linked slots contain a cycle or overlapping chains");
            symbol.slots.push_back(slot);
            external_slots_.emplace_back(slot, i);
            // Deliberately bypass typed be32: this reads the archive's linked
            // list encoding, not an unresolved pointer or scalar field.
            slot = read_be32(data(), slot);
        }
        external_symbols_.push_back(std::move(symbol));
    }
    std::sort(external_slots_.begin(), external_slots_.end());
    referenced_targets_.push_back(data_size_);
    std::sort(referenced_targets_.begin(), referenced_targets_.end());
    referenced_targets_.erase(std::unique(referenced_targets_.begin(), referenced_targets_.end()),
                              referenced_targets_.end());
}

std::span<const std::uint8_t> DatArchive::data() const noexcept
{
    return std::span<const std::uint8_t>{bytes_}.subspan(header_size, data_size_);
}

std::span<const std::uint8_t> DatArchive::range(std::uint32_t offset,
                                             std::size_t length) const
{
    require_range(data_size_, offset, length);
    return data().subspan(offset, length);
}

std::uint16_t DatArchive::be16(std::uint32_t offset) const
{
    require_resolved(offset, 2);
    const auto bytes = range(offset, 2);
    return static_cast<std::uint16_t>((std::uint16_t{bytes[0]} << 8U) |
                                      std::uint16_t{bytes[1]});
}

std::uint32_t DatArchive::be32(std::uint32_t offset) const
{
    require_resolved(offset, 4);
    return read_be32(data(), offset);
}

float DatArchive::f32(std::uint32_t offset) const
{
    static_assert(sizeof(float) == sizeof(std::uint32_t) &&
                  std::numeric_limits<float>::is_iec559,
                  "DAT float reads require IEEE 754 binary32");
    return std::bit_cast<float>(be32(offset));
}

const std::vector<DatPublicSymbol>& DatArchive::public_symbols() const noexcept
{
    return public_symbols_;
}

const std::vector<DatExternalSymbol>& DatArchive::external_symbols() const noexcept
{
    return external_symbols_;
}

void DatArchive::require_resolved(std::uint32_t offset, std::size_t length) const
{
    require_range(data_size_, offset, length);
    if (!length || external_slots_.empty()) return;
    const auto first = offset > 3 ? offset - 3 : 0;
    const auto it = std::lower_bound(external_slots_.begin(), external_slots_.end(),
        std::pair<std::uint32_t, std::uint32_t>{first, 0});
    if (it != external_slots_.end() && it->first < std::size_t{offset} + length)
        throw DatError("DAT unresolved external symbol: " + external_symbols_.at(it->second).name);
}

std::uint32_t DatArchive::next_target_offset(std::uint32_t offset) const
{
    (void) range(offset, 1);
    return *std::upper_bound(referenced_targets_.begin(), referenced_targets_.end(), offset);
}

bool DatArchive::has_relocation(std::uint32_t slot) const
{
    if (slot % 4 != 0) {
        throw DatError("DAT pointer slot is not four-byte aligned");
    }
    (void) range(slot, 4);
    const auto external = std::lower_bound(external_slots_.begin(), external_slots_.end(),
        std::pair<std::uint32_t, std::uint32_t>{slot, 0});
    return (external != external_slots_.end() && external->first == slot) ||
        std::binary_search(relocation_slots_.begin(), relocation_slots_.end(), slot);
}

std::optional<std::uint32_t> DatArchive::pointer(std::uint32_t slot,
                                               std::size_t minbytes) const
{
    const bool relocated = has_relocation(slot);
    const auto target = be32(slot);
    if (!relocated) {
        if (target == 0) {
            return std::nullopt;
        }
        throw DatError("DAT nonzero pointer is missing a relocation entry");
    }
    (void) range(target, minbytes);
    return target;
}

} // namespace melee_web
