#include "dat_fighter_runtime.hpp"
#include "fighter_binding.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using namespace melee_web;

static std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot open ") + path);
    return {std::istreambuf_iterator<char>(input), {}};
}
static uint32_t root(const DatArchive& archive, const char* name)
{
    for (const auto& symbol : archive.public_symbols())
        if (symbol.name == name) return symbol.data_offset;
    throw std::runtime_error(std::string("missing public symbol: ") + name);
}

int main(int argc, char** argv)
{
    try {
        if (argc != 11)
            throw std::runtime_error("expected Samus fighter, animation, effects, audio, results and five costumes");
        const FighterCostume* identity = nullptr;
        constexpr std::array<const char*, 5> models = {
            "PlySamus5K_Share_joint", "PlySamus5KPi_Share_joint",
            "PlySamus5KBk_Share_joint", "PlySamus5KGr_Share_joint",
            "PlySamus5KLa_Share_joint"};
        for (const auto& costume : fighter_costumes())
            if (costume.fighter_kind == 13 && costume.costume_index == 0) identity = &costume;
        if (!identity || identity->motion_count != 313 || identity->fighter_symbol != "ftDataSamus")
            throw std::runtime_error("generated Samus identity differs from GALE01r2 source metadata");

        auto archive = std::make_shared<const DatArchive>(read_file(argv[1]), DatExternalPolicy::ResolveNull);
        auto runtime = std::make_shared<const DatFighterRuntime>(archive, *identity);
        if (runtime->actions().size() != 313 || !runtime->samus_attributes())
            throw std::runtime_error("Samus actions or exact ftSs_DatAttrs are incomplete");
        if (runtime->samus_attributes()->xD0 != 0xB4)
            throw std::runtime_error("Samus's unrelocated xD0 source word was not retained exactly");

        const auto fighter = root(*archive, "ftDataSamus");
        const auto metal_root=archive->pointer(fighter+0x5c,64);
        if(metal_root) {
            std::vector<uint32_t> pending{*metal_root};std::set<uint32_t> seen;bool contains=false;
            while(!pending.empty()&&seen.size()<4096) {
                const auto current=pending.back();pending.pop_back();
                if(!seen.insert(current).second)continue;
                if(current==109352)contains=true;
                const auto flags=archive->be32(current+4);
                if(flags&0x1000)if(const auto target=archive->pointer(current+8,64))
                    std::cerr<<"SAMUS metal instance "<<current<<" -> "<<*target<<'\n';
                if(const auto next=archive->pointer(current+12,64))pending.push_back(*next);
                if(!(flags&0x1000))if(const auto child=archive->pointer(current+8,64))pending.push_back(*child);
            }
            std::cerr<<"SAMUS metal root="<<*metal_root<<" contains109352="<<contains<<" nodes="<<seen.size()<<'\n';
        }
        const auto attributes = archive->pointer(fighter + 4, 0xD4);
        if (!attributes || archive->has_relocation(*attributes + 0xD0))
            throw std::runtime_error("Samus source attribute root or xD0 relocation contract changed");
        const auto articles = archive->pointer(fighter + 0x48, 20);
        if (!articles) throw std::runtime_error("Samus five-entry x48 table is absent");
        for(unsigned ai=0;ai<4;++ai) {
            const auto article=archive->pointer(*articles+ai*4,24);
            const auto model=article?archive->pointer(*article+16,16):std::nullopt;
            const auto joint=model?archive->pointer(*model,64):std::nullopt;
            if(joint) {
                std::vector<uint32_t> pending{*joint};std::set<uint32_t> seen;bool contains=false;
                while(!pending.empty()&&seen.size()<4096) {
                    const auto current=pending.back();pending.pop_back();
                    if(!seen.insert(current).second)continue;
                    if(current==109352)contains=true;
                    const auto flags=archive->be32(current+4);
                    if(const auto next=archive->pointer(current+12,64))pending.push_back(*next);
                    if(!(flags&0x1000))if(const auto child=archive->pointer(current+8,64))pending.push_back(*child);
                }
                std::cerr<<"SAMUS article["<<ai<<"] model="<<*joint<<" contains109352="<<contains<<" nodes="<<seen.size()<<'\n';
            }
        }
        constexpr std::array<uint32_t, 4> special_sizes = {0x1C, 0x20, 0x40, 0xB0};
        constexpr std::array<uint32_t, 4> state_counts = {2, 9, 4, 0};
        for (unsigned i = 0; i < 4; ++i) {
            const auto article = archive->pointer(*articles + i * 4, 24);
            if (!article) throw std::runtime_error("Samus x48 Article root is null");
            const auto special = archive->pointer(*article + 4, special_sizes[i]);
            if (!special) throw std::runtime_error("Samus Article special record is null");
            (void)archive->range(*special, special_sizes[i]);
            const auto states = archive->pointer(*article + 12, state_counts[i] ? state_counts[i] * 16 : 1);
            if (state_counts[i] != 0) {
                if (!states) throw std::runtime_error("Samus serialized ItemState rows are absent");
                (void)archive->range(*states, state_counts[i] * 16);
            } else if (states) {
                throw std::runtime_error("Grapple Beam has an unexpected ItemState table");
            }
        }
        const auto grapple = archive->pointer(*articles + 16, 16);
        if (!grapple) throw std::runtime_error("Samus fifth x48 grapple descriptor is absent");
        const auto grapple_joint = archive->pointer(*grapple, 64);
        const auto throw_table = archive->pointer(*grapple + 4, 16);
        if (!grapple_joint || !throw_table || !archive->pointer(*grapple + 8, 20) ||
            !archive->pointer(*grapple + 12, 1))
            throw std::runtime_error("Samus grapple descriptor's joint or animations are incomplete");
        const auto grapple_instance=archive->pointer(*grapple_joint+8,64);
        {
            std::vector<uint32_t> pending{*grapple_joint};std::set<uint32_t> seen;bool contains=false;
            while(!pending.empty()&&seen.size()<4096) {
                const auto current=pending.back();pending.pop_back();
                if(!seen.insert(current).second)continue;
                if(current==109352)contains=true;
                const auto flags=archive->be32(current+4);
                std::cerr<<"SAMUS valid-path joint="<<current<<" flags="<<std::hex<<flags<<std::dec<<'\n';
                if(const auto next=archive->pointer(current+12,64))pending.push_back(*next);
                if(!(flags&0x1000))if(const auto child=archive->pointer(current+8,64))pending.push_back(*child);
            }
            std::cerr<<"SAMUS valid-path contains109352="<<contains<<" nodes="<<seen.size()<<'\n';
        }
        const auto beam_article=archive->pointer(*articles+3*4,24);
        const auto beam_model_desc=beam_article?archive->pointer(*beam_article+16,16):std::nullopt;
        const auto beam_model=beam_model_desc?archive->pointer(*beam_model_desc,64):std::nullopt;
        const auto beam_special=beam_article?archive->pointer(*beam_article+4,0xB0):std::nullopt;
        std::cerr << "SAMUS grapple root=" << *grapple_joint
                  << " first child=" << (grapple_instance?std::to_string(*grapple_instance):"null")
                  << " GBeam Article model=" << (beam_model?std::to_string(*beam_model):"null");
        if(beam_special)for(const auto offset:{0x64U,0x68U,0x6cU,0x70U}) {
            const auto joint=archive->pointer(*beam_special+offset,64);
            std::cerr << " attr" << std::hex << offset << '=' << (joint?*joint:UINT32_MAX) << std::dec;
            if(joint) {
                const auto first_child=archive->pointer(*joint+8,64);
                std::cerr << " flags=" << std::hex << archive->be32(*joint+4)
                          << " child=" << (first_child?*first_child:UINT32_MAX) << std::dec;
                std::vector<uint32_t> pending{*joint};std::set<uint32_t> seen;bool contains=false;
                while(!pending.empty()&&seen.size()<256) {
                    const auto current=pending.back();pending.pop_back();
                    if(!seen.insert(current).second)continue;
                    if(current==109352)contains=true;
                    if(archive->be32(current+4)&0x1000) {
                        const auto reference=archive->pointer(current+8,64);
                        std::cerr << " instanceRef=" << std::hex << (reference?*reference:UINT32_MAX) << std::dec;
                    }
                    if(const auto next=archive->pointer(current+12,64))pending.push_back(*next);
                    if(!(archive->be32(current+4)&0x1000))
                        if(const auto child=archive->pointer(current+8,64))pending.push_back(*child);
                }
                std::cerr << " contains109352=" << contains << " nodes=" << seen.size();
            }
        }
        if(beam_model) {
            const auto first_child=archive->pointer(*beam_model+8,64);
            std::cerr << " GBeam model flags=" << std::hex << archive->be32(*beam_model+4)
                      << " child=" << (first_child?*first_child:UINT32_MAX) << std::dec;
            std::vector<uint32_t> pending{*beam_model};std::set<uint32_t> seen;bool contains=false;
            while(!pending.empty()&&seen.size()<256) {
                const auto current=pending.back();pending.pop_back();
                if(!seen.insert(current).second)continue;
                if(current==109352)contains=true;
                if(archive->be32(current+4)&0x1000) {
                    const auto reference=archive->pointer(current+8,64);
                    std::cerr << " GBeamInstanceRef=" << std::hex << (reference?*reference:UINT32_MAX) << std::dec;
                }
                if(const auto next=archive->pointer(current+12,64))pending.push_back(*next);
                if(!(archive->be32(current+4)&0x1000))
                    if(const auto child=archive->pointer(current+8,64))pending.push_back(*child);
            }
            std::cerr << " contains109352=" << contains << " nodes=" << seen.size() << '\n';
        }
        std::cerr << '\n';
        for(const auto& symbol:archive->public_symbols())
            if(symbol.data_offset==*grapple_joint || (grapple_instance&&symbol.data_offset==*grapple_instance))
                std::cerr << "SAMUS symbol " << symbol.name << '=' << symbol.data_offset << '\n';
        if(grapple_instance)for(uint32_t slot=0;slot+4<=archive->data().size();slot+=4)
            if(archive->has_relocation(slot)) {
                const auto target=archive->pointer(slot,1);
                if(target&&*target==*grapple_instance)
                    std::cerr << "SAMUS instance target reference slot=" << slot << '\n';
            }
        {
            std::vector<uint32_t> pending{*grapple_joint};
            std::set<uint32_t> visited;
            while (!pending.empty() && visited.size() < 140) {
                const auto current = pending.back(); pending.pop_back();
                if (current % 4 || !visited.insert(current).second) {
                    std::cerr << "SAMUS grapple joint graph repeats/unaligns at " << current
                              << " visited=" << visited.size() << '\n';
                    break;
                }
                const auto sibling = archive->pointer(current + 12, 64);
                const auto child = archive->pointer(current + 8, 64);
                std::cerr << "SAMUS joint=" << current << " flags=" << archive->be32(current+4)
                          << " child=" << (child ? std::to_string(*child) : "-")
                          << " sibling=" << (sibling ? std::to_string(*sibling) : "-") << '\n';
                if (sibling) pending.push_back(*sibling);
                if (child) pending.push_back(*child);
            }
            std::cerr << "SAMUS grapple joint graph nodes=" << visited.size()
                      << " pending=" << pending.size() << '\n';
        }
        for (unsigned i = 0; i < 4; ++i)
            if (!archive->pointer(*throw_table + i * 4, 20))
                throw std::runtime_error("Samus grapple throw motion animation is absent");

        DatFighterAnimationStore actions(runtime, read_file(argv[2]));
        for (const auto motion : {0U, 44U, 295U, 299U, 301U, 303U, 306U, 312U}) {
            const auto selected = actions.select(motion);
            if (!selected.action.archive_bytes || !selected.animation)
                throw std::runtime_error("Samus source motion animation is missing: " + std::to_string(motion));
            if (selected.action.command_offset && !selected.commands)
                throw std::runtime_error("Samus source motion command stream is missing: " + std::to_string(motion));
        }

        const DatArchive effects(read_file(argv[3]), DatExternalPolicy::ResolveNull);
        const auto effect_root = root(effects, "effSamusDataTable");
        const auto effect_extent = effects.next_target_offset(effect_root) - effect_root;
        if (effect_extent < 8 + 4 * 20 || effect_extent >= 8 + 5 * 20)
            throw std::runtime_error("Samus source effect table does not bound four entries");
        if (read_file(argv[4]).empty()) throw std::runtime_error("Samus source audio bank is empty");
        if (read_file(argv[5]).empty()) throw std::runtime_error("Samus result-motion archive is empty");
        for (size_t i = 0; i < models.size(); ++i) {
            const FighterCostume* row = nullptr;
            for (const auto& costume : fighter_costumes())
                if (costume.fighter_kind == 13 && costume.costume_index == i) row = &costume;
            if (!row || row->model_symbol != models[i] || row->motion_count != 313)
                throw std::runtime_error("generated Samus costume identity differs from source metadata");
            const DatArchive model(read_file(argv[6 + i]), DatExternalPolicy::ResolveNull);
            (void)root(model, models[i]);
        }
        std::cout << "Samus GALE01r2 metadata, 313 actions, four Articles plus grapple throw joint/four animations, five costume roots, effect bank 2/count 4 and audio passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
