#pragma once
#include "dat_archive.hpp"
#include "dat_item_commands.h"
#include <map>
#include <memory>
namespace melee_web {
// Checked finite item scripts. This explicitly supports source waits, item
// variables, six-word item hitboxes, effect spawns (it_80278F2C), the
// texture/effect interpreter entry (it_8027978C) and subroutine/goto control
// flow; other families remain unavailable.
class DatItemCommands {
    std::map<uint32_t,std::unique_ptr<void,decltype(&melee_web_item_commands_destroy)>> scripts_;
public:
    void* decode(const DatArchive& a,uint32_t root){
        if(auto it=scripts_.find(root);it!=scripts_.end())return it->second.get();
        std::vector<uint32_t> words;
        std::map<uint32_t,size_t> index_of;
        std::vector<std::pair<size_t,uint32_t>> jump_words;
        std::vector<uint32_t> loops;uint32_t execution_multiplier=1;
        // The walk follows the original interpreter's static control flow:
        // opcodes 5 (subroutine) and 7 (goto) carry a relocated pointer word
        // whose target decodes recursively, opcode 6 returns and opcode 0
        // ends a path. Joined targets decode once. Every consumed word is
        // validated as authored script data and the emitted script is
        // bounded at 1024 words.
        auto walk=[&](uint32_t from,auto&& walk_rec)->void{
            uint32_t at=from;
            for(unsigned step=0;step<1024;++step){
                if(auto seen=index_of.find(at);seen!=index_of.end()){
                    (void)seen;return;
                }
                if(words.size()>=1024)
                    throw DatError("Item script exceeds the 1024-word checked bound");
                if((at&3)||a.has_relocation(at))
                    throw DatError("Item command word at "+std::to_string(at)+
                                   " is unaligned or relocated");
                uint32_t w=a.be32(at),op=w>>26;unsigned count=1;
                if(op==11)count=6;
                /* it_80278F2C consumes five words: arg2 word, ef_id/arg6 word
                 * and three Vec2 words. */
                else if(op==10)count=5;
                /* it_8027978C reads its sub-opcode from source bits 25..18
                 * and always consumes the command word plus two operand
                 * words. The low 0..2 forms use both operands; the other
                 * forms ignore them but still advance over both. */
                else if(op==16)count=3;
                else if(op!=0&&op!=1&&op!=2&&op!=3&&op!=4&&op!=5&&op!=6&&op!=7&&op!=8&&op!=9&&op!=12&&
                        op!=13&&op!=14&&op!=15&&op!=17&&op!=18&&op!=19)
                    throw DatError("Item command opcode " + std::to_string(op) +
                                   " is outside checked capabilities at source offset " +
                                   std::to_string(at));
                if(op==3){
                    const auto iterations=w&0x3ffffff;
                    // CommandInfo has three return slots; each loop consumes
                    // two. The multiplier is a conservative runtime bound for
                    // the whole remainder, which is why op4 divides it back.
                    if(!iterations||!loops.empty()||iterations>4096/execution_multiplier)
                        throw DatError("Item command loop exceeds checked execution/stack bound");
                    loops.push_back(iterations);execution_multiplier*=iterations;
                }else if(op==4){
                    if(loops.empty())throw DatError("Item command loop end has no matching start");
                    execution_multiplier/=loops.back();loops.pop_back();
                }
                index_of.emplace(at,words.size());
                if(op==5||op==7){
                    // [opcode word][relocated target pointer word]. The
                    // runtime advances past the opcode word first, so the
                    // subroutine return address is the word after the
                    // pointer word. Targets are followed recursively and
                    // patched to native indices after the walk.
                    if((at+4)&3||!a.has_relocation(at+4))
                        throw DatError("Item command jump operand at "+std::to_string(at+4)+
                                       " is not a checked pointer");
                    const auto target=a.pointer(at+4,4);
                    if(!target||(*target&3))
                        throw DatError("Item command jump target is unaligned or missing");
                    words.push_back(w);
                    size_t j=words.size();words.push_back(0);
                    walk_rec(*target,walk_rec);
                    jump_words.push_back({j,*target});
                    if(op==7)return;
                    at+=8;continue;
                }
                for(unsigned k=0;k<count;k++){
                    if(k&&a.has_relocation(at+4*k))
                        throw DatError("Item numeric command operand is relocated");
                    words.push_back(a.be32(at+4*k));
                }
                if(op==0||op==6)return;
                at+=count*4;
            }
            throw DatError("Item script exceeds the 1024-word checked bound");
        };
        walk(root,walk);
        for(auto& [index,target]:jump_words){
            auto it=index_of.find(target);
            if(it==index_of.end())
                throw DatError("Item command jump target was never decoded");
            words[index]=(words[index]&0xfc000000u)|it->second;
        }
        void* out=melee_web_item_commands_create(words.data(),words.size());
        if(!out)throw DatError("Item native command conversion failed");
        scripts_.emplace(root,std::unique_ptr<void,decltype(&melee_web_item_commands_destroy)>(out,melee_web_item_commands_destroy));return out;
    }
};
}
