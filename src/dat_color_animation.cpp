#include "dat_color_animation.hpp"
namespace melee_web {
struct DatColorAnimation::Storage {
    std::vector<MeleeWebColorRow> rows;
    std::unique_ptr<DatCommands> commands;
};
DatColorAnimation::DatColorAnimation(std::shared_ptr<const DatArchive> archive,uint32_t root,size_t count)
:storage_(std::make_unique<Storage>()){
    if(!archive||!count||count>256||root%4||count*8!=archive->next_target_offset(root)-root)
        throw DatError("Color overlay table does not match its source count");
    auto& s=*storage_;s.rows.resize(count);std::vector<uint32_t> roots;
    for(size_t i=0;i<count;i++)if(auto p=archive->pointer(root+8*i,4))roots.push_back(*p);
    s.commands=std::make_unique<DatCommands>(archive,roots,DatCommandKind::ColorOverlay);
    for(size_t i=0;i<count;i++){
        const uint32_t at=root+8*i;auto p=archive->pointer(at,4);
        if(archive->has_relocation(at+4))throw DatError("Color overlay flags are relocated");
        s.rows[i].program=p?s.commands->at(*p):nullptr;
        auto flags=archive->range(at+4,4);s.rows[i].priority=flags[0];s.rows[i].layer=flags[1];
    }
}
DatColorAnimation::~DatColorAnimation()=default;
const MeleeWebColorRow* DatColorAnimation::table()const noexcept{return storage_->rows.data();}
}
