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
    struct Script {
        void* allocation;
        void* entry;
        Script(void* allocation_,void* entry_):allocation(allocation_),entry(entry_){}
        Script(const Script&)=delete;
        Script& operator=(const Script&)=delete;
        ~Script(){melee_web_item_commands_destroy(allocation);}
    };
    std::map<uint32_t,std::unique_ptr<Script>> scripts_;
public:
    void* decode(const DatArchive& a,uint32_t root){
        if(auto it=scripts_.find(root);it!=scripts_.end())return it->second->entry;
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

        // Validate the actual lbcommand return stack over every reachable
        // source path. The graph walk above discovers all addresses, but its
        // visited set intentionally merges calls and can otherwise hide a
        // recursive Command_05, an unmatched Command_06, or a call that
        // shares an active two-slot loop frame. The source CommandInfo owns
        // only three event_return slots, so reject those paths before native
        // publication rather than allowing the original handlers to corrupt
        // the adjacent command state.
        struct StackEntry { uint32_t continuation; bool loop; };
        struct Flow { uint32_t at; std::vector<StackEntry> stack; };
        std::vector<Flow> pending{{root,{}}};
        std::set<std::string> flow_seen;
        auto command_count=[](uint32_t w){
            const auto op=w>>26;
            if(op==11)return 6U;
            if(op==10)return 5U;
            if(op==16){const auto sub=(w>>18)&0xff;return (sub<=2||sub==10||sub==11)?3U:2U;}
            return 1U;
        };
        while(!pending.empty()){
            auto flow=std::move(pending.back());pending.pop_back();
            std::string key=std::to_string(flow.at)+":";
            for(const auto& slot:flow.stack)
                key+=(slot.loop?'L':'R')+std::to_string(slot.continuation)+",";
            if(!flow_seen.insert(std::move(key)).second)continue;
            const auto word_it=source_words.find(flow.at);
            if(word_it==source_words.end())
                throw DatError("Item command control flow leaves the decoded source graph");
            const auto op=word_it->second>>26;
            auto next=[&](uint32_t at,std::vector<StackEntry> stack){
                if(source_words.contains(at))pending.push_back({at,std::move(stack)});
                else throw DatError("Item command control flow targets an undecoded source word");
            };
            if(op==0)continue;
            if(op==6){
                if(flow.stack.empty()||flow.stack.back().loop)
                    throw DatError("Item command return has no active subroutine");
                const auto continuation=flow.stack.back().continuation;flow.stack.pop_back();
                next(continuation,std::move(flow.stack));continue;
            }
            if(op==5){
                const auto branch=branches.find(flow.at+4);
                if(branch==branches.end())throw DatError("Item subroutine target is missing");
                if(flow.stack.size()>=3)
                    throw DatError("Item command subroutine stack exceeds source bound");
                flow.stack.push_back({flow.at+8,false});
                next(branch->second,std::move(flow.stack));continue;
            }
            if(op==7){
                const auto branch=branches.find(flow.at+4);
                if(branch==branches.end())throw DatError("Item goto target is missing");
                next(branch->second,std::move(flow.stack));continue;
            }
            if(op==3){
                if(flow.stack.size()>1)
                    throw DatError("Item command loop shares the three-slot return stack");
                flow.stack.push_back({0,true});flow.stack.push_back({0,true});
                next(flow.at+4,std::move(flow.stack));continue;
            }
            if(op==4){
                if(flow.stack.size()<2||!flow.stack.back().loop||!flow.stack[flow.stack.size()-2].loop)
                    throw DatError("Item command loop end has no active loop frame");
                flow.stack.pop_back();flow.stack.pop_back();
            }
            next(flow.at+command_count(word_it->second)*4,std::move(flow.stack));
        }
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
        void* allocation=melee_web_item_commands_create(words.data(),words.size());
        if(!allocation)throw DatError("Item native command conversion failed");
        const auto root_index=index_of.find(root);
        if(root_index==index_of.end()){
            melee_web_item_commands_destroy(allocation);
            throw DatError("Item command root was not emitted");
        }
        void* entry=melee_web_item_commands_entry(allocation,root_index->second);
        if(!entry){
            melee_web_item_commands_destroy(allocation);
            throw DatError("Item native command entry allocation failed");
        }
        auto script=std::make_unique<Script>(allocation,entry);
        scripts_.emplace(root,std::move(script));return entry;
    }
};
}
