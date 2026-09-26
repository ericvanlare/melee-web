#include "gameplay_action_store.hpp"
#include "fighter_runtime_fixture.hpp"
#include <algorithm>
#include <string>
#include <fstream>
#include <iostream>
using namespace fighter_runtime_test;
extern "C" {
Fighter* action_test_fighter(void);
void action_test_destroy(Fighter*);
void action_test_install_table(Fighter*, void*, void*, unsigned);
void action_test_set_count(Fighter*, unsigned);
int action_test_load(Fighter*, int, int);
int action_test_load_from(Fighter*, Fighter*, int);
int action_test_auxiliary_out_of_range(Fighter*, int);
int action_test_primary_out_of_range(Fighter*, Fighter*, int);
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
int action_test_dobj_operands(void);
int action_test_opcode14_consumer(void);
int action_test_opcode15_consumer(void);
int action_test_opcode50_consumer(void);
int action_test_opcode51_consumer(void);
int action_test_opcode21_consumer(void);
int action_test_opcode36_consumer(void);
int action_test_opcode53_consumer(void);
int action_test_common_operands(void);
int action_test_falco_operands(void);
int action_test_wind_operands(void);
int action_test_common_appeals(void*, unsigned);
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
    for(unsigned id=78;id<=88;++id)check(store.command_ready(id),"Shared light-item pickup and throw command graphs are ready");
    for(unsigned id=96;id<=103;++id)check(store.command_ready(id),"Shared light-item smash throw command graphs are ready");
    for(unsigned id=267;id<=275;++id)check(store.command_ready(id),"Shared cargo victim command graphs are ready");
    Fighter* fp = action_test_fighter(); check(fp != nullptr, "Fighter allocation"); store.bind(fp);
    action_test_install_table(fp, store.action_rows(), store.blend_rows(),
                              static_cast<unsigned>(store.runtime().actions().size()));
    check(action_test_load(fp, 2, 0) > 0 && action_test_load(fp, 6, 1) > 0 && action_test_alias(fp), "Original loader preserves 2/6 clip aliases");
    action_test_set_count(fp, 14);
    check(action_test_auxiliary_out_of_range(fp, 72),
          "Demo auxiliary action 72 returns NULL and preserves its cached slot outside the 14-row source domain");
    check(action_test_primary_out_of_range(fp, fp, 72),
          "Demo primary action 72 is a source no-op outside the 14-row domain");
    action_test_set_count(fp, static_cast<unsigned>(store.runtime().actions().size()));
    const auto frames = action_test_frames(fp); check(frames > 0, "Original FigaTree frames consumer");
    action_test_load(fp, 7, 1); action_test_load(fp, 8, 1);
    check(action_test_frames(fp) == frames, "Primary clip retains stream ownership across secondary cache eviction");
    action_test_load(fp, 6, 1); check(action_test_alias(fp), "Alias remains stable after CPU cache eviction");
    check(store.selected_motion(1) == 6 && store.selected_motion(0) == 2, "Exact motion identity survives alias sharing");
    check(action_test_rows(store.action_rows(), store.blend_rows(), store.wait_choices()), "Native rows preserve independent scripts and numeric flags");
    for (auto id : {2U,6U}) check(action_test_commands(store.action_rows(),id,actual), "Original Wait command executor trace");
    if (actual) {
        for(auto id : {7U,8U,9U,12U,13U,16U,18U,20U,35U,36U}) check(store.command_ready(id),"Explicit movement command readiness");
        for(auto id:{46U,47U,48U,49U,50U,51U}) check(store.command_ready(id),"Explicit jab/dash/tilt command readiness");
        for(auto id:{37U,41U,52U,60U,66U,72U,77U,177U,199U,221U,242U,247U,250U,253U}) check(store.command_ready(id),"Explicit normal action group readiness");
        for(auto id:{295U,296U,297U,298U,299U,300U,301U,302U}) check(store.command_ready(id),"Mario special script operands ready independently of Article services");
        check(store.command_ready(238), "Original EntryStart command graph is ready");
        check(store.command_ready(277), "Yoshi Egg capture victim command graph is ready");
        check(action_test_load(fp,238,1)>0, "Original EntryStart clip loads");
        check(action_test_commands(store.action_rows(),238,false), "Original Mario EntryStart END executes");
        check(!store.command_ready(138),"Unchecked item shoot script remains gated");
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
    action_test_install_table(thrower, source->action_rows(), source->blend_rows(),
                              static_cast<unsigned>(source->runtime().actions().size()));
    action_test_install_table(victim, destination.action_rows(), destination.blend_rows(),
                              static_cast<unsigned>(destination.runtime().actions().size()));
    check(action_test_load_from(victim, thrower, 2) > 0, "Cross-fighter source action load");
    const auto frames = action_test_frames(victim); const auto identity = action_test_identity(victim);
    const std::string symbol = action_test_identity_symbol(victim);
    check(frames > 0 && identity && !symbol.empty() && action_test_identity_command_live(victim),
          "Cross-fighter destination owns action identity and source command lease");
    action_test_set_count(thrower, 1);
    check(action_test_primary_out_of_range(victim, thrower, 2),
          "Cross-fighter primary action guard uses the source fighter row count and preserves the destination lease");
    source.reset();
    check(action_test_frames(victim) == frames && action_test_identity(victim) == identity &&
          action_test_identity_symbol(victim) && symbol == action_test_identity_symbol(victim) &&
          action_test_identity_command_live(victim) && action_test_identity_command_executes(victim),
          "Destination clip, source-row identity, symbol and command survive source teardown");
    destination.unbind(); check(action_test_cleared(victim), "Cross-fighter destination unbind clears pointers");
    action_test_destroy(thrower); action_test_destroy(victim);
}

const FighterCostume& costume_for_archive(const DatArchive& archive)
{
    for (const auto& symbol : archive.public_symbols()) {
        for (const auto& costume : fighter_costumes()) {
            if (costume.costume_index == 0 && costume.fighter_symbol == symbol.name)
                return costume;
        }
    }
    throw std::runtime_error("Owned fighter archive has no canonical source costume");
}

void verify_common_appeals(std::shared_ptr<const DatArchive> archive, const Bytes& container,
                           const FighterCostume& costume)
{
    if (costume.fighter_kind == 1) {
        // Fox's source appeal row 239 contains HSD_A_J_NODE visibility tracks.
        // Selecting it through the native-action policy must succeed, while a
        // subsequent generic selection must parse independently and reject it.
        auto runtime = std::make_shared<const DatFighterRuntime>(archive, costume);
        DatFighterAnimationStore policy_store(runtime, container);
        const auto selected = policy_store.select_native_action(239);
        check(selected.animation && std::any_of(selected.animation->tracks.begin(), selected.animation->tracks.end(),
                                                [](const auto& track) { return track.type == 11; }),
              "Fox common appeal row 239 did not retain its source node channel");
        bool generic_rejected = false;
        try { (void) policy_store.select(239); }
        catch (const melee_web::DatError&) { generic_rejected = true; }
        check(generic_rejected, "generic and native-action animation cache policies were conflated");
    }
    if (costume.fighter_kind == 3) {
        auto runtime = std::make_shared<const DatFighterRuntime>(archive, costume);
        DatFighterAnimationStore policy_store(runtime, container);
        const auto wall = policy_store.select_native_action(0);
        check(wall.animation && std::any_of(wall.animation->tracks.begin(), wall.animation->tracks.end(),
                                           [](const auto& track) { return track.type == 12; }),
              "Donkey WallDamage must retain its original branch visibility channel");
        rejects([&] { (void) policy_store.select(0); });
    }
    GameplayActionStore store(std::move(archive), costume, container);
    // Koopa's side-special victim MS rows 278..287 map to the authored
    // submotion rows SM_None, SM 278..283, SM_None, SM 281..283. The action
    // store accepts submotion IDs, so only SM 278..283 require command graph
    // admission; the MS None rows intentionally have no command root.
    for (unsigned motion = 278; motion <= 283; ++motion) {
        check(store.command_ready(motion), "Koopa victim source submotion graph is not admitted");
        check(store.runtime().commands(motion).has_value() ==
                  store.runtime().action(motion).command_offset.has_value(),
              "Koopa victim source command presence changed");
    }
    if (costume.fighter_kind == 2 || costume.fighter_kind == 25) {
        // Captain/Ganondorf dive catches run the common CaptureCaptain
        // submotion row 276 on the catcher's own store.
        check(store.command_ready(276), "Captain-family dive-catch CaptureCaptain command graph is not admitted");
        check(store.runtime().commands(276).has_value() ==
                  store.runtime().action(276).command_offset.has_value(),
              "Captain-family dive-catch source command presence changed");
    }
    if (costume.fighter_kind == 5) {
        check(costume.motion_count == 316, "Koopa authored action count changed");
        for (unsigned motion = 295; motion < costume.motion_count; ++motion) {
            check(store.command_ready(motion), "Koopa self-motion graph is not admitted");
            check(store.runtime().commands(motion).has_value() ==
                      store.runtime().action(motion).command_offset.has_value(),
                  "Koopa self-motion source command presence changed");
        }
    }
    if (costume.fighter_kind == 8) {
        check(costume.motion_count == 326, "Ness authored action count changed");
        for (unsigned motion = 295; motion < costume.motion_count; ++motion) {
            check(store.command_ready(motion), "Ness self-motion graph is not admitted");
            check(store.runtime().commands(motion).has_value() ==
                      store.runtime().action(motion).command_offset.has_value(),
                  "Ness self-motion source command presence changed");
        }
        // Ness's own victim-side rows 259-261 and 266-285 are empty motions
        // (no authored clip bytes) whose single-word END scripts behave
        // exactly like Mario's identical table shape: the shared cargo
        // victim groups 267-275 and 278-283 are admitted, the remaining
        // rows keep the same non-admitted command treatment as every other
        // fighter and the animation identity comes from the thrower's
        // store, so a captured/shouldered Ness never reaches a divergent
        // state relative to the admitted Mario victim.
        for (unsigned motion : {259U, 260U, 261U, 266U, 267U, 268U, 269U, 270U,
                                271U, 272U, 273U, 274U, 275U, 276U, 277U, 278U,
                                279U, 280U, 281U, 282U, 283U, 284U, 285U}) {
            const auto& action = store.runtime().action(motion);
            check(!action.archive_bytes,
                  "Ness authored victim motion unexpectedly gained clip data");
        }
        for (unsigned motion = 267; motion <= 275; ++motion)
            check(store.command_ready(motion),
                  "Ness shared cargo victim rows are not admitted");
        for (unsigned motion = 278; motion <= 283; ++motion)
            check(store.command_ready(motion),
                  "Ness shared Koopa victim rows are not admitted");
        std::cout << "Ness kind 8 authored self-motion rows 295/325 and empty victim dispatch: passed\n";
    }
    if (costume.fighter_kind == 9) {
        check(costume.motion_count == 318, "Peach authored action count changed");
        for (unsigned motion = 295; motion < costume.motion_count; ++motion) {
            check(store.command_ready(motion), "Peach self-motion graph is not admitted");
            check(store.runtime().commands(motion).has_value() ==
                      store.runtime().action(motion).command_offset.has_value(),
                  "Peach self-motion source command presence changed");
        }
        // Peach's own victim-side rows 259-261 and 266-285 are empty motions
        // (no authored clip bytes), exactly like Ness's and Marth's identical
        // table shape: the shared cargo victim groups 267-275 and 278-283 are
        // admitted, the remaining rows keep the same non-admitted command
        // treatment as every other fighter and the animation identity comes
        // from the thrower's store, so a captured/shouldered Peach never
        // reaches a divergent state relative to the admitted Mario victim.
        for (unsigned motion : {259U, 260U, 261U, 266U, 267U, 268U, 269U, 270U,
                                271U, 272U, 273U, 274U, 275U, 276U, 277U, 278U,
                                279U, 280U, 281U, 282U, 283U, 284U, 285U}) {
            const auto& action = store.runtime().action(motion);
            check(!action.archive_bytes,
                  "Peach authored victim motion unexpectedly gained clip data");
        }
        for (unsigned motion = 267; motion <= 275; ++motion)
            check(store.command_ready(motion),
                  "Peach shared cargo victim rows are not admitted");
        for (unsigned motion = 278; motion <= 283; ++motion)
            check(store.command_ready(motion),
                  "Peach shared Koopa victim rows are not admitted");
        std::cout << "Peach kind 9 authored self-motion rows 295/317 and empty victim dispatch: passed\n";
    }
    if (costume.fighter_kind == 16) {
        check(costume.motion_count == 314, "Mewtwo authored action count changed");
        for (unsigned motion = 295; motion < costume.motion_count; ++motion) {
            check(store.command_ready(motion), "Mewtwo self-motion graph is not admitted");
            check(store.runtime().commands(motion).has_value() ==
                      store.runtime().action(motion).command_offset.has_value(),
                  "Mewtwo self-motion source command presence changed");
        }
    }
    unsigned expected_command_mask = 0;
    for (unsigned index = 0; index < 2; ++index) {
        const unsigned motion = 239 + index;
        const bool source_has_command = store.runtime().action(motion).command_offset.has_value();
        if (source_has_command) expected_command_mask |= 1U << index;
        check(store.command_ready(motion), "Common appeal action row is not admitted");
        check(store.runtime().commands(motion).has_value() == source_has_command,
              "Common appeal source command presence changed");
    }
    if (costume.fighter_kind == 17) {
        for (unsigned motion = 295; motion <= 311; ++motion) {
            check(store.command_ready(motion), "Luigi self-motion command graph is not admitted");
            check(store.runtime().commands(motion).has_value(),
                  "Luigi self-motion command root is not retained");
        }
    }
    if (costume.fighter_kind == 12 || costume.fighter_kind == 23) {
        check(costume.motion_count == 320, "Pikachu-family authored action count changed");
        for (unsigned motion = 295; motion < costume.motion_count; ++motion) {
            check(store.command_ready(motion), "Pikachu-family self-motion graph is not admitted");
            check(store.runtime().commands(motion).has_value() ==
                      store.runtime().action(motion).command_offset.has_value(),
                  "Pikachu-family self-motion source command presence changed");
        }
    }
    if (costume.fighter_kind == 3) {
        check(costume.motion_count == 337, "Donkey authored action count changed");
        for (unsigned motion = 295; motion < costume.motion_count; ++motion)
            check(store.command_ready(motion), "Donkey cargo/special command graph is not admitted");
        Fighter* fighter = action_test_fighter();
        check(fighter, "Donkey action fixture allocation failed");
        store.bind(fighter);
        for (int motion : {0, 24, 296, 297, 298})
            check(action_test_load(fighter, motion, 1) > 0,
                  "Donkey WallDamage/fall/OnLoad cargo walk clip is not hydrated");
        store.unbind(); action_test_destroy(fighter);
        std::cout << "Donkey branch visibility, fall and cargo OnLoad clips: passed\n";
    }
    check(action_test_common_appeals(store.action_rows(), expected_command_mask),
          "Common appeal rows did not retain checked command storage or were given the sentinel");
}

}
int main(int argc, char** argv)
{
    try {
        check(action_test_movement_operands(), "Native movement operand ABI and truncation rejection");
        check(action_test_jab_operands(),"Native hitbox fields and canonical alias bounds/lifetime");
        check(action_test_dobj_operands(),"Native DObj visibility command operands and admission");
        check(action_test_opcode14_consumer(),"Opcode 14 hitbox flag consumer and native admission");
        check(action_test_opcode15_consumer(),"Opcode 15 hitbox disable consumer and native admission");
        check(action_test_opcode50_consumer(),"Opcode 50 dynamics consumer and native admission");
        check(action_test_opcode51_consumer(),"Opcode 51 signed self-damage consumer and native admission");
        check(action_test_opcode21_consumer(),"Opcode 21 throw-flag consumer and native admission");
        check(action_test_opcode36_consumer(),"Opcode 36 source article-visibility consumer and native admission");
        check(action_test_opcode53_consumer(),"Opcode 53 source fighter-flag consumer and native admission");
        check(action_test_common_operands(),"Common attack operands and original finite-loop execution");
        check(action_test_falco_operands(),"Falco special opcode schemas retain source fields and canonical words");
        check(action_test_wind_operands(),"Marth wind command decodes source fields and reaches ftCo_8009E714");
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
        // Command_02 is a source-frame timer rather than a scheduler wait.
        // A loop that stops at frame 3 of a 20-frame clip is therefore not
        // bounded: after the animation advances past frame 3 it spins in one
        // source tick.  The Falco Run graph reaches frame 20 before jumping
        // back, and its source motion is explicitly marked looping.
        auto async=fixture;
        const uint32_t async_root=async.data.size(); async.data.resize(async_root+12);
        put32(async.data,async_root,0x08000003); // Command_02, absolute frame 3
        put32(async.data,async_root+4,0x1c000000); // Command_07, relocated goto
        async.link(async_root+8,async_root);
        const DatCommandRoot bounded_low{async_root,20.0f,1};
        rejects([&]{DatCommands bad(std::make_shared<const DatArchive>(async.file()),
                                     std::span(&bounded_low,1));});
        put32(async.data,async_root,0x08000014); // same graph reaches source frame 20
        const DatCommandRoot bounded_ok{async_root,20.0f,1};
        DatCommands bounded_commands(std::make_shared<const DatArchive>(async.file()),
                                     std::span(&bounded_ok,1));
        check(bounded_commands.word_count()==3,"Looping source timer reaches animation boundary");
        const DatCommandRoot non_looping{async_root,20.0f,0};
        rejects([&]{DatCommands bad(std::make_shared<const DatArchive>(async.file()),
                                     std::span(&non_looping,1));});
        auto finite=fixture;
        const uint32_t finite_root=finite.data.size();finite.data.resize(finite_root+32);
        put32(finite.data,finite_root,0x0c000003);put32(finite.data,finite_root+4,0x04000001);
        put32(finite.data,finite_root+8,0x10000000);put32(finite.data,finite_root+12,0);
        DatCommands finite_commands(std::make_shared<const DatArchive>(finite.file()),std::span(&finite_root,1));
        check(finite_commands.word_count()==4,"Finite loop validates original two-slot stack semantics");
        put32(finite.data,finite_root,0x0c001f40); // Kirby SpecialNLoop's authored 8000-count SetLoop.
        DatCommands long_finite_commands(std::make_shared<const DatArchive>(finite.file()),
                                         std::span(&finite_root,1));
        check(long_finite_commands.word_count()==4,
              "Long finite loop retains its four source words without unrolling 8000 iterations");
        for(uint32_t invalid:{0x0c000000U,0x10000000U}){
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
        if (argc > 1) {
            check(argc >= 3 && ((argc - 1) % 2) == 0, "Owned action archive arguments must be archive/container pairs");
            std::set<unsigned> kinds;
            for (int arg = 1; arg < argc; arg += 2) {
                auto archive = std::make_shared<const DatArchive>(read_file(argv[arg]));
                const auto container = read_file(argv[arg + 1]);
                const auto& costume = costume_for_archive(*archive);
                kinds.insert(costume.fighter_kind);
                verify_common_appeals(archive, container, costume);
                if (costume.fighter_kind == 0)
                    verify(archive, container, true);
            }
            if (kinds == std::set<unsigned>{0, 1, 18, 22})
                std::cout << "Owned common appeal action rows 239/240 for Mario, Fox, Falco and Marth: passed\n";
            else
                std::cout << "Owned common appeal action rows 239/240: passed\n";
            if (kinds.contains(0))
                std::cout << "Local Mario Wait2/3/6 source command traces and startup clips: passed\n";
            if (kinds.contains(17))
                std::cout << "Luigi kind 17 authored self-motion command rows 295/311: passed\n";
            for (unsigned kind : {12U, 23U})
                if (kinds.contains(kind))
                    std::cout << "Pikachu-family kind " << kind << " authored self-motion rows 295/319: passed\n";
            if (kinds.contains(16))
                std::cout << "Mewtwo kind 16 authored self-motion rows 295/313: passed\n";
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
