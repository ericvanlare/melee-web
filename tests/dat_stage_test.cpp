#include "dat_stage.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace melee_web;
using Bytes = std::vector<uint8_t>;
namespace {
void check(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
template<class F> void rejects(F f) { try { f(); } catch (const DatError&) { return; } throw std::runtime_error("Expected DatError"); }
void put(Bytes& b, uint32_t p, uint32_t n) { for(int i=0;i<4;++i)b.at(p+i)=uint8_t(n>>(24-8*i)); }
struct Fixture {
    // First joint is data offset0; table has two records, of which the second
    // can describe a locator/service entry without a drawable joint.
    static constexpr uint32_t head=64, entries=128, joint2=256, auxiliary=384;
    Bytes data=Bytes(512); std::vector<uint32_t> relocs;
    void link(uint32_t p,uint32_t n){put(data,p,n);if(std::find(relocs.begin(),relocs.end(),p)==relocs.end())relocs.push_back(p);}
    void unlink(uint32_t p){put(data,p,0);std::erase(relocs,p);}
    Fixture(){ link(head+8,entries);put(data,head+12,2);link(entries,0);link(entries+52,joint2); }
    DatArchive archive()const{
        const std::string name="map_head";const auto pubs=uint32_t(32+data.size()+relocs.size()*4);
        Bytes bytes(pubs+8+name.size()+1);put(bytes,0,uint32_t(bytes.size()));put(bytes,4,uint32_t(data.size()));
        put(bytes,8,uint32_t(relocs.size()));put(bytes,12,1);
        std::copy(data.begin(),data.end(),bytes.begin()+32);
        for(uint32_t i=0;i<relocs.size();i++)put(bytes,uint32_t(32+data.size())+i*4,relocs[i]);
        put(bytes,pubs,head);std::copy(name.begin(),name.end(),bytes.begin()+pubs+8);
        return DatArchive(bytes);
    }
    DatStage read()const{return DatStage(archive());}
};
void entry_identity(){
 Fixture f; const auto s=f.read();
 check(s.root_offset==64&&s.symbol=="map_head"&&s.entries.size()==2,"explicit map root and count");
 check(s.entry_table.data_offset==128&&s.entry_table.element_bytes==52,"source descriptor stride");
 check(s.entries[0].index==0&&s.entries[0].descriptor_offset==128&&s.entries[0].joint_offset==0,"relocated zero joint is not null");
 check(s.entries[1].index==1&&s.entries[1].descriptor_offset==180&&s.entries[1].joint_offset==256,"entry identity independent of root offset");
 check(s.unapplied_services(0).empty(),"absent optional services are not claimed");
 f.unlink(Fixture::entries+52);check(!f.read().entries[1].joint_offset,"nullable source entry stays enumerable");
 f.unlink(Fixture::head+8);put(f.data,Fixture::head+12,0);check(f.read().entries.empty(),"empty map metadata remains inspectable");
}
void optional_service_references(){
 Fixture f;
 for(auto slot:{4U,8U,12U,16U,20U,24U,28U})f.link(Fixture::entries+slot,Fixture::auxiliary);
 f.link(Fixture::entries+40,Fixture::auxiliary+5); // Byte-aligned flag stream, no inferred count.
 const auto s=f.read(); const auto& e=s.entries[0];
 check(e.joint_animation_table==384&&e.material_animation_table==384&&e.shape_animation_table==384,"animation pointer tables retain identity");
 check(e.camera_offset==384&&e.unknown_14_offset==384&&e.light_table_offset==384&&e.fog_offset==384,"opaque service references stay unapplied");
 const auto services=s.unapplied_services(0);
 check(services.size()==8&&std::find(services.begin(),services.end(),"stage camera")!=services.end(),"all present entry services are exposed to the viewer");
 check(s.unapplied_services(1).empty(),"services belong to their actual entry");
 rejects([&]{(void)s.unapplied_services(2);});
}
void known_counted_regions(){
 Fixture f;
 f.link(Fixture::head,384);put(f.data,Fixture::head+4,2); //24-byte joint-reference table.
 f.link(Fixture::head+16,408);put(f.data,Fixture::head+20,2); //8-byte spline pointer array.
 f.link(Fixture::head+32,416);put(f.data,Fixture::head+36,1); //8-byte shadow record.
 f.link(Fixture::head+40,424);put(f.data,Fixture::head+44,1); //4-byte object pointer.
 f.link(Fixture::entries+32,430);put(f.data,Fixture::entries+36,2); // GrJoint permits2-byte alignment.
 f.link(Fixture::entries+44,442);put(f.data,Fixture::entries+48,3);
 const auto s=f.read();
 check(s.joint_reference_table.count==2&&s.shadow_table.element_bytes==8&&s.entries[0].collision_bindings.element_bytes==6&&s.entries[0].joint_indices.count==3,"known source table widths remain explicit");
 put(f.data,Fixture::head+4,3); rejects([&]{(void)f.read();}); // Crosses next referenced table.
 f=Fixture();f.link(Fixture::entries+44,510);put(f.data,Fixture::entries+48,2);rejects([&]{(void)f.read();});
 f=Fixture();f.link(Fixture::entries+32,383);put(f.data,Fixture::entries+36,1);rejects([&]{(void)f.read();});
}
void uninterpreted_service_count(){
 Fixture f;
 f.link(Fixture::head+24,384);put(f.data,Fixture::head+28,32);
 f.link(Fixture::head+40,400); // An opaque16-byte referenced region does not establish32 record widths.
 const auto s=f.read();
 check(s.light_override_table.count==32&&s.light_override_table.element_bytes==0,"unknown service count units preserved without guessed payload layout");
 const auto labels=s.unapplied_services(0);
 check(std::find(labels.begin(),labels.end(),"stage light overrides (payload not decoded)")!=labels.end(),"opaque payload limitation is explicit");
 put(f.data,Fixture::head+28,0xffffffff);rejects([&]{(void)f.read();});
}
void count_and_pointer_failures(){
 for(auto count:{257U,0x80000000U,0xffffffffU}){
  Fixture f;put(f.data,Fixture::head+12,count);rejects([&]{(void)f.read();});
 }
 for(auto slot:{0U,16U,24U,32U,40U}){
  Fixture f;put(f.data,Fixture::head+slot+4,1);rejects([&]{(void)f.read();});
 }
 Fixture f;f.unlink(Fixture::head+8);rejects([&]{(void)f.read();});
 f=Fixture();f.link(Fixture::head+8,130);rejects([&]{(void)f.read();});
 f=Fixture();put(f.data,Fixture::head+12,3);rejects([&]{(void)f.read();});
 f=Fixture();f.link(Fixture::entries,484);rejects([&]{(void)f.read();}); // Truncated joint header.
 f=Fixture();f.link(Fixture::entries+4,510);rejects([&]{(void)f.read();}); // Truncated pointer table header.
 f=Fixture();put(f.data,Fixture::entries+16,384);rejects([&]{(void)f.read();}); // Missing relocation.
 f=Fixture();rejects([&]{(void)DatStage(f.archive(),"missing");});
}
}
int main(int argc,char**argv){
 std::map<std::string,std::function<void()>>cases={{"entry_identity",entry_identity},{"optional_service_references",optional_service_references},{"known_counted_regions",known_counted_regions},{"uninterpreted_service_count",uninterpreted_service_count},{"count_and_pointer_failures",count_and_pointer_failures}};
 if(argc!=2||!cases.contains(argv[1]))return 2;
 try{cases.at(argv[1])();}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
