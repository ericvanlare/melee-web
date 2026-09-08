#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace melee_web {

class DatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct DatPublicSymbol {
    std::string name;
    std::uint32_t data_offset;
};

// An immutable, owning view of an HSD DAT archive. All exposed offsets are
// relative to the data section, not the file. No native pointers are written
// into the original big-endian bytes. Typed object/array validation belongs to
// the caller; for example, a valid relocation can point to unaligned byte data.
class DatArchive {
public:
    static constexpr std::size_t max_archive_bytes = 64U * 1024U * 1024U;
    static constexpr std::size_t max_table_entries = 1024U * 1024U;
    static constexpr std::size_t max_symbol_name_bytes = 4096U;

    // Limits also bound total copied symbol-name bytes to max_archive_bytes.
    // External links require another archive and are explicitly unsupported.
    explicit DatArchive(std::span<const std::uint8_t> input);

    [[nodiscard]] std::span<const std::uint8_t> data() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t>
    range(std::uint32_t offset, std::size_t length) const;
    [[nodiscard]] std::uint16_t be16(std::uint32_t offset) const;
    [[nodiscard]] std::uint32_t be32(std::uint32_t offset) const;
    [[nodiscard]] float f32(std::uint32_t offset) const;
    [[nodiscard]] const std::vector<DatPublicSymbol>& public_symbols() const noexcept;

    // First referenced target strictly after offset, or the data-section end.
    // offset must name a byte inside data. This is a conservative region bound
    // from validated relocations/public symbols, not proof of allocation size:
    // aliases or references into an array can divide a legitimate allocation.
    [[nodiscard]] std::uint32_t next_target_offset(std::uint32_t offset) const;

    // Query relocation metadata without interpreting a scalar word as a
    // pointer. The slot must be aligned and contain four bytes inside data.
    [[nodiscard]] bool has_relocation(std::uint32_t slot) const;

    // A zero word is null only if the slot is absent from the relocation table.
    // A relocated zero instead names data offset zero. Nonzero unrelocated
    // words are not accepted as pointers. Slots must be four-byte aligned;
    // targets must contain minbytes but need not themselves be aligned.
    [[nodiscard]] std::optional<std::uint32_t>
    pointer(std::uint32_t slot, std::size_t minbytes = 1) const;

private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t data_size_ = 0;
    std::vector<std::uint32_t> relocation_slots_;
    std::vector<std::uint32_t> referenced_targets_;
    std::vector<DatPublicSymbol> public_symbols_;
};

} // namespace melee_web
