#include "dat_archive.hpp"
#include "dat_item_article.hpp"
#include "gameplay_article_data.h"
#include "gameplay_compat.h"
#include "native_dat.hpp"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/it/forward.h>
}
#pragma GCC diagnostic pop
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace melee_web;

namespace {
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

std::shared_ptr<const DatArchive> read_archive(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    check(bool(input), "Game & Watch archive cannot be opened");
    const auto size = input.tellg();
    check(size > 0 && size <= std::streamoff(DatArchive::max_archive_bytes),
          "Game & Watch archive exceeds the checked bound");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    check(bool(input.read(reinterpret_cast<char*>(bytes.data()), size)),
          "Game & Watch archive is truncated");
    return std::make_shared<const DatArchive>(std::move(bytes),
                                               DatExternalPolicy::ResolveNull);
}

std::uint32_t symbol(const DatArchive& archive, const char* name)
{
    for (const auto& entry : archive.public_symbols())
        if (entry.name == name) return entry.data_offset;
    throw std::runtime_error("Game & Watch fighter root is missing");
}

struct NativeVisibility {
    std::uint16_t x0, pad2;
    std::uint8_t* x4;
    std::uint16_t x8, padA;
    std::uint8_t* xC;
    std::int32_t x10, x14;
    std::uint8_t x18, pad19[3];
    float x1C, x20, x24, x28, x2C, x30, x34, x38;
};
static_assert(sizeof(void*) == 4 && sizeof(NativeVisibility) == 0x3C);

uint8_t reverse_source_bits(uint8_t value)
{
    value=static_cast<uint8_t>(((value&0x55U)<<1U)|((value&0xAAU)>>1U));
    value=static_cast<uint8_t>(((value&0x33U)<<2U)|((value&0xCCU)>>2U));
    return static_cast<uint8_t>((value<<4U)|(value>>4U));
}

void check_list(const DatArchive& archive, std::uint32_t source,
                std::uint32_t count_offset, std::uint32_t pointer_offset,
                std::uint16_t count, const std::uint8_t* actual)
{
    check(count == archive.be16(source + count_offset),
          "Game & Watch visibility count changed during hydration");
    const auto indices = archive.pointer(source + pointer_offset, count);
    check(indices.has_value(), "Game & Watch visibility list source is missing");
    const auto expected = archive.range(*indices, count);
    check(std::memcmp(actual, expected.data(), count) == 0,
          "Game & Watch visibility bone order changed during hydration");
}

void run(const std::shared_ptr<const DatArchive>& archive)
{
    static constexpr std::array<std::uint32_t, 10> kinds = {
        It_Kind_GameWatch_Greenhouse, It_Kind_GameWatch_Manhole,
        It_Kind_GameWatch_Fire, It_Kind_GameWatch_Parachute,
        It_Kind_GameWatch_Turtle, It_Kind_GameWatch_Breath,
        It_Kind_GameWatch_Judge, It_Kind_GameWatch_Panic,
        It_Kind_GameWatch_Chef, It_Kind_GameWatch_Rescue,
    };
    const auto before = std::vector<std::uint8_t>(archive->data().begin(),
                                                  archive->data().end());
    const auto fighter = symbol(*archive, "ftDataGamewatch");
    const auto table = archive->pointer(fighter + 0x48, 44);
    check(table.has_value(), "Game & Watch Article/visibility table is missing");
    NativeDatArena registration_arena(archive);
    std::uint32_t hydrated = 0;
    for (std::uint32_t slot = 0; slot < kinds.size(); ++slot) {
        const auto article_root = archive->pointer(*table + slot * 4, 24);
        check(article_root.has_value(), "Game & Watch Article root is missing");
        std::uint32_t unresolved = 0;
        void* article = melee_web_article_decode(registration_arena.reader(),
                                                  *article_root, &unresolved);
        check(article && (unresolved & ~((1U << 1) | (1U << 3) | (1U << 4))) == 0,
              "Game & Watch Article registration graph is malformed");
        DatItemArticle owner(archive, *article_root, kinds[slot], article);
        check(owner.state_count() != 0,
              "Game & Watch Article lost its source state rows");
        check(melee_web_article_unresolved(article) == 0,
              "Game & Watch Article graph did not become creation-ready");

        auto** article_fields = static_cast<void**>(article);
        auto** special_cell = static_cast<void**>(article_fields[1]);
        check(special_cell && special_cell[0],
              "Game & Watch Article visibility identity is missing");
        const auto* vars = static_cast<const NativeVisibility*>(special_cell[0]);
        const auto special_at = archive->pointer(*article_root + 4, 4);
        check(special_at.has_value(), "Game & Watch source visibility cell is missing");
        const auto source = archive->pointer(*special_at, 0x3C);
        check(source.has_value(), "Game & Watch source visibility descriptor is missing");
        check_list(*archive, *source, 0, 4, vars->x0, vars->x4);
        check_list(*archive, *source, 8, 12, vars->x8, vars->xC);
        check(vars->x10 == static_cast<std::int32_t>(
                  reinterpret_cast<std::uintptr_t>(vars)) &&
              vars->x14 == static_cast<std::int32_t>(archive->be32(*source + 0x14)) &&
              vars->x18 == reverse_source_bits(
                  archive->range(*source + 0x18, 1)[0]),
              "Game & Watch visibility self-pointer/scalars changed");
        const float actual[] = {vars->x1C, vars->x20, vars->x24, vars->x28,
                                vars->x2C, vars->x30, vars->x34, vars->x38};
        for (unsigned i = 0; i < 8; ++i) {
            const float expected = archive->f32(*source + 0x1C + i * 4);
            check(std::memcmp(&actual[i], &expected, sizeof(float)) == 0,
                  "Game & Watch visibility collision scalar changed");
        }
        ++hydrated;
    }
    check(hydrated == kinds.size(), "Not all ten Game & Watch Articles hydrated");
    check(std::equal(before.begin(), before.end(), archive->data().begin(),
                     archive->data().end()),
          "Game & Watch visibility hydration modified immutable DAT bytes");
}

void run_copy_pan(const std::shared_ptr<const DatArchive>& archive)
{
    const auto before=std::vector<uint8_t>(archive->data().begin(),
                                           archive->data().end());
    const auto copy_root=symbol(*archive,"ftDataKirbyCopyGamewatch");
    const auto article_root=archive->pointer(copy_root+0x24,24);
    check(article_root.has_value(),"Kirby copied Pan Article root is missing");
    NativeDatArena registration_arena(archive);
    uint32_t unresolved=0;
    void* article=melee_web_article_decode(registration_arena.reader(),
                                           *article_root,&unresolved);
    check(article&&!(unresolved&~((1U<<1)|(1U<<3)|(1U<<4))),
          "Kirby copied Pan Article registration graph is malformed");
    {
        DatItemArticle owner(archive,*article_root,
                             It_Kind_Kirby_GameWatchChefPan,article);
        check(owner.state_count()==0,
              "Kirby copied Pan invented serialized animation states");
        check(melee_web_article_unresolved(article)==0,
              "Kirby copied Pan Article graph did not become creation-ready");
        auto** fields=static_cast<void**>(article);
        auto** special=static_cast<void**>(fields[1]);
        check(special&&special[0],"Kirby copied Pan render attributes are missing");
        const auto special_at=archive->pointer(*article_root+4,4);
        check(special_at.has_value(),"Kirby copied Pan special cell is missing");
        const auto source=archive->pointer(*special_at,0x3C);
        check(source.has_value(),"Kirby copied Pan render descriptor is missing");
        const auto* vars=static_cast<const NativeVisibility*>(special[0]);
        check_list(*archive,*source,0,4,vars->x0,vars->x4);
        check_list(*archive,*source,8,12,vars->x8,vars->xC);
        check(vars->x10==static_cast<int32_t>(
                  reinterpret_cast<uintptr_t>(vars))&&
              vars->x14==static_cast<int32_t>(archive->be32(*source+0x14))&&
              vars->x18==reverse_source_bits(
                  archive->range(*source+0x18,1)[0]),
              "Kirby copied Pan source flags or identity changed");
        check((vars->x18&0x03U)==0,
              "Kirby copied Pan source enables unsupported collision overlay");
        const uint32_t offsets[]{0x1C,0x20,0x24,0x28,0x2C,0x30,0x34,0x38};
        const float* actual[]{&vars->x1C,&vars->x20,&vars->x24,&vars->x28,
                              &vars->x2C,&vars->x30,&vars->x34,&vars->x38};
        for(unsigned i=0;i<8;i++) {
            const auto at=*source+offsets[i];
            if(archive->has_relocation(at)) {
                const auto target=archive->pointer(at,0);
                check(target.has_value(),"Kirby copied Pan collision identity is null");
                const auto expected=reinterpret_cast<uintptr_t>(
                    archive->range(*target,0).data());
                uintptr_t observed=0;
                std::memcpy(&observed,actual[i],sizeof(observed));
                check(observed==expected,
                      "Kirby copied Pan relocation identity changed");
            } else {
                const uint32_t expected=archive->be32(at);
                check(std::memcmp(actual[i],&expected,sizeof(expected))==0,
                      "Kirby copied Pan collision scalar bits changed");
            }
        }
    }
    check(std::equal(before.begin(),before.end(),archive->data().begin(),
                     archive->data().end()),
          "Kirby copied Pan hydration modified immutable DAT bytes");
}

void run_copy_ice(const std::shared_ptr<const DatArchive>& archive)
{
    const auto before=std::vector<std::uint8_t>(archive->data().begin(),
                                                archive->data().end());
    const auto copy_root=symbol(*archive,"ftDataKirbyCopyPopo");
    const auto article_root=archive->pointer(copy_root+0x0C,24);
    check(article_root.has_value(),
          "Kirby copied Ice Article root at Popo hat_dynamics[0] is missing");
    NativeDatArena registration_arena(archive);
    std::uint32_t unresolved=0;
    void* article=melee_web_article_decode(registration_arena.reader(),
                                           *article_root,&unresolved);
    check(article&&!(unresolved&~((1U<<1)|(1U<<3)|(1U<<4))),
          "Kirby copied Ice Article registration graph is malformed");
    {
        DatItemArticle owner(archive,*article_root,
                             It_Kind_Kirby_IceClimberIce,article);
        check(owner.state_count()==1,
              "Kirby copied Ice lost the source-authored item state row");
        check(melee_web_article_unresolved(article)==0,
              "Kirby copied Ice Article graph did not become creation-ready");
        const auto special_at=archive->pointer(*article_root+4,0x34);
        check(special_at.has_value(),
              "Kirby copied Ice source attribute record is missing");
        const auto* special=static_cast<const std::uint8_t*>(
            static_cast<void**>(article)[1]);
        check(special,"Kirby copied Ice native attributes are missing");
        for(unsigned offset=0;offset<0x34;offset+=4){
            const auto expected=archive->be32(*special_at+offset);
            std::uint32_t actual=0;
            std::memcpy(&actual,special+offset,sizeof(actual));
            if(actual!=expected)
                throw std::runtime_error(
                    "Kirby copied Ice item attribute bits changed at +"+
                    std::to_string(offset));
        }
    }
    check(std::equal(before.begin(),before.end(),archive->data().begin(),
                     archive->data().end()),
          "Kirby copied Ice Article hydration modified immutable DAT bytes");
}

void run_copy_fox(const std::shared_ptr<const DatArchive>& archive)
{
    static constexpr std::pair<std::uint32_t, std::uint32_t> articles[]{
        {0x0C, It_Kind_Kirby_FoxLaser},
        {0x10, It_Kind_Kirby_FoxBlaster},
    };
    static constexpr std::uint32_t state_counts[]{2, 9};
    const auto before = std::vector<std::uint8_t>(archive->data().begin(),
                                                  archive->data().end());
    const auto copy_root = symbol(*archive, "ftDataKirbyCopyFox");
    NativeDatArena registration_arena(archive);
    for (std::size_t index = 0; index < std::size(articles); ++index) {
        const auto [field_offset, kind] = articles[index];
        const auto article_root = archive->pointer(copy_root + field_offset, 24);
        check(article_root.has_value(),
              "Kirby copied Fox Article root at its source hat_dynamics slot is missing");
        std::uint32_t unresolved = 0;
        void* registered = melee_web_article_decode(
            registration_arena.reader(), *article_root, &unresolved);
        check(registered &&
                  (unresolved & ~((1U << 1) | (1U << 3) | (1U << 4))) == 0,
              "Kirby copied Fox Article registration graph is malformed");
        DatItemArticle owner(archive, *article_root, kind, registered);
        check(owner.state_count() == state_counts[index],
              "Kirby copied Fox Article lost a source animation row");
        check(owner.model_joint_descriptor() != nullptr,
              "Kirby copied Fox Article model joint was not hydrated");
        check(melee_web_article_unresolved(registered) == 0,
              "Kirby copied Fox Article graph did not become creation-ready");
    }
    check(std::equal(before.begin(), before.end(), archive->data().begin(),
                     archive->data().end()),
          "Kirby copied Fox Article hydration modified immutable DAT bytes");
}

void inspect_copy_visibility(const std::shared_ptr<const DatArchive>& archive)
{
    const auto root=symbol(*archive,"ftDataKirbyCopyGamewatch");
    std::cout<<"Game & Watch copy root="<<root;
    for(unsigned offset=0;offset<=0x1c;offset+=4){
        const auto at=root+offset;
        const auto target=archive->has_relocation(at)?archive->pointer(at,0):
            std::optional<std::uint32_t>{};
        std::cout<<" +"<<std::hex<<offset<<"="<<archive->be32(at)
                 <<(archive->has_relocation(at)?"->":"")
                 <<(target?std::to_string(*target):"")<<std::dec;
    }
    std::cout<<std::endl;
    for(const auto& entry:archive->public_symbols())
        if(entry.data_offset>=root+0x1c&&entry.data_offset<=root+0x40)
            std::cout<<"  nearby symbol +"<<(entry.data_offset-root)<<" "
                     <<entry.name<<std::endl;
    const auto lookup=archive->pointer(root+0x18,8);
    check(lookup.has_value(),"Game & Watch Kirby-copy secondary visibility root is missing");
    std::cout<<"Game & Watch copy source visibility: lookup="<<*lookup<<std::endl;
    for(unsigned model=0;model<2;model++) {
        const auto row=*lookup+model*8;
        try { (void)archive->range(row,8); }
        catch(const DatError&) {
            std::cout<<"  row["<<model<<"] escapes archive extent"<<std::endl;
            break;
        }
        const auto word0=archive->be32(row);
        const auto word4=archive->be32(row+4);
        const auto pointer0=archive->has_relocation(row)?
            archive->pointer(row,4):std::optional<std::uint32_t>{};
        const auto pointer4=archive->has_relocation(row+4)?
            archive->pointer(row+4,4):std::optional<std::uint32_t>{};
        std::cout<<"  row["<<model<<"] word0="<<word0
                 <<" reloc0="<<archive->has_relocation(row)
                 <<" target0="<<(pointer0?std::to_string(*pointer0):"-")
                 <<" word4="<<word4
                 <<" reloc4="<<archive->has_relocation(row+4)
                 <<" target4="<<(pointer4?std::to_string(*pointer4):"-")
                 <<std::endl;
    }
}

void inspect_kirby_models(const std::shared_ptr<const DatArchive>& archive)
{
    const auto root=symbol(*archive,"ftDataKirby");
    const auto data=archive->pointer(root+8,8);
    check(data.has_value(),"Kirby ftData model descriptor is missing");
    std::cout<<"Kirby source root="<<root<<" x8="<<archive->be32(root+8)
             <<" reloc="<<archive->has_relocation(root+8)
             <<" target="<<*data<<" FtPartsDesc models="
             <<archive->be32(*data)<<std::endl;
}

void inspect_copy_root(const std::shared_ptr<const DatArchive>& archive,
                       const char* name)
{
    const auto root=symbol(*archive,name);
    std::cout<<name<<" root="<<root;
    for(unsigned offset=0;offset<=0x1c;offset+=4){
        const auto at=root+offset;
        const auto target=archive->has_relocation(at)?archive->pointer(at,0):
            std::optional<std::uint32_t>{};
        std::cout<<" +"<<std::hex<<offset<<"="<<archive->be32(at)
                 <<(archive->has_relocation(at)?"->":"")
                 <<(target?std::to_string(*target):"")<<std::dec;
    }
    std::cout<<std::endl;
}
}

int main(int argc, char** argv)
{
    try {
        check(argc == 6, "Expected PlGw.dat, Kirby Game & Watch/Popo/Fox copy archives, and PlKb.dat");
        run(read_archive(argv[1]));
        const auto copy_archive=read_archive(argv[2]);
        run_copy_pan(copy_archive);
        inspect_copy_visibility(copy_archive);
        inspect_kirby_models(read_archive(argv[3]));
        const auto popo_copy_archive=read_archive(argv[4]);
        inspect_copy_root(popo_copy_archive,"ftDataKirbyCopyPopo");
        run_copy_ice(popo_copy_archive);
        const auto fox_copy_archive=read_archive(argv[5]);
        inspect_copy_root(fox_copy_archive,"ftDataKirbyCopyFox");
        run_copy_fox(fox_copy_archive);
        std::cout << "All ten Game & Watch Article visibility descriptors hydrated, source-ordered, and torn down\n";
        std::cout << "Kirby copied Pan Article retains source lists, flags, relocation identities, and no invented animation rows\n";
        std::cout << "Kirby copied Ice Article hydrates its Popo-owned source attributes/state and tears down cleanly\n";
        std::cout << "Kirby copied Fox Laser/Blaster hydrate source models and 2/9 animation rows, then tear down cleanly\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
