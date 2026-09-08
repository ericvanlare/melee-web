#include "dat_common.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace melee_web {
namespace {
MELEE_WEB_COMMON_ASSERT_LAYOUT(MeleeWebCommonScalars)
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
            region(archive,offset,MELEE_WEB_COMMON_FIGHTERS*4);
            for(std::uint32_t kind=0;kind<MELEE_WEB_COMMON_FIGHTERS;++kind) {
                const auto desc=required(archive,offset+kind*4,12);
                region(archive,desc,12);
                auto& parts=t.parts[kind];
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
}
} // namespace melee_web
