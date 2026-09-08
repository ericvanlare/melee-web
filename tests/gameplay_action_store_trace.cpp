#include "gameplay_action_store.hpp"
#include "fighter_runtime_fixture.hpp"
#include <fstream>
#include <iostream>
using namespace fighter_runtime_test;
extern "C" {
Fighter* action_test_fighter(void);
void action_test_destroy(Fighter*);
int action_test_load(Fighter*, int, int);
int action_test_alias(Fighter*);
float action_test_frames(Fighter*);
int action_test_cleared(Fighter*);
int action_test_commands(void*, unsigned, int);
int action_test_rows(void*, void*, void*);
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
        check(action_test_load(fp,20,0)>0, "Source Fall20 clip loads through original loader");
        check(action_test_commands(store.action_rows(),20,false), "Source Fall20 END command executes");
        check(action_test_commands(store.action_rows(),3,true), "Weighted alternate Wait source script");
        for (auto id : {35,7,8,9,37}) check(action_test_load(fp,id,1) > 0, "Fighter_Create startup clip loader");
    }
    store.unbind(); check(action_test_cleared(fp), "Unbind clears borrowed fighter pointers"); action_test_destroy(fp);
}
}
int main(int argc, char** argv)
{
    try {
        FighterFixture fixture;
        put32(fixture.data, fixture.command_a, 0xd0000003); fixture.unlink(fixture.command_a + 4);
        put32(fixture.data, fixture.command_b, 0);
        verify(std::make_shared<const DatArchive>(fixture.file()),fixture.container,false);
        // Fail before publication on unsupported opcodes, missing branch relocations,
        // return underflow and branch cycles; original stack has only three slots.
        for (uint32_t word : {0xfc000000U,0x14000000U,0x18000000U}) {
            auto broken = fixture; put32(broken.data,broken.command_a,word);
            rejects([&] { GameplayActionStore store(std::make_shared<const DatArchive>(broken.file()),mario(),broken.container); });
        }
        auto loop = fixture; put32(loop.data,loop.command_a,0x1c000000); loop.link(loop.command_a+4,loop.command_a);
        rejects([&] { GameplayActionStore store(std::make_shared<const DatArchive>(loop.file()),mario(),loop.container); });
        auto recursion = fixture; put32(recursion.data,recursion.command_a,0x14000000); recursion.link(recursion.command_a+4,recursion.command_a);
        rejects([&] { GameplayActionStore store(std::make_shared<const DatArchive>(recursion.file()),mario(),recursion.container); });
        auto operand = fixture; put32(operand.data,operand.command_a,0x1c000000); operand.link(operand.command_a+4,operand.command_a+4);
        rejects([&] { GameplayActionStore store(std::make_shared<const DatArchive>(operand.file()),mario(),operand.container); });
        std::cout << "Original owned action loaders and checked Wait command execution: passed\n";
        if (argc == 3) { verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),true);
            std::cout << "Local Mario Wait2/3/6 source command traces and startup clips: passed\n"; }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
