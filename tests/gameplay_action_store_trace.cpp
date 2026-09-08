#include "gameplay_action_store.hpp"
#include "fighter_runtime_fixture.hpp"
#include <string>
#include <fstream>
#include <iostream>
using namespace fighter_runtime_test;
extern "C" {
Fighter* action_test_fighter(void);
void action_test_destroy(Fighter*);
int action_test_load(Fighter*, int, int);
int action_test_load_from(Fighter*, Fighter*, int);
void* action_test_identity(Fighter*);
const char* action_test_identity_symbol(Fighter*);
int action_test_identity_command_live(Fighter*);
int action_test_identity_command_executes(Fighter*);
int action_test_alias(Fighter*);
float action_test_frames(Fighter*);
int action_test_cleared(Fighter*);
int action_test_commands(void*, unsigned, int);
int action_test_rows(void*, void*, void*);
int action_test_movement_operands(void);
int action_test_jab_operands(void);
int action_test_common_operands(void);
}
namespace {
Bytes read_file(const char* path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate); check(bool(in), "Missing input");
    auto size = in.tellg(); check(size > 0 && size < 64 * 1024 * 1024, "Input size limit");
    Bytes b((size_t)size); in.seekg(0); check(bool(in.read((char*)b.data(), size)), "Truncated input"); return b;
}
void verify(std::shared_ptr<const DatArchive> archive, const Bytes& container, bool actual)
{
    GameplayActionStore store(archive, mario(), container);
    Fighter* fp = action_test_fighter(); check(fp != nullptr, "Fighter allocation"); store.bind(fp);
    check(action_test_load(fp, 2, 0) > 0 && action_test_load(fp, 6, 1) > 0 && action_test_alias(fp), "Original loader preserves 2/6 clip aliases");
    const auto frames = action_test_frames(fp); check(frames > 0, "Original FigaTree frames consumer");
    action_test_load(fp, 7, 1); action_test_load(fp, 8, 1);
    check(action_test_frames(fp) == frames, "Primary clip retains stream ownership across secondary cache eviction");
    action_test_load(fp, 6, 1); check(action_test_alias(fp), "Alias remains stable after CPU cache eviction");
    check(store.selected_motion(1) == 6 && store.selected_motion(0) == 2, "Exact motion identity survives alias sharing");
    check(action_test_rows(store.action_rows(), store.blend_rows(), store.wait_choices()), "Native rows preserve independent scripts and numeric flags");
    for (auto id : {2U,6U}) check(action_test_commands(store.action_rows(),id,actual), "Original Wait command executor trace");
    if (actual) {
        for(auto id : {7U,8U,9U,12U,13U,16U,18U,20U,35U,36U}) check(store.command_ready(id),"Explicit movement command readiness");
        for(auto id:{46U,47U,48U}) check(store.command_ready(id),"Explicit jab command readiness");
        for(auto id:{37U,41U,52U,60U,66U,72U,77U,177U,199U,221U,242U,247U,250U,253U}) check(store.command_ready(id),"Explicit normal action group readiness");
        for(auto id:{295U,296U,297U,298U,299U,300U,301U,302U}) check(store.command_ready(id),"Mario special script operands ready independently of Article services");
        check(!store.command_ready(138),"Unchecked item shoot script remains gated");
        check(!store.command_ready(49),"Unchecked rapid jab script remains gated");
        check(action_test_load(fp,20,0)>0, "Source Fall20 clip loads through original loader");
        check(action_test_commands(store.action_rows(),20,false), "Source Fall20 END command executes");
        check(action_test_commands(store.action_rows(),3,true), "Weighted alternate Wait source script");
        for (auto id : {35,7,8,9,37}) check(action_test_load(fp,id,1) > 0, "Fighter_Create startup clip loader");
    }
    store.unbind(); check(action_test_cleared(fp), "Unbind clears borrowed fighter pointers"); action_test_destroy(fp);
}
void verify_cross_fighter_identity_lease(std::shared_ptr<const DatArchive> archive, const Bytes& container)
{
    auto source = std::make_unique<GameplayActionStore>(archive, mario(), container);
    GameplayActionStore destination(archive, mario(), container);
    Fighter* thrower = action_test_fighter(); Fighter* victim = action_test_fighter();
    check(thrower && victim, "Cross-fighter test allocation"); source->bind(thrower); destination.bind(victim);
    check(action_test_load_from(victim, thrower, 2) > 0, "Cross-fighter source action load");
    const auto frames = action_test_frames(victim); const auto identity = action_test_identity(victim);
    const std::string symbol = action_test_identity_symbol(victim);
    check(frames > 0 && identity && !symbol.empty() && action_test_identity_command_live(victim),
          "Cross-fighter destination owns action identity and source command lease");
    source.reset();
    check(action_test_frames(victim) == frames && action_test_identity(victim) == identity &&
          action_test_identity_symbol(victim) && symbol == action_test_identity_symbol(victim) &&
          action_test_identity_command_live(victim) && action_test_identity_command_executes(victim),
          "Destination clip, source-row identity, symbol and command survive source teardown");
    destination.unbind(); check(action_test_cleared(victim), "Cross-fighter destination unbind clears pointers");
    action_test_destroy(thrower); action_test_destroy(victim);
}
}
int main(int argc, char** argv)
{
    try {
        check(action_test_movement_operands(), "Native movement operand ABI and truncation rejection");
        check(action_test_jab_operands(),"Native hitbox fields and canonical alias bounds/lifetime");
        check(action_test_common_operands(),"Common attack operands and original finite-loop execution");
        FighterFixture fixture;
        put32(fixture.data, fixture.command_a, 0xd0000003); fixture.unlink(fixture.command_a + 4);
        put32(fixture.data, fixture.command_b, 0);
        verify(std::make_shared<const DatArchive>(fixture.file()),fixture.container,false);
        verify_cross_fighter_identity_lease(std::make_shared<const DatArchive>(fixture.file()), fixture.container);
        // Fail before publication on unsupported opcodes, missing branch relocations,
        // return underflow and branch cycles; original stack has only three slots.
        for (uint32_t word : {0xfc000000U,0x14000000U,0x18000000U}) {
            auto broken = fixture; put32(broken.data,broken.command_a,word);
            rejects([&] { GameplayActionStore store(std::make_shared<const DatArchive>(broken.file()),mario(),broken.container); });
        }
        auto loop = fixture; put32(loop.data,loop.command_a,0x1c000000); loop.link(loop.command_a+4,loop.command_a);
        rejects([&] { GameplayActionStore store(std::make_shared<const DatArchive>(loop.file()),mario(),loop.container); });
        auto timed = fixture;
        put32(timed.data,timed.command_a,0x20000000); // original animation wait
        put32(timed.data,timed.command_a+4,0x1c000000);
        timed.link(timed.command_a+8,timed.command_a);
        const uint32_t timed_root=timed.command_a;
        DatCommands timed_commands(std::make_shared<const DatArchive>(timed.file()),std::span(&timed_root,1));
        check(timed_commands.word_count()==3,"Animation-yielding source loop preserves relocated goto");
        put32(timed.data,timed.command_a,0x08000001); // absolute wait alone becomes non-yielding
        rejects([&]{DatCommands bad(std::make_shared<const DatArchive>(timed.file()),std::span(&timed_root,1));});
        auto finite=fixture;
        const uint32_t finite_root=finite.data.size();finite.data.resize(finite_root+32);
        put32(finite.data,finite_root,0x0c000003);put32(finite.data,finite_root+4,0x04000001);
        put32(finite.data,finite_root+8,0x10000000);put32(finite.data,finite_root+12,0);
        DatCommands finite_commands(std::make_shared<const DatArchive>(finite.file()),std::span(&finite_root,1));
        check(finite_commands.word_count()==4,"Finite loop validates original two-slot stack semantics");
        for(uint32_t invalid:{0x0c000000U,0x10000000U,0x0c001001U}){
            put32(finite.data,finite_root,invalid);
            rejects([&]{DatCommands bad(std::make_shared<const DatArchive>(finite.file()),std::span(&finite_root,1));});
        }
        put32(finite.data,finite_root,0x0c000002);put32(finite.data,finite_root+4,0x0c000002);
        rejects([&]{DatCommands bad(std::make_shared<const DatArchive>(finite.file()),std::span(&finite_root,1));});
        // ColorOverlay uses six source stack words and byte-order-preserved RGBA.
        auto color=fixture;const uint32_t color_root=color.data.size();color.data.resize(color_root+40);
        put32(color.data,color_root,18U<<26);put32(color.data,color_root+4,0x12345678);
        put32(color.data,color_root+8,11U<<26|2U);put32(color.data,color_root+12,7U<<26);
        color.link(color_root+16,color_root);
        DatCommands color_commands(std::make_shared<const DatArchive>(color.file()),std::span(&color_root,1),DatCommandKind::ColorOverlay);
        auto* color_words=static_cast<uint32_t*>(color_commands.at(color_root));
        check(color_words[0]==18U<<26,"Color header retains numeric source opcode");
        const auto* rgba=reinterpret_cast<const unsigned char*>(&color_words[1]);
        check(rgba[0]==0x12&&rgba[1]==0x34&&rgba[2]==0x56&&rgba[3]==0x78,"Color operand retains RGBA byte order");
        check(color_words[4]==reinterpret_cast<uintptr_t>(color_words),"Color branch retains owned native identity");
        put32(color.data,color_root+8,11U<<26);
        rejects([&]{DatCommands bad(std::make_shared<const DatArchive>(color.file()),std::span(&color_root,1),DatCommandKind::ColorOverlay);});
        put32(color.data,color_root,19U<<26);
        rejects([&]{DatCommands bad(std::make_shared<const DatArchive>(color.file()),std::span(&color_root,1),DatCommandKind::ColorOverlay);});
        auto recursion = fixture; put32(recursion.data,recursion.command_a,0x14000000); recursion.link(recursion.command_a+4,recursion.command_a);
        rejects([&] { GameplayActionStore store(std::make_shared<const DatArchive>(recursion.file()),mario(),recursion.container); });
        auto operand = fixture; put32(operand.data,operand.command_a,0x1c000000); operand.link(operand.command_a+4,operand.command_a+4);
        rejects([&] { GameplayActionStore store(std::make_shared<const DatArchive>(operand.file()),mario(),operand.container); });
        std::cout << "Original owned action loaders and checked Wait command execution: passed\n";
        if (argc == 3) { verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),true);
            std::cout << "Local Mario Wait2/3/6 source command traces and startup clips: passed\n"; }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
