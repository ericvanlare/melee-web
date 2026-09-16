#pragma once
#include "dat_archive.hpp"
#include "dat_item_commands.h"
#include <map>
#include <memory>
namespace melee_web {
// Checked finite item scripts. This explicitly supports source waits, item
// variables and six-word item hitboxes; other families remain unavailable.
class DatItemCommands {
    std::map<uint32_t,std::unique_ptr<void,decltype(&melee_web_item_commands_destroy)>> scripts_;
public:
    void* decode(const DatArchive& a,uint32_t root){
        if(auto it=scripts_.find(root);it!=scripts_.end())return it->second.get();
        std::vector<uint32_t> words;bool end=false;uint32_t at=root;
        std::vector<uint32_t> loops;uint32_t execution_multiplier=1;
        const uint32_t limit=a.next_target_offset(root);
        for(unsigned step=0;step<1024;step++){
            if((at&3)||at>=limit||a.has_relocation(at))throw DatError("Item command instruction bounds or relocation invalid");
            uint32_t w=a.be32(at),op=w>>26;unsigned count=1;
            if(op==11)count=6;
            else if(op!=0&&op!=1&&op!=2&&op!=3&&op!=4&&op!=12&&op!=13&&op!=14&&op!=15&&op!=17&&op!=18&&op!=19)
                throw DatError("Item command opcode " + std::to_string(op) +
                               " is outside checked capabilities at source offset " +
                               std::to_string(at));
            if(op==3){
                const auto iterations=w&0x3ffffff;
                // CommandInfo has three return slots; each loop consumes two.
                if(!iterations||!loops.empty()||iterations>4096/execution_multiplier)
                    throw DatError("Item command loop exceeds checked execution/stack bound");
                loops.push_back(iterations);execution_multiplier*=iterations;
            }else if(op==4){
                if(loops.empty())throw DatError("Item command loop end has no matching start");
                execution_multiplier/=loops.back();loops.pop_back();
            }else if(op==0&&!loops.empty())throw DatError("Item command loop is unterminated");
            if(count*4>limit-at)throw DatError("Item command operands cross referenced region");
            for(unsigned k=0;k<count;k++){
                if(a.has_relocation(at+4*k))throw DatError("Item numeric command operand is relocated");
                words.push_back(a.be32(at+4*k));
            }
            at+=count*4;if(op==0){end=true;break;}
        }
        if(!end)throw DatError("Item script is unterminated or exceeds execution bound");
        void* out=melee_web_item_commands_create(words.data(),words.size());
        if(!out)throw DatError("Item native command conversion failed");
        scripts_.emplace(root,std::unique_ptr<void,decltype(&melee_web_item_commands_destroy)>(out,melee_web_item_commands_destroy));return out;
    }
};
}
