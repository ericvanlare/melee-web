#include "dat_shape_animation.hpp"
#include <fstream>
#include <iostream>
using namespace melee_web;
int main(int argc,char** argv){
 try{
    if(argc!=2)throw DatError("Expected owned EfCoData archive");
    std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    auto archive=std::make_shared<DatArchive>(bytes);uint32_t root=UINT32_MAX;
    for(const auto& s:archive->public_symbols())if(s.name=="effCommonDataTable")root=s.data_offset;
    if(root==UINT32_MAX)throw DatError("Common effect symbol missing");
    unsigned count=0,joints=0,dobjs=0;
    for(unsigned i=0;i<47;i++){
        const auto at=root+8+i*20;const auto shape=archive->pointer(at+16,12);if(!shape)continue;
        std::cerr<<"Validating source shape entry "<<i<<"\n";
        DatNativeJoint model(archive,*archive->pointer(at+4,64));
        DatShapeAnimation animation(archive,*shape,model.graph());
        if(!animation.descriptor())throw DatError("Source shape root lost");
        count++;joints+=animation.joint_count();dobjs+=animation.dobj_count();
        if(archive->pointer(*shape,12)){
            auto broken=bytes;
            for(unsigned k=0;k<4;k++)broken[32+*shape+k]=uint8_t(*shape>>(24-8*k));
            bool rejected=false;
            try{DatShapeAnimation invalid(std::make_shared<DatArchive>(broken),*shape,model.graph());}
            catch(const DatError&){rejected=true;}
            if(!rejected)throw DatError("Shape cycle accepted");
        }
        // Any present subtree must still map to actual source geometry.
        auto bad=model.graph();bad.joint_count=0;bool rejected=false;
        try{DatShapeAnimation invalid(archive,*shape,bad);}catch(const DatError&){rejected=true;}
        if(!rejected)throw DatError("Invalid model topology accepted");
    }
    if(count!=10||joints!=37||dobjs!=22)throw DatError("Common shape graph inventory changed");
    std::cout<<"Common effect sparse shape trees: "<<count<<" entries, "<<joints<<" joints, "<<dobjs<<" DObjs passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
