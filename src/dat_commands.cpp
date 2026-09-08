#include "dat_commands.hpp"
#include <set>
namespace melee_web {
namespace {
void require(bool v, const char* message) { if (!v) throw DatError(message); }
}
DatCommands::DatCommands(std::shared_ptr<const DatArchive> archive, std::span<const uint32_t> roots)
{
    require(bool(archive), "Command archive is null");
    std::map<uint32_t, uint32_t> words;
    std::map<uint32_t, uint32_t> branches;
    std::set<uint32_t> operands;
    for (uint32_t root : roots) {
        std::vector<uint32_t> returns;
        uint32_t at = root;
        bool terminated = false;
        for (unsigned steps = 0; steps < 4096; ++steps) {
            require(at % 4 == 0, "Command instruction is unaligned");
            require(!operands.contains(at), "Command branch enters an operand");
            require(!archive->has_relocation(at), "Command opcode is a relocated pointer");
            const uint32_t word = archive->be32(at), op = word >> 26;
            words[at] = word;
            if (op == 0) { terminated = true; break; }
            if (op == 6) {
                require(!returns.empty(), "Command return stack underflow");
                at = returns.back(); returns.pop_back(); continue;
            }
            if (op == 5 || op == 7) {
                require(archive->has_relocation(at + 4), "Command branch lacks a relocation");
                auto target = archive->pointer(at + 4, 4);
                require(target.has_value(), "Command branch target is null");
                require(!words.contains(at + 4) || operands.contains(at + 4), "Command operand overlaps an instruction");
                operands.insert(at + 4); words[at + 4] = archive->be32(at + 4); branches[at] = *target;
                if (op == 5) { require(returns.size() < 3, "Command return stack overflow"); returns.push_back(at + 8); }
                at = *target; continue;
            }
            require(op == 1 || op == 2 || op == 8 || op == 40 || op == 52,
                    "Command opcode is outside the checked Wait execution subset");
            at += 4;
        }
        require(terminated, "Command graph has a cycle or exceeds the execution bound");
    }
    require(!words.empty() && words.size() <= 8192, "Command graph is empty or too large");
    std::vector<MeleeWebCommandWord> decoded;
    for (auto [offset, word] : words) { indices_[offset] = decoded.size(); decoded.push_back({word, UINT32_MAX}); }
    for (auto [offset, target] : branches) decoded.at(indices_.at(offset)).target = uint32_t(indices_.at(target));
    native_ = melee_web_commands_create(decoded.data(), decoded.size());
    require(native_ != nullptr, "Native command conversion failed");
}
DatCommands::~DatCommands() { melee_web_commands_destroy(native_); }
void* DatCommands::at(uint32_t offset) const
{
    const auto it = indices_.find(offset);
    require(it != indices_.end(), "Command root was not validated");
    return melee_web_commands_at(native_, it->second);
}
}
