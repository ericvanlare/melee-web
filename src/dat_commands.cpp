#include "dat_commands.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <set>
#include <utility>
namespace melee_web {
namespace {
void require(bool v, const char* message) { if (!v) throw DatError(message); }
std::vector<DatCommandRoot> unbounded_roots(std::span<const uint32_t> roots)
{
    std::vector<DatCommandRoot> result;
    result.reserve(roots.size());
    for (const auto root : roots) result.push_back({root, 0.0f, 0});
    return result;
}
void* color_commands(const std::vector<MeleeWebCommandWord>& words){
    require(sizeof(void*)==4,"Original color command pointers require a 32-bit target");
    auto* out=static_cast<uint32_t*>(std::calloc(words.size(),4));
    require(out!=nullptr,"Cannot allocate native color command graph");
    for(size_t i=0;i<words.size();i++)out[i]=words[i].word;
    for(size_t i=0;i<words.size();i++)if(words[i].target!=UINT32_MAX)
        out[i+1]=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&out[words[i].target]));
    return out;
}
}
DatCommands::DatCommands(std::shared_ptr<const DatArchive> archive, std::span<const uint32_t> roots, DatCommandKind kind)
    : DatCommands(std::move(archive), unbounded_roots(roots), kind) {}

DatCommands::DatCommands(std::shared_ptr<const DatArchive> archive, std::span<const DatCommandRoot> roots, DatCommandKind kind):kind_(kind)
{
    require(bool(archive), "Command archive is null");
    std::map<uint32_t, uint32_t> words;
    std::map<uint32_t, uint32_t> branches;
    std::set<uint32_t> operands;
    const size_t stack_capacity=kind==DatCommandKind::ColorOverlay?6:3;
    for (const DatCommandRoot root_info : roots) {
        const uint32_t root = root_info.offset;
        if (kind == DatCommandKind::Fighter && root_info.animation_end_frame != 0.0f)
            require(std::isfinite(root_info.animation_end_frame) && root_info.animation_end_frame > 0.0f &&
                    root_info.animation_end_frame <= 65535.0f,
                    "Fighter command animation end frame is outside the source range");
        struct StackEntry { bool loop; uint32_t address; uint32_t remaining; };
        std::vector<StackEntry> returns;
        auto stack_slots = [&] { size_t n=0;for(auto e:returns)n+=e.loop?2:1;return n; };
        uint32_t at = root;
        std::map<std::vector<uint32_t>, size_t> visited;
        std::vector<uint32_t> history;
        bool terminated = false;
        for (unsigned steps = 0; steps < 4096; ++steps) {
            require(at % 4 == 0, "Command instruction is unaligned");
            require(!operands.contains(at), "Command branch enters an operand");
            require(!archive->has_relocation(at), "Command opcode is a relocated pointer");
            const uint32_t word = archive->be32(at), op = word >> 26;
            std::vector<uint32_t> state;
            for(auto e:returns){state.push_back(e.loop);state.push_back(e.address);state.push_back(e.remaining);}
            state.push_back(at);
            if (auto seen = visited.find(state); seen != visited.end()) {
                bool relative = false, animation_wait = false;
                uint32_t maximum_async_timer = 0;
                for (size_t j = seen->second; j < history.size(); ++j) {
                    const auto v = history[j];
                    relative |= (v >> 26) == (kind==DatCommandKind::ColorOverlay?11:1) && (v & 0x3ffffff) != 0;
                    animation_wait |= (v >> 26) == 8;
                    if (kind == DatCommandKind::Fighter && (v >> 26) == 2)
                        maximum_async_timer = std::max(maximum_async_timer, v & 0x3ffffff);
                }
                /* Command_02 is asynchronous only when it is the sole
                 * scheduler yield in a cycle. Relative waits and the source
                 * animation wait already prove that the cycle yields, so do
                 * not impose a frame witness on those original loops. */
                if (!relative && !animation_wait) {
                    if (kind == DatCommandKind::Fighter && maximum_async_timer != 0) {
                        require(root_info.animation_end_frame > 0.0f &&
                                root_info.animation_loops != 0,
                                "Asynchronous timer cycle lacks a looping source animation witness");
                        require(static_cast<float>(maximum_async_timer) <= root_info.animation_end_frame &&
                                static_cast<float>(maximum_async_timer) >= root_info.animation_end_frame,
                                "Asynchronous timer cycle does not reach the source animation boundary");
                    } else {
                        throw DatError("Command graph has a non-yielding cycle (root " +
                                       std::to_string(root) + ", instruction " + std::to_string(at) + ")");
                    }
                }
                terminated = true; break;
            }
            visited.emplace(std::move(state), history.size()); history.push_back(word);
            words[at] = word;
            if (op == 0 || (kind==DatCommandKind::ColorOverlay&&op==10)) { terminated = true; break; }
            if (op == 6) {
                require(!returns.empty() && !returns.back().loop, "Command return stack underflow or loop mismatch");
                at = returns.back().address; returns.pop_back(); continue;
            }
            if (op == 3) {
                const uint32_t count=word & 0x3ffffff;
                require(count>0 && count<=4096,"Command loop count is zero or exceeds bound");
                require(stack_slots()+2<=stack_capacity,"Command loop stack overflow");
                returns.push_back({true,at+4,count});at+=4;continue;
            }
            if (op == 4) {
                require(!returns.empty() && returns.back().loop,"Command loop end without matching start");
                if(--returns.back().remaining)at=returns.back().address;
                else {returns.pop_back();at+=4;}
                continue;
            }
            if (op == 5 || op == 7) {
                require(archive->has_relocation(at + 4), "Command branch lacks a relocation");
                auto target = archive->pointer(at + 4, 4);
                require(target.has_value(), "Command branch target is null");
                require(!words.contains(at + 4) || operands.contains(at + 4), "Command operand overlaps an instruction");
                operands.insert(at + 4); words[at + 4] = archive->be32(at + 4); branches[at] = *target;
                if (op == 5) { require(stack_slots() < stack_capacity, "Command return stack overflow"); returns.push_back({false,at + 8,0}); }
                at = *target; continue;
            }
            unsigned length = 1;
            if(kind==DatCommandKind::ColorOverlay){
                switch(op){
                case 11:case 12:case 16:case 17:case 20:case 23:break;
                case 13:case 14:case 15:case 18:case 19:length=2;break;
                case 21:length=5;break;
                case 22:length=3;break;
                default:throw DatError("Color overlay opcode is outside its checked source schema");
                }
                if(op==15||op==19)require((word&0x3ffffff)!=0,"Color blend duration is zero");
            }else switch (op) {
            case 1: case 2: case 8: case 12: case 13: case 14: case 15:
            case 16: case 18: case 19: case 20: case 21: case 22: case 23:
            case 24: case 25: case 26: case 27: case 28: case 29: case 30:
            case 31: case 32: case 33: case 35: case 36: case 37: case 40:
            case 41: case 42: case 43: case 44: case 45: case 46: case 47:
            case 48: case 49: case 50: case 51: case 52: case 53: case 57: break;
            case 10: case 11: length = 5; break;
            case 17: case 34: case 54: case 55: length = 3; break;
            case 38: length = 7; break;
            case 56: length = 2; break;
            case 39: length = 4; break;
            case 58: length = 4; break;
            default: throw DatError("Command opcode is outside the checked action execution subset");
            }
            for (unsigned i = 1; i < length; ++i) {
                const uint32_t offset = at + 4 * i;
                require(!archive->has_relocation(offset), "Command numeric operand is relocated");
                require(!words.contains(offset) || operands.contains(offset), "Command operand overlaps an instruction");
                operands.insert(offset); words[offset] = archive->be32(offset);
                if(kind==DatCommandKind::ColorOverlay && i==1 && (op==13||op==14||op==15||op==18||op==19))
                    std::memcpy(&words[offset],archive->range(offset,4).data(),4);
            }
            at += 4 * length;
        }
        require(terminated, "Command graph has a cycle or exceeds the execution bound");
    }
    require(!words.empty() && words.size() <= 8192, "Command graph is empty or too large");
    std::vector<MeleeWebCommandWord> decoded;
    for (auto [offset, word] : words) { indices_[offset] = decoded.size(); decoded.push_back({word, UINT32_MAX}); }
    for (auto [offset, target] : branches) decoded.at(indices_.at(offset)).target = uint32_t(indices_.at(target));
    native_ = kind==DatCommandKind::ColorOverlay?color_commands(decoded):melee_web_commands_create(decoded.data(), decoded.size());
    require(native_ != nullptr, "Native command conversion failed");
}
DatCommands::~DatCommands() { if(kind_==DatCommandKind::ColorOverlay)std::free(native_);else melee_web_commands_destroy(native_); }
void* DatCommands::at(uint32_t offset) const
{
    const auto it = indices_.find(offset);
    require(it != indices_.end(), "Command root was not validated");
    return melee_web_commands_at(native_, it->second);
}
}
