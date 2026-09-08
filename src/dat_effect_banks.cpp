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
DatEffectBanks::~DatEffectBanks()
{
    // Continuing would release native pointers still published to original
    // particle consumers. Explicit detach returns a diagnostic to the caller.
    if(bank_&&!melee_web_effect_bank_detach(bank_,nullptr,0))std::terminate();
}
MeleeWebEffectBank* DatEffectBanks::bank()const noexcept{return bank_;}
}
