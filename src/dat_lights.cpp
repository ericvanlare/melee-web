#include "dat_lights.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace melee_web {
namespace {
void region(const DatArchive& a, uint32_t p, uint32_t n) {
    if (p % 4) throw DatError("Light descriptor is unaligned");
    (void)a.range(p,n);
    if (n > a.next_target_offset(p)-p) throw DatError("Light descriptor crosses a referenced region");
}
float finite(const DatArchive& a,uint32_t p) {
    float f=a.f32(p);
    if (!std::isfinite(f)) throw DatError("Light descriptor contains a nonfinite scalar");
    return f;
}
void world(const DatArchive& a,uint32_t slot,uint8_t& present,float* xyz) {
    auto p=a.pointer(slot,20); present=p.has_value(); if (!p) return;
    region(a,*p,20);
    if(a.pointer(*p)||a.pointer(*p+16)) throw DatError("Light WObj custom class or constraints are unsupported");
    for(uint32_t i=0;i<3;i++) xyz[i]=finite(a,*p+4+i*4);
}
}
std::optional<uint8_t> read_dat_light_override(const DatArchive& a,uint32_t light_offset) {
    auto root=std::find_if(a.public_symbols().begin(),a.public_symbols().end(),
                         [](const auto& s){return s.name=="map_head";});
    if(root==a.public_symbols().end()) throw DatError("Stage map_head is missing for light override lookup");
    region(a,root->data_offset,48);
    uint32_t count=a.be32(root->data_offset+28);
    if(count>65536) throw DatError("Stage light override count exceeds resource budget");
    auto table=a.pointer(root->data_offset+24,8);
    if(count&&!table) throw DatError("Stage light override table is null");
    if(!count) return std::nullopt;
    const uint32_t end=a.next_target_offset(*table);
    for(uint32_t i=0;i<count;i++) {
        uint32_t p=*table+i*8;
        if(p>end||end-p<8) throw DatError("Stage light override identity is absent from the validated prefix; declared count crosses a referenced region");
        region(a,p,8);
        auto desc=a.pointer(p,28);
        if(desc&&*desc==light_offset) return a.range(p+4,1)[0]&0xe0;
    }
    return std::nullopt;
}
DatLights::DatLights(const DatArchive& a,const std::string& symbol,
                     bool retain_animation_tables) {
    auto root=std::find_if(a.public_symbols().begin(),a.public_symbols().end(),
                         [&](const auto& s){return s.name==symbol;});
    if(root==a.public_symbols().end()) throw DatError("Stage light public symbol is missing");
    root_offset=root->data_offset;
    region(a,root_offset,4);
    const uint32_t list_end=a.next_target_offset(root_offset);
    std::set<uint32_t> seen;
    for(uint32_t i=0;i<=64;i++) {
        if(root_offset+i*4>=list_end) throw DatError("Stage light list terminator is missing within its referenced region");
        region(a,root_offset+i*4,4);
        auto list=a.pointer(root_offset+i*4,8);
        if(!list) {if(lights.empty()) throw DatError("Original light loader requires a nonempty light list"); return;}
        if(i==64) throw DatError("Stage light list exceeds resource budget");
        region(a,*list,8);
        auto animation_table = a.pointer(*list + 4, 4);
        if(animation_table && !retain_animation_tables)
            throw DatError("Stage light animation is unsupported");
        if(animation_table) region(a, *animation_table, 4);
        auto d=a.pointer(*list,28); if(!d) throw DatError("Stage light descriptor is null");
        region(a,*d,28);
        if(!seen.insert(*d).second) throw DatError("Stage light list repeats a descriptor");
        if(a.pointer(*d)) throw DatError("Stage light custom class is unsupported");
        if(a.pointer(*d+4)) throw DatError("Stage light descriptor chains are unsupported");
        MeleeWebStageLightDesc out{};out.source_offset=*d;out.flags=a.be16(*d+8);out.attenuation_flags=a.be16(*d+10);
        if((out.flags&3)>1) throw DatError("Stage point/spot attenuation is unsupported");
        auto color=a.range(*d+12,4);std::copy(color.begin(),color.end(),out.color);
        world(a,*d+16,out.has_position,out.position);world(a,*d+20,out.has_interest,out.interest);
        if((out.flags&3)==1&&!out.has_position) throw DatError("Infinite stage light requires a position");
        auto shininess=a.pointer(*d+24,4);out.has_shininess=shininess.has_value();
        if(shininess){region(a,*shininess,4);out.shininess=finite(a,*shininess);}
        lights.push_back(out); animation_tables.push_back(animation_table);
    }
}
}
