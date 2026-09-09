#include "dat_sis.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

namespace melee_web {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw DatError(message);
}
}
struct DatSis::Storage {
    std::vector<void*> table;
    std::vector<std::shared_ptr<void>> buffers;
    void* copy(std::span<const uint8_t> bytes) {
        require(!bytes.empty(), "Empty SIS byte array");
        void* result = std::aligned_alloc(32, (bytes.size() + 31) & ~size_t(31));
        if (!result) throw std::bad_alloc();
        std::shared_ptr<void> owner(result, std::free);
        std::memcpy(result, bytes.data(), bytes.size());
        buffers.push_back(std::move(owner));
        return result;
    }
};
DatSis::DatSis(std::shared_ptr<const DatArchive> archive, std::string_view symbol)
    : storage_(std::make_unique<Storage>()) {
    require(bool(archive), "Missing SIS archive");
    const auto& a = *archive;
    std::optional<uint32_t> root;
    for (const auto& entry : a.public_symbols())
        if (entry.name == symbol) root = entry.data_offset;
    require(root.has_value() && !(*root & 3), "Missing or unaligned SIS root");
    const auto table_bytes = a.next_target_offset(*root) - *root;
    require(!(table_bytes & 3) && table_bytes >= 12 && table_bytes <= 4096 * 4,
            "Invalid SIS pointer table size");
    auto& s = *storage_;
    s.table.resize(table_bytes / 4);
    auto region = [&](uint32_t offset) {
        auto bytes = a.range(offset, a.next_target_offset(offset) - offset);
        for (uint32_t p = offset & ~3U; p + 4 <= a.data().size() && p < offset + bytes.size(); p += 4)
            require(!a.has_relocation(p), "Unexpected relocation in SIS byte payload");
        return bytes;
    };
    // These field names are reversed in the recovered SIS declaration:
    // field 0 is I4 glyph pixels, field 1 is two-byte kerning pairs.
    const auto pixels = a.pointer(*root, 0);
    const auto kerning = a.pointer(*root + 4);
    require(pixels.has_value() == kerning.has_value(), "Incomplete SIS font data");
    size_t custom_glyphs = 0;
    if (pixels) {
        auto image_bytes = *pixels == a.data().size() ? a.range(*pixels, 0) : region(*pixels);
        auto kern_bytes = region(*kerning);
        require(!(*pixels & 31) && image_bytes.size() % 512 == 0 && kern_bytes.size() >= 2,
                "Invalid SIS I4 glyph atlas");
        custom_glyphs = std::min(image_bytes.size() / 512, kern_bytes.size() / 2);
        s.table[1] = s.copy(kern_bytes);
        // Text-only SIS archives can point their zero-length custom atlas at
        // end-of-data. Preserve a non-null one-past pointer into owned storage;
        // the zero glyph count below rejects every attempted custom glyph read.
        s.table[0] = image_bytes.empty() ?
            static_cast<uint8_t*>(s.table[1]) + kern_bytes.size() : s.copy(image_bytes);
    }
    std::map<uint32_t, void*> strings;
    size_t total_bytes = 0;
    for (uint32_t i = 2; i < s.table.size(); ++i) {
        auto offset = a.pointer(*root + 4 * i);
        if (!offset) continue;
        if (auto found = strings.find(*offset); found != strings.end()) {
            s.table[i] = found->second;
            continue;
        }
        auto bytes = region(*offset);
        require((total_bytes += bytes.size()) <= 4 * 1024 * 1024, "SIS bytecode exceeds budget");
        bool terminated = false;
        for (size_t p = 0; p < bytes.size();) {
            const auto opcode = bytes[p];
            if (!opcode) { terminated = true; break; }
            size_t length = 1;
            if (opcode >= 0x20) length = 2;
            else {
                require(opcode <= 26, "Unknown SIS opcode");
                require(opcode != 8 && opcode != 9,
                        "SIS branch relocation is not implemented");
                if (opcode == 5) length = 3;
                if (opcode == 12) length = 4;
                if (opcode == 6 || opcode == 7 || opcode == 10 || opcode == 14) length = 5;
            }
            require(length <= bytes.size() - p, "Truncated SIS instruction");
            if (opcode >= 0x20) {
                const uint16_t glyph = (uint16_t(opcode) << 8) | bytes[p + 1];
                require(glyph < 0x4000 ? glyph - 0x2000 < 287 :
                        size_t(glyph - 0x4000) < custom_glyphs,
                        "SIS glyph index exceeds its font atlas");
            }
            p += length;
        }
        require(terminated, "SIS string lacks a bounded terminator");
        // Preserve the full owned byte region: original SIS copy/append APIs
        // may use author-provided capacity after the first terminator.
        s.table[i] = s.copy(bytes);
        strings.emplace(*offset, s.table[i]);
    }
}
DatSis::~DatSis() = default;
void* DatSis::descriptor() const noexcept { return storage_->table.data(); }
unsigned DatSis::entry_count() const noexcept { return unsigned(storage_->table.size()); }
}
