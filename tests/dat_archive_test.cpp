#include "dat_archive.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using melee_web::DatArchive;
using melee_web::DatError;
using Bytes = std::vector<std::uint8_t>;

namespace {

void check(bool condition, const std::string& description)
{
    if (!condition) {
        throw std::runtime_error(description);
    }
}

template <typename F> void rejects(F operation)
{
    try {
        operation();
    } catch (const DatError&) {
        return;
    }
    throw std::runtime_error("expected DatError");
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    bytes.at(offset) = static_cast<std::uint8_t>(value >> 24U);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 16U);
    bytes.at(offset + 2) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(offset + 3) = static_cast<std::uint8_t>(value);
}

// Byte layout from HSD_ArchiveHeader/RelocationInfo/PublicInfo, independent of
// the reader. Synthetic data is deliberately a mix of pointers, scalars and
// float bit patterns, not a fabricated host-endian native structure.
Bytes archive(const Bytes& data, const std::vector<std::uint32_t>& relocations,
              const std::vector<std::pair<std::uint32_t, std::uint32_t>>& publics,
              const Bytes& names, std::uint32_t external_count = 0)
{
    const auto size = 0x20 + data.size() + relocations.size() * 4 +
                      publics.size() * 8 + std::size_t{external_count} * 8 + names.size();
    Bytes bytes(size, 0);
    put32(bytes, 0, static_cast<std::uint32_t>(size));
    put32(bytes, 4, static_cast<std::uint32_t>(data.size()));
    put32(bytes, 8, static_cast<std::uint32_t>(relocations.size()));
    put32(bytes, 12, static_cast<std::uint32_t>(publics.size()));
    put32(bytes, 16, external_count);
    std::copy(data.begin(), data.end(), bytes.begin() + 0x20);
    auto offset = 0x20 + data.size();
    for (const auto relocation : relocations) {
        put32(bytes, offset, relocation);
        offset += 4;
    }
    for (const auto& [target, name] : publics) {
        put32(bytes, offset, target);
        put32(bytes, offset + 4, name);
        offset += 8;
    }
    offset += std::size_t{external_count} * 8;
    std::copy(names.begin(), names.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return bytes;
}

Bytes specimen()
{
    Bytes data(32, 0);
    put32(data, 0, 16);            // Relocated pointer to the float at data+16.
    put32(data, 4, 0);             // Relocated pointer to the data-section root.
    put32(data, 8, 0);             // Null pointer, absent from the relocation table.
    put32(data, 12, 0x12345678);    // Non-pointer integer.
    put32(data, 16, 0x3fc00000);    // 1.5f, stored in GameCube byte order.
    put32(data, 20, 0x80000000);    // Negative zero.
    put32(data, 24, 0x7f800000);    // Infinity; typed consumers must check finiteness.
    put32(data, 28, 0xabcd1234);
    return archive(data, {0, 4}, {{0, 0}}, {'r', 'o', 'o', 't', 0});
}

void endian_reads()
{
    const DatArchive dat(specimen());
    check(dat.data().size() == 32, "data view must exclude header and tables");
    check(dat.be32(12) == 0x12345678, "32-bit big-endian read");
    check(dat.be16(28) == 0xabcd && dat.be16(30) == 0x1234, "16-bit big-endian reads");
    check(dat.be16(13) == 0x3456, "unaligned scalar reads are byte-based");
    check(dat.f32(16) == 1.5F, "IEEE binary32 read");
    check(dat.f32(20) == 0 && std::signbit(dat.f32(20)), "negative-zero bits preserved");
    check(std::isinf(dat.f32(24)), "parser must not silently sanitize floats");
    check(dat.public_symbols().size() == 1, "one public symbol");
    check(dat.public_symbols()[0].name == "root" &&
          dat.public_symbols()[0].data_offset == 0, "symbol resolves to data root");
}

void pointer_semantics()
{
    const DatArchive dat(specimen());
    check(dat.pointer(0, 4) == 16, "ordinary relocation");
    check(dat.pointer(4).has_value() && *dat.pointer(4) == 0,
          "relocated zero names data offset zero, not null");
    check(!dat.pointer(8), "unrelocated zero is null");
    rejects([&] { (void) dat.pointer(12); });
    rejects([&] { (void) dat.pointer(1); });
    rejects([&] { (void) dat.pointer(32); });
    rejects([&] { (void) dat.pointer(0, 17); });
    rejects([&] { (void) dat.pointer(0, std::numeric_limits<std::size_t>::max()); });
    check(dat.pointer(0, 16) == 16, "target range can end at the data boundary");
    // Generic archive pointers can address byte strings, unlike typed joint
    // pointers. Alignment requirements for targets belong to the typed reader.
    auto bytes = specimen();
    put32(bytes, 0x20, 17);
    check(DatArchive(bytes).pointer(0) == 17, "byte-array targets may be unaligned");
}

void ownership()
{
    auto bytes = specimen();
    DatArchive dat(bytes);
    std::fill(bytes.begin(), bytes.end(), 0);
    bytes.clear();
    bytes.shrink_to_fit();
    check(dat.f32(16) == 1.5F, "input storage must be owned");
    check(dat.public_symbols()[0].name == "root", "symbol names remain owned");
    auto copy = dat;
    dat = DatArchive(archive({}, {}, {}, {}));
    check(copy.pointer(0) == 16 && copy.public_symbols()[0].name == "root",
          "copied archive must not retain views into the original object");
}

void bounds()
{
    const DatArchive dat(specimen());
    check(dat.range(31, 1)[0] == 0x34, "last data byte");
    check(dat.range(32, 0).empty(), "empty range at the boundary");
    rejects([&] { (void) dat.range(32, 1); });
    rejects([&] { (void) dat.range(33, 0); });
    rejects([&] { (void) dat.range(1, std::numeric_limits<std::size_t>::max()); });
    rejects([&] { (void) dat.range(0xffffffff, 4); });
    rejects([&] { (void) dat.be16(31); });
    rejects([&] { (void) dat.be32(29); });
    rejects([&] { (void) dat.f32(30); });
    const DatArchive empty(archive({}, {}, {}, {}));
    check(empty.data().empty() && empty.public_symbols().empty(), "empty archive");
    check(empty.range(0, 0).empty(), "empty data has an empty range");
    rejects([&] { (void) empty.pointer(0); });
}

void file_and_header_sizes()
{
    const auto original = specimen();
    for (std::size_t size = 0; size < original.size(); ++size) {
        auto bytes = Bytes(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(size));
        rejects([&] { (void) DatArchive(bytes); });
        if (size >= 0x20) {
            put32(bytes, 0, static_cast<std::uint32_t>(size));
            rejects([&] { (void) DatArchive(bytes); });
        }
    }
    auto bytes = original;
    bytes.push_back(0);
    rejects([&] { (void) DatArchive(bytes); });
    bytes = original;
    put32(bytes, 4, 0xffffffff);
    rejects([&] { (void) DatArchive(bytes); });
    bytes = original;
    std::reverse(bytes.begin(), bytes.begin() + 4);
    rejects([&] { (void) DatArchive(bytes); });
}

void table_counts()
{
    for (const auto header_field : {8, 12, 16}) {
        for (const auto count : {100U, 0x40000000U, 0xffffffffU}) {
            auto bytes = specimen();
            put32(bytes, header_field, count);
            rejects([&] { (void) DatArchive(bytes); });
        }
    }
    auto bytes = specimen();
    put32(bytes, 4, 50); // Data fits, but the following tables no longer do.
    rejects([&] { (void) DatArchive(bytes); });
}

void relocation_slots()
{
    auto bytes = specimen();
    put32(bytes, 0x20 + 32 + 4, 0);
    rejects([&] { (void) DatArchive(bytes); });
    for (const auto slot : {1U, 30U, 32U, 0xfffffffcU}) {
        bytes = specimen();
        put32(bytes, 0x20 + 32, slot);
        rejects([&] { (void) DatArchive(bytes); });
    }
    bytes = specimen();
    put32(bytes, 0x20 + 32, 4);
    put32(bytes, 0x20 + 32 + 4, 0);
    const DatArchive unsorted(bytes);
    check(unsorted.pointer(0) == 16 && unsorted.pointer(4) == 0,
          "valid relocation tables need not be sorted");
}

void relocation_targets()
{
    for (const auto target : {32U, 33U, 0xffffffffU}) {
        auto bytes = specimen();
        put32(bytes, 0x20, target);
        rejects([&] { (void) DatArchive(bytes); });
    }
}

void public_targets()
{
    for (const auto target : {32U, 0xffffffffU}) {
        auto bytes = specimen();
        put32(bytes, 0x20 + 32 + 8, target);
        rejects([&] { (void) DatArchive(bytes); });
    }
    rejects([] { (void) DatArchive(archive({}, {}, {{0, 0}}, {'x', 0})); });
}

void public_names()
{
    for (const auto offset : {4U, 5U, 0xffffffffU}) {
        auto bytes = specimen();
        put32(bytes, 0x20 + 32 + 8 + 4, offset);
        rejects([&] { (void) DatArchive(bytes); });
    }
    rejects([] { (void) DatArchive(archive(Bytes(4), {}, {{0, 0}}, {'x'})); });
    rejects([] { (void) DatArchive(archive(Bytes(4), {}, {{0, 0}}, {})); });
    rejects([] { (void) DatArchive(archive(Bytes(4), {}, {{0, 0}}, {0})); });
    rejects([] {
        (void) DatArchive(archive(Bytes(4), {}, {{0, 0}, {0, 2}}, {'x', 0, 'x', 0}));
    });
    rejects([] {
        (void) DatArchive(archive(Bytes(4), {}, {{0, 0}, {0, 0}}, {'x', 0}));
    });
    const DatArchive distinct(archive(Bytes(4), {}, {{0, 0}, {0, 1}},
                                      {'x', 'y', 0}));
    check(distinct.public_symbols()[0].name == "xy" &&
          distinct.public_symbols()[1].name == "y", "distinct names may share storage");
}

void external_links()
{
    rejects([] {
        (void) DatArchive(archive(Bytes(4), {}, {}, {'e', 'x', 't', 0}, 1));
    });
}

void referenced_region_boundaries()
{
    Bytes data(40, 0);
    put32(data, 0, 24);
    put32(data, 4, 16);
    put32(data, 8, 16); // Duplicate target must not become a zero-length region.
    put32(data, 12, 0); // Relocated root is also a public symbol target.
    const DatArchive dat(archive(data, {0, 4, 8, 12}, {{0, 0}, {32, 2}},
                                  {'a', 0, 'b', 0}));
    check(dat.next_target_offset(0) == 16, "smallest strictly greater relocation target");
    check(dat.next_target_offset(15) == 16, "an offset inside a referenced region");
    check(dat.next_target_offset(16) == 24, "duplicate targets do not split a region");
    check(dat.next_target_offset(24) == 32, "public target bounds a region");
    check(dat.next_target_offset(32) == 40 && dat.next_target_offset(39) == 40,
          "last region is bounded by the data-section end");
    rejects([&] { (void) dat.next_target_offset(40); });
    rejects([&] { (void) dat.next_target_offset(0xffffffff); });
    const DatArchive no_targets(archive(Bytes(4), {}, {}, {}));
    check(no_targets.next_target_offset(0) == 4, "data end without referenced targets");
    const DatArchive empty(archive({}, {}, {}, {}));
    rejects([&] { (void) empty.next_target_offset(0); });
}

void resource_limits()
{
    Bytes too_large(DatArchive::max_archive_bytes + 1, 0);
    rejects([&] { (void) DatArchive(too_large); });
    Bytes longest(DatArchive::max_symbol_name_bytes, 'x');
    longest.push_back(0);
    check(DatArchive(archive(Bytes(4), {}, {{0, 0}}, longest)).public_symbols()[0].name.size()
              == DatArchive::max_symbol_name_bytes,
          "documented maximum symbol length is accepted");
    longest.insert(longest.begin(), 'x');
    rejects([&] { (void) DatArchive(archive(Bytes(4), {}, {{0, 0}}, longest)); });
    for (const auto header_field : {8, 12, 16}) {
        auto bytes = specimen();
        put32(bytes, header_field, static_cast<std::uint32_t>(DatArchive::max_table_entries + 1));
        rejects([&] { (void) DatArchive(bytes); });
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::map<std::string, std::function<void()>> cases = {
        {"endian_reads", endian_reads}, {"pointer_semantics", pointer_semantics},
        {"ownership", ownership}, {"bounds", bounds},
        {"file_and_header_sizes", file_and_header_sizes}, {"table_counts", table_counts},
        {"relocation_slots", relocation_slots}, {"relocation_targets", relocation_targets},
        {"public_targets", public_targets}, {"public_names", public_names},
        {"external_links", external_links}, {"resource_limits", resource_limits},
        {"referenced_region_boundaries", referenced_region_boundaries},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "Usage: dat_archive_test <known case name>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& error) {
        std::cerr << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
