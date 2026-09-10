#include "dat_effect_banks.hpp"
#include <stdexcept>
namespace melee_web {
DatEffectBanks::DatEffectBanks(std::shared_ptr<const DatArchive> archive,std::string_view symbol,uint32_t bank)
    :arena_(archive)
{
    std::optional<uint32_t> root;
    for(const auto& entry:archive->public_symbols())if(entry.name==symbol)root=entry.data_offset;
    if(!root)throw DatError("Exact effect data table symbol is absent");
    const auto commands=archive->pointer(*root,8),textures=archive->pointer(*root+4,4);
    if(!commands||!textures)throw DatError("Effect data table lacks particle command or texture bank");
    bank_=melee_web_effect_bank_decode(arena_.reader(),*root,
        archive->next_target_offset(*commands)-*commands,
        archive->next_target_offset(*textures)-*textures,bank);
}
DatEffectBanks::DatEffectBanks(std::shared_ptr<const DatArchive> archive,std::string_view command_symbol,
    std::string_view texture_symbol,uint32_t bank):arena_(archive)
{
    std::optional<uint32_t> commands,textures;
    for(const auto& entry:archive->public_symbols()) {
        if(entry.name==command_symbol)commands=entry.data_offset;
        if(entry.name==texture_symbol)textures=entry.data_offset;
    }
    if(!commands||!textures)throw DatError("Exact particle command/texture public symbols are absent");
    bank_=melee_web_effect_bank_decode_roots(arena_.reader(),*commands,*textures,
        archive->next_target_offset(*commands)-*commands,
        archive->next_target_offset(*textures)-*textures,bank);
}
DatEffectBanks::~DatEffectBanks()
{
    // Continuing would release native pointers still published to original
    // particle consumers. Explicit detach returns a diagnostic to the caller.
    for(auto [id,alias]:aliases_)
        if(!melee_web_effect_bank_detach(alias,nullptr,0))std::terminate();
    if(bank_&&!melee_web_effect_bank_detach(bank_,nullptr,0))std::terminate();
}
MeleeWebEffectBank* DatEffectBanks::bank()const noexcept{return bank_;}
MeleeWebEffectBank* DatEffectBanks::alias(uint32_t bank)
{
    for(auto [id,alias]:aliases_)if(id==bank)return alias;
    auto* alias=melee_web_effect_bank_alias(arena_.reader(),bank_,bank);
    aliases_.emplace_back(bank,alias);
    return alias;
}
}
