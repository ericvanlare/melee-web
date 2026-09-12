#include "dat_common.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace melee_web {
namespace {
MELEE_WEB_COMMON_ASSERT_LAYOUT(MeleeWebCommonScalars)

/* PlCo's CPU arrays cover the 32 indexed fighter kinds used by the CPU
 * routines.  The common roster's 33-row capacity includes the source-only
 * Sandbag/None tail, which is absent from this root22 graph. */
constexpr std::uint32_t cpu_table_count = 32;
constexpr std::uint32_t cpu_script_count = 62;
constexpr std::uint32_t cpu_table_fields = 7;
constexpr std::uint8_t cpu_cmd_done = 0x7f;
/* CpuCmd_Count in the pinned source.  The command enum has a deliberate gap
 * between the zero-argument and one-argument ranges, so keep the value
 * explicit in this host-portable archive decoder. */
constexpr std::uint8_t cpu_cmd_count = 0xc3;
constexpr std::size_t cpu_attack_entry_bytes = 9 * sizeof(std::uint32_t);
constexpr std::size_t cpu_attack_entry_limit = 31;
constexpr std::size_t cpu_script_limit = 0x100;

/* Private source type used by ftcpuattack.c; this is the serialized
 * nine-word layout, not a new gameplay representation. */
struct CpuAttackEntry {
    std::int32_t cmd;
    std::int32_t x04;
    float x08;
    float x0C;
    float x10;
    float x14;
    float weight;
    std::int32_t x1C;
    std::int32_t x20;
};
static_assert(sizeof(CpuAttackEntry) == cpu_attack_entry_bytes);

/* Source-compatible root22 layout.  This bridge deliberately avoids pulling
 * the target's headers into host-only DAT parser tests; under Wasm32 these
 * pointer fields have the same offsets and element types as Fighter's root. */
struct CpuRoot {
    std::uint8_t** cmdscripts;
    void** x4;
    void** x8;
    void** xC;
    void** x10;
    void** x14;
    void** x18;
    void** x1C;
    float* x20;
    void* x24;
};
static_assert(sizeof(CpuRoot) == 10 * sizeof(void*));
static_assert(offsetof(CpuRoot, x24) == 9 * sizeof(void*));

struct CpuStorage {
    CpuRoot root{};
    std::array<std::uint8_t*, cpu_script_count> scripts{};
    std::array<std::vector<std::uint8_t>, cpu_script_count> script_bytes{};
    std::array<std::array<void*, cpu_table_count>, cpu_table_fields> tables{};
    std::array<std::array<std::vector<CpuAttackEntry>, cpu_table_count>, cpu_table_fields>
        attack_entries{};
    std::array<float, cpu_table_count> distances{};
    std::array<float, 6> reach{};
};

void destroy_cpu_data(MeleeWebCommonCpuData* data)
{
    if (!data) return;
    delete static_cast<CpuStorage*>(data->storage);
    delete data;
}

void require_plain_words(const DatArchive& archive, std::uint32_t offset,
                         std::size_t bytes, const char* description)
{
    if (offset % 4 || bytes % 4)
        throw DatError(std::string("CPU ") + description + " is unaligned");
    (void) archive.range(offset, bytes);
    if (bytes > archive.next_target_offset(offset) - offset)
        throw DatError(std::string("CPU ") + description + " crosses a referenced region");
    for (std::size_t at = offset; at < std::size_t{offset} + bytes; at += 4)
        if (archive.has_relocation(static_cast<std::uint32_t>(at)))
            throw DatError(std::string("CPU ") + description + " contains a pointer relocation");
}

void bounded_region(const DatArchive& archive, std::uint32_t offset,
                    std::size_t bytes, const char* description)
{
    if (offset % 4 || bytes % 4)
        throw DatError(std::string("CPU ") + description + " is unaligned");
    (void) archive.range(offset, bytes);
    if (bytes > archive.next_target_offset(offset) - offset)
        throw DatError(std::string("CPU ") + description + " crosses a referenced region");
}

std::uint32_t required_pointer(const DatArchive& archive, std::uint32_t slot,
                               std::size_t bytes, const char* description)
{
    const auto target = archive.pointer(slot, bytes);
    if (!target)
        throw DatError(std::string("CPU ") + description + " is null");
    return *target;
}

std::size_t target_extent(const DatArchive& archive, std::uint32_t target,
                          const char* description)
{
    const auto end = archive.next_target_offset(target);
    if (end <= target)
        throw DatError(std::string("CPU ") + description + " has an empty extent");
    return end - target;
}

bool valid_cpu_command(std::uint8_t command)
{
    return (command >= 1 && command <= 25) || command == cpu_cmd_done ||
           (command >= 0x80 && command <= 0x95) ||
           (command >= 0xc0 && command <= 0xc2);
}

void reject_script_relocations(const DatArchive& archive, std::uint32_t target,
                               std::size_t extent)
{
    const auto end = std::size_t{target} + extent;
    for (auto slot = target & ~std::uint32_t{3}; std::size_t{slot} < end; slot += 4)
        if (std::size_t{slot} + 4 > target && archive.has_relocation(slot))
            throw DatError("CPU command script contains a pointer relocation");
}

void decode_script(const DatArchive& archive, std::uint32_t target,
                   std::vector<std::uint8_t>& output)
{
    const auto extent = target_extent(archive, target, "command script");
    if (!extent || extent > cpu_script_limit)
        throw DatError("CPU command script exceeds the source 0x100-byte buffer");
    reject_script_relocations(archive, target, extent);
    const auto bytes = archive.range(target, extent);
    std::size_t at = 0;
    bool done = false;
    while (at < bytes.size()) {
        const auto command = bytes[at++];
        if (command >= cpu_cmd_count || !valid_cpu_command(command))
            throw DatError("CPU command script contains an unknown opcode");
        if (command == cpu_cmd_done) {
            done = true;
            break;
        }
        const std::size_t argument_count = command > 0xBF ? 2 : command > 0x7F ? 1 : 0;
        if (argument_count > bytes.size() - at)
            throw DatError("CPU command script has truncated operands");
        at += argument_count;
    }
    if (!done)
        throw DatError("CPU command script has no Done terminator");
    for (; at < bytes.size(); ++at)
        if (bytes[at] != 0)
            throw DatError("CPU command script has nonzero bytes after Done");
    output.assign(bytes.begin(), bytes.end());
}

void decode_attack_list(const DatArchive& archive, std::uint32_t target,
                        std::vector<CpuAttackEntry>& output)
{
    const auto extent = target_extent(archive, target, "attack table");
    if (extent < cpu_attack_entry_bytes)
        throw DatError("CPU attack table is shorter than its terminator");
    const auto rows = extent / cpu_attack_entry_bytes;
    if (rows > cpu_attack_entry_limit + 1)
        throw DatError("CPU attack table exceeds the original 32-entry selection buffer");

    std::size_t count = 0;
    bool done = false;
    for (; count < rows; ++count) {
        const auto row = target + static_cast<std::uint32_t>(count * cpu_attack_entry_bytes);
        require_plain_words(archive, row, cpu_attack_entry_bytes, "attack entry");
        const auto command = static_cast<std::int32_t>(archive.be32(row));
        if (command == 0) {
            done = true;
            break;
        }
        if (command < 1 || command >= static_cast<std::int32_t>(cpu_script_count))
            throw DatError("CPU attack table references an invalid command script");
        if (std::bit_cast<std::int32_t>(archive.be32(row + 0x1C)) <= 0)
            throw DatError("CPU attack table has a nonpositive selection divisor");
        for (unsigned field = 2; field <= 6; ++field)
            if (!std::isfinite(archive.f32(row + field * 4)))
                throw DatError("CPU attack table has a nonfinite floating field");
    }
    if (!done)
        throw DatError("CPU attack table has no terminator");
    const auto consumed = (count + 1) * cpu_attack_entry_bytes;
    for (std::size_t at = consumed; at < extent; ++at)
        if (archive.data()[target + at] != 0)
            throw DatError("CPU attack table has nonzero trailing padding");

    output.resize(count + 1);
    for (std::size_t i = 0; i <= count; ++i) {
        const auto row = target + static_cast<std::uint32_t>(i * cpu_attack_entry_bytes);
        auto& entry = output[i];
        entry.cmd = static_cast<std::int32_t>(archive.be32(row));
        entry.x04 = static_cast<std::int32_t>(archive.be32(row + 4));
        entry.x08 = archive.f32(row + 8);
        entry.x0C = archive.f32(row + 0xC);
        entry.x10 = archive.f32(row + 0x10);
        entry.x14 = archive.f32(row + 0x14);
        entry.weight = archive.f32(row + 0x18);
        entry.x1C = static_cast<std::int32_t>(archive.be32(row + 0x1C));
        entry.x20 = static_cast<std::int32_t>(archive.be32(row + 0x20));
    }
}

MeleeWebCommonCpuData* decode_cpu_data(const DatArchive& archive,
                                       std::uint32_t root_offset)
{
    auto storage = std::make_unique<CpuStorage>();
    bounded_region(archive, root_offset, 10 * 4, "root22 descriptor");

    const auto command_table = required_pointer(archive, root_offset, cpu_script_count * 4,
                                                "command script table");
    bounded_region(archive, command_table, cpu_script_count * 4, "command script pointers");
    if (archive.pointer(command_table, 1))
        throw DatError("CPU command script table row zero must be null");
    for (std::uint32_t i = 1; i < cpu_script_count; ++i) {
        const auto target = required_pointer(archive, command_table + i * 4, 1,
                                             "command script");
        decode_script(archive, target, storage->script_bytes[i]);
        storage->scripts[i] = storage->script_bytes[i].data();
    }

    for (std::uint32_t field = 1; field <= 7; ++field) {
        const auto table = required_pointer(archive, root_offset + field * 4,
                                            cpu_table_count * 4, "attack table pointer array");
        bounded_region(archive, table, cpu_table_count * 4, "attack table pointers");
        for (std::uint32_t kind = 0; kind < cpu_table_count; ++kind) {
            const auto target = archive.pointer(table + kind * 4);
            if (!target) continue;
            auto& entries = storage->attack_entries[field - 1][kind];
            decode_attack_list(archive, *target, entries);
            storage->tables[field - 1][kind] = entries.data();
        }
    }

    const auto distance_table = required_pointer(archive, root_offset + 8 * 4,
                                                  cpu_table_count * 4, "distance table");
    require_plain_words(archive, distance_table, cpu_table_count * 4, "distance values");
    for (std::uint32_t kind = 0; kind < cpu_table_count; ++kind) {
        storage->distances[kind] = archive.f32(distance_table + kind * 4);
        if (!std::isfinite(storage->distances[kind]))
            throw DatError("CPU distance table contains a nonfinite value");
    }

    const auto reach_table = required_pointer(archive, root_offset + 9 * 4, 6 * 4,
                                              "reach table");
    require_plain_words(archive, reach_table, 6 * 4, "reach values");
    for (std::uint32_t i = 0; i < 6; ++i) {
        storage->reach[i] = archive.f32(reach_table + i * 4);
        if (!std::isfinite(storage->reach[i]))
            throw DatError("CPU reach table contains a nonfinite value");
    }

    storage->root.cmdscripts = storage->scripts.data();
    storage->root.x4 = storage->tables[0].data();
    storage->root.x8 = storage->tables[1].data();
    storage->root.xC = storage->tables[2].data();
    storage->root.x10 = storage->tables[3].data();
    storage->root.x14 = storage->tables[4].data();
    storage->root.x18 = storage->tables[5].data();
    storage->root.x1C = storage->tables[6].data();
    storage->root.x20 = storage->distances.data();
    storage->root.x24 = storage->reach.data();

    auto data = std::make_unique<MeleeWebCommonCpuData>();
    data->root = &storage->root;
    data->storage = storage.release();
    data->refs = 1;
    data->destroy = &destroy_cpu_data;
    return data.release();
}

void region(const DatArchive& archive, std::uint32_t offset, std::size_t size)
{
    if (offset % 4) throw DatError("Common-data descriptor is unaligned");
    (void) archive.range(offset, size);
    if (size > archive.next_target_offset(offset) - offset)
        throw DatError("Common-data descriptor crosses a referenced region");
}
float finite_scalar(const DatArchive& archive, std::uint32_t offset)
{
    const float value = archive.f32(offset);
    if (!std::isfinite(value)) throw DatError("Common-data float is nonfinite");
    return value;
}
void plain(const DatArchive& archive, std::uint32_t offset, std::size_t bytes, bool aligned = true)
{
    if (aligned) region(archive,offset,bytes);
    else {
        (void) archive.range(offset,bytes);
        if (bytes > archive.next_target_offset(offset)-offset)
            throw DatError("Common byte array crosses a referenced region");
    }
    for (std::uint32_t slot=offset&~3U; slot<offset+bytes; slot+=4)
        // A final byte array need not have word padding. Validated relocation
        // slots always contain a complete word, so a partial tail cannot be one.
        if (std::size_t(slot)+4<=archive.data().size() && archive.has_relocation(slot))
            throw DatError("Common scalar/byte array contains a relocation");
}
std::uint32_t count_word(const DatArchive& archive, std::uint32_t offset)
{
    if (archive.has_relocation(offset)) throw DatError("Common count is incorrectly relocated");
    return archive.be32(offset);
}
std::uint32_t required(const DatArchive& archive, std::uint32_t slot, std::size_t bytes)
{
    const auto result=archive.pointer(slot,bytes);
    if (!result) throw DatError("Required common table pointer is null");
    return *result;
}
void floats(const DatArchive& archive, std::uint32_t offset, float* out, std::size_t count)
{
    plain(archive,offset,count*4);
    for(std::size_t i=0;i<count;++i) out[i]=finite_scalar(archive,offset+std::uint32_t(i*4));
}
void shake(const DatArchive& archive, std::uint32_t offset, MeleeWebCommonShake& out)
{
    region(archive,offset,8);
    out.count=count_word(archive,offset+4);
    if (!out.count || out.count>MELEE_WEB_COMMON_MAX_SHAKE)
        throw DatError("Common shake count cannot be represented by the source runtime index");
    const auto samples=required(archive,offset,out.count*8);
    plain(archive,samples,out.count*8);
    for(std::uint32_t i=0;i<out.count;++i)
        out.samples[i]={finite_scalar(archive,samples+i*8),finite_scalar(archive,samples+i*8+4)};
}
void static_tables(const DatArchive& archive, DatCommon& common)
{
    auto& t=common.tables;
    for (auto& root:common.roots) {
        if (!(MELEE_WEB_COMMON_STATIC_ROOT_MASK&(1U<<root.index)) || !root.data_offset) continue;
        const auto offset=*root.data_offset;
        try {
        switch(root.index) {
        case 1:
            plain(archive,offset,26*12);
            for(std::uint32_t i=0;i<26;++i) t.item_throw[i]={finite_scalar(archive,offset+i*12),
                finite_scalar(archive,offset+i*12+4),finite_scalar(archive,offset+i*12+8)};
            break;
        case 2:
            plain(archive,offset,6*5*4);
            for(std::uint32_t row=0;row<6;++row) for(std::uint32_t col=0;col<5;++col)
                t.swing[row][col]=finite_scalar(archive,offset+(row*5+col)*4);
            break;
        case 3: floats(archive,offset,t.stale,9); break;
        case 4:
            region(archive,offset,MELEE_WEB_COMMON_PART_TABLES*4);
            for(std::uint32_t kind=0;kind<MELEE_WEB_COMMON_PART_TABLES;++kind) {
                const auto desc=required(archive,offset+kind*4,12);
                region(archive,desc,12);
                auto& parts=kind<MELEE_WEB_COMMON_FIGHTERS?t.parts[kind]:t.none_parts;
                parts.part_count=count_word(archive,desc+8);
                if (!parts.part_count || parts.part_count>MELEE_WEB_COMMON_MAX_PARTS)
                    throw DatError("Common part count exceeds original Fighter storage");
                const auto joint=required(archive,desc,parts.part_count);
                const auto names=required(archive,desc+4,MELEE_WEB_COMMON_PART_NAMES);
                plain(archive,joint,parts.part_count,false);
                plain(archive,names,MELEE_WEB_COMMON_PART_NAMES,false);
                const auto j=archive.range(joint,parts.part_count), n=archive.range(names,MELEE_WEB_COMMON_PART_NAMES);
                for(auto value:j) if(value!=255 && value>=MELEE_WEB_COMMON_PART_NAMES)
                    throw DatError("Common joint-to-part index exceeds the source named-part domain");
                for(auto value:n) if(value!=255 && value>=parts.part_count)
                    throw DatError("Common named part refers outside the fighter skeleton");
                std::copy(j.begin(),j.end(),parts.joint_to_part);
                std::copy(n.begin(),n.end(),parts.part_to_joint);
            }
            break;
        case 5:
            if (!(t.ready_mask&(1U<<4))) throw DatError("Common alternate tables require decoded part maps");
            region(archive,offset,MELEE_WEB_COMMON_FIGHTERS*4);
            for(std::uint32_t kind=0;kind<MELEE_WEB_COMMON_FIGHTERS;++kind) {
                const auto desc=archive.pointer(offset+kind*4,8);
                if (!desc) continue;
                region(archive,*desc,8);
                auto& alt=t.alternates[kind]; alt.has_descriptor=1; alt.count=count_word(archive,*desc+4);
                if(alt.count>MELEE_WEB_COMMON_MAX_ALTERNATES) throw DatError("Common alternate count exceeds source mask width");
                const auto entries=archive.pointer(*desc,alt.count?alt.count*4:1);
                if(alt.count && !entries) throw DatError("Common alternate entries are missing");
                if(alt.count) plain(archive,*entries,alt.count*4,false);
                bool seen[MELEE_WEB_COMMON_MAX_PARTS]={};
                for(std::uint32_t i=0;i<alt.count;++i) {
                    const auto raw=archive.range(*entries+i*4,4); const auto count=t.parts[kind].part_count;
                    if(raw[0]>=count || raw[1]>=count || raw[2]>3 || (raw[3]!=255 && raw[3]>=count) || seen[raw[0]])
                        throw DatError("Common alternate part entry is invalid or repeated");
                    seen[raw[0]]=true; alt.entries[i]={raw[0],raw[1],raw[2],raw[3]};
                }
            }
            break;
        case 9:
            region(archive,offset,3*8);
            for(std::uint32_t i=0;i<3;++i) shake(archive,offset+i*8,t.damage_shake[i]);
            break;
        case 10: shake(archive,offset,t.grab_shake); break;
        case 11: shake(archive,offset,t.smash_shake); break;
        case 12: floats(archive,offset,t.scale_modifiers,39); break;
        case 13: floats(archive,offset,t.bunny_modifiers,15); break;
        case 14: floats(archive,offset,t.metal_modifiers,9); break;
        case 15: floats(archive,offset,t.gravity_weight,2); break;
        case 18: case 19: {
            plain(archive,offset,5*4);
            auto* colors=root.index==18?t.primary_colors:t.secondary_colors;
            for(std::uint32_t i=0;i<5;++i) {
                const auto raw=archive.range(offset+i*4,4); colors[i]={raw[0],raw[1],raw[2],raw[3]};
            }
            break;
        }
        case 21:
            plain(archive,offset,0x44);
#define CROWD_F32(offset,member) t.crowd.member=finite_scalar(archive,*root.data_offset+(offset));
#define CROWD_I32(offset,member) t.crowd.member=std::bit_cast<std::int32_t>(archive.be32(*root.data_offset+(offset)));
#define CROWD_FIELD(offset,kind,member) CROWD_##kind(offset,member)
            MELEE_WEB_CROWD_FIELDS(CROWD_FIELD)
#undef CROWD_FIELD
#undef CROWD_I32
#undef CROWD_F32
            break;
        }
        } catch(const DatError& error) {
            throw DatError("Common root "+std::to_string(root.index)+" ("+std::string(root.source_global)+
                           "): "+error.what());
        }
        t.ready_mask|=1U<<root.index;
        root.readiness=DatCommonReadiness::StaticTablesDecoded;
    }
}
}
DatCommon::DatCommon(const DatArchive& archive)
{
    const auto& symbols = archive.public_symbols();
    const auto found = std::find_if(symbols.begin(), symbols.end(),
        [](const auto& symbol) { return symbol.name == "ftLoadCommonData"; });
    if (found == symbols.end()) throw DatError("Common-data ftLoadCommonData root is missing");
    descriptor_offset = found->data_offset;
    region(archive, descriptor_offset, MELEE_WEB_COMMON_ROOT_COUNT * 4);
#define LOAD_ROOT(index, name) do { \
    const auto offset = archive.pointer(descriptor_offset + (index) * 4); \
    roots[index] = {index, #name, offset, offset ? DatCommonReadiness::Unresolved : DatCommonReadiness::Missing}; \
} while (false);
    MELEE_WEB_COMMON_ROOTS(LOAD_ROOT)
#undef LOAD_ROOT
    if (!roots[0].data_offset) throw DatError("Required ftCommonData scalar root is null");
    scalar_offset = *roots[0].data_offset;
    region(archive, scalar_offset, MELEE_WEB_COMMON_SCALAR_BYTES);
    for (std::uint32_t offset = 0; offset < MELEE_WEB_COMMON_SCALAR_BYTES; offset += 4)
        if (archive.has_relocation(scalar_offset + offset))
            throw DatError("Common scalar block has an unsupported internal relocation");
#define LOAD_F32(offset, member) scalars.member = finite_scalar(archive, scalar_offset + (offset));
#define LOAD_I32(offset, member) scalars.member = std::bit_cast<std::int32_t>(archive.be32(scalar_offset + (offset)));
#define LOAD_U32(offset, member) scalars.member = archive.be32(scalar_offset + (offset));
#define LOAD_OPAQUE32(offset, member) LOAD_U32(offset, member)
#define LOAD_BYTE(offset, member) scalars.member = archive.range(scalar_offset + (offset), 1)[0];
#define LOAD_FIELD(offset, kind, member) LOAD_##kind(offset, member)
    MELEE_WEB_COMMON_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
#undef LOAD_BYTE
#undef LOAD_OPAQUE32
#undef LOAD_U32
#undef LOAD_I32
#undef LOAD_F32
    roots[0].readiness = DatCommonReadiness::ScalarsDecoded;
    static_tables(archive,*this);
    if (roots[22].data_offset) {
        /* decode_cpu_data starts with one owner reference.  Transfer it only
         * after the complete graph has validated, so malformed DAT input
         * cannot leak a partially constructed source graph. */
        std::unique_ptr<MeleeWebCommonCpuData, void (*)(MeleeWebCommonCpuData*)> data(
            decode_cpu_data(archive, *roots[22].data_offset), melee_web_common_cpu_release);
        cpu_data_ = data.release();
        tables.cpu_data = cpu_data_;
        roots[22].readiness = DatCommonReadiness::CpuDataDecoded;
    }
}

DatCommon::DatCommon(const DatCommon& other)
    : descriptor_offset(other.descriptor_offset), scalar_offset(other.scalar_offset),
      roots(other.roots), scalars(other.scalars), tables(other.tables),
      cpu_data_(other.cpu_data_)
{
    if (cpu_data_ && !melee_web_common_cpu_retain(cpu_data_))
        throw DatError("Cannot retain decoded CPU common data");
    tables.cpu_data = cpu_data_;
}

DatCommon& DatCommon::operator=(const DatCommon& other)
{
    if (this == &other) return *this;
    if (other.cpu_data_ && !melee_web_common_cpu_retain(other.cpu_data_))
        throw DatError("Cannot retain decoded CPU common data");
    melee_web_common_cpu_release(cpu_data_);
    descriptor_offset = other.descriptor_offset;
    scalar_offset = other.scalar_offset;
    roots = other.roots;
    scalars = other.scalars;
    tables = other.tables;
    cpu_data_ = other.cpu_data_;
    tables.cpu_data = cpu_data_;
    return *this;
}

DatCommon::DatCommon(DatCommon&& other) noexcept
    : descriptor_offset(other.descriptor_offset), scalar_offset(other.scalar_offset),
      roots(std::move(other.roots)), scalars(other.scalars), tables(other.tables),
      cpu_data_(other.cpu_data_)
{
    tables.cpu_data = cpu_data_;
    other.cpu_data_ = nullptr;
    other.tables.cpu_data = nullptr;
}

DatCommon& DatCommon::operator=(DatCommon&& other) noexcept
{
    if (this == &other) return *this;
    melee_web_common_cpu_release(cpu_data_);
    descriptor_offset = other.descriptor_offset;
    scalar_offset = other.scalar_offset;
    roots = std::move(other.roots);
    scalars = other.scalars;
    tables = other.tables;
    cpu_data_ = other.cpu_data_;
    tables.cpu_data = cpu_data_;
    other.cpu_data_ = nullptr;
    other.tables.cpu_data = nullptr;
    return *this;
}

DatCommon::~DatCommon()
{
    melee_web_common_cpu_release(cpu_data_);
}
} // namespace melee_web
