#pragma once
#include "dat_archive.hpp"
#include "dat_item_commands.h"
#include <map>
#include <memory>
#include <set>
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
        // Keep the graph keyed by authored source address until every
        // reachable path is known. Sorting by source address is important for
        // Command_05: after NEXT_CMD reaches its relocated pointer operand,
        // the original stores operand+1 as the return address. A DFS emission
        // would put the callee at that address and return into the callee.
        std::map<uint32_t,uint32_t> source_words;
        std::map<uint32_t,uint32_t> branches;
        std::set<uint32_t> visited;
        std::set<uint32_t> operands;
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
                if(operands.contains(at))
                    throw DatError("Item command branch enters an operand at " + std::to_string(at));
                if(visited.contains(at)) return;
                if(source_words.size()>=1024)
                    throw DatError("Item script exceeds the 1024-word checked bound");
                if((at&3)||a.has_relocation(at))
                    throw DatError("Item command word at "+std::to_string(at)+
                                   " is unaligned or relocated");
                uint32_t w=a.be32(at),op=w>>26;unsigned count=1;
                if(op==11)count=6;
                /* it_80278F2C consumes five words: arg2 word, ef_id/arg6 word
                 * and three Vec2 words. */
                else if(op==10)count=5;
                /* it_8027978C reads its sub-opcode from source bits 25..18.
                 * Low 0..2 and high 10..11 forms consume two operands; all
                 * other forms consume one operand before returning. */
                else if(op==16){
                    const auto sub=(w>>18)&0xff;
                    count=(sub<=2||sub==10||sub==11)?3:2;
                }
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
                visited.insert(at);source_words.emplace(at,w);
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
                    if(operands.contains(*target))
                        throw DatError("Item command jump target enters an operand");
                    if(source_words.size()>=1024)
                        throw DatError("Item script exceeds the 1024-word checked bound");
                    operands.insert(at+4);source_words.emplace(at+4,a.be32(at+4));
                    branches.emplace(at+4,*target);
                    if(op==7){walk_rec(*target,walk_rec);return;}
                    // The source return address is the command after the
                    // pointer operand. Discover it as an ordinary successor;
                    // the final address-sorted emission then keeps it at
                    // native pointer+1 even when the target is earlier.
                    walk_rec(at+8,walk_rec);
                    walk_rec(*target,walk_rec);
                    return;
                }
                for(unsigned k=0;k<count;k++){
                    const auto offset=at+4*k;
                    if(k&&a.has_relocation(offset))
                        throw DatError("Item numeric command operand is relocated");
                    if(k){
                        if(operands.contains(offset))
                            throw DatError("Item command operand overlaps an operand");
                        if(source_words.contains(offset))
                            throw DatError("Item command operand overlaps an instruction");
                        if(source_words.size()>=1024)
                            throw DatError("Item script exceeds the 1024-word checked bound");
                        operands.insert(offset);source_words.emplace(offset,a.be32(offset));
                    }
                }
                if(op==0||op==6)return;
                at+=count*4;
            }
            throw DatError("Item script exceeds the 1024-word checked bound");
        };
        walk(root,walk);
        if(source_words.empty()||source_words.size()>1024)
            throw DatError("Item script is empty or exceeds the checked bound");
        std::vector<uint32_t> words;std::map<uint32_t,size_t> index_of;
        words.reserve(source_words.size());
        for(const auto& [offset,word]:source_words){
            index_of.emplace(offset,words.size());words.push_back(word);
        }
        for(const auto& [operand,target]:branches){
            const auto target_index=index_of.find(target);
            if(target_index==index_of.end())
                throw DatError("Item command jump target was never decoded");
            const auto operand_index=index_of.find(operand);
            if(operand_index==index_of.end())
                throw DatError("Item command jump operand was not emitted");
            words[operand_index->second]=static_cast<uint32_t>(target_index->second);
        }
        void* out=melee_web_item_commands_create(words.data(),words.size());
        if(!out)throw DatError("Item native command conversion failed");
        scripts_.emplace(root,std::unique_ptr<void,decltype(&melee_web_item_commands_destroy)>(out,melee_web_item_commands_destroy));return out;
    }
};
}
