// A source-only rollback feasibility boundary. No draw traversal, GPU, browser
// clock, Web Audio sink or card commit is reachable in this diagnostic target.
// The host calls one synchronous function at a time and snapshots only after
// it returns; a snapshot is never taken inside the original scheduler.
#include "gameplay_match_session.hpp"
#include "gameplay_menu.h"
#include "gameplay_pad_state.h"
#include "gameplay_fighter_assets.h"
#include "gameplay_source_memory_runtime.h"
#include "gameplay_bootstrap.h"
#include "gameplay_source_files.h"
#include <emscripten.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
extern "C" {
#include <melee/gm/forward.h>
#include <melee/pl/forward.h>
#include <melee/ft/forward.h>
#include <melee/gr/forward.h>
#include <melee/lb/lblanguage.h>
#include <sysdolphin/baselib/random.h>
#include <melee/it/forward.h>
#include <dolphin/dvd.h>
// Exact source declaration from it_26B1.h; its complete C-only types are not
// portable C++ declarations. Keep the ItemKind type from the source header.
int it_8026B3C0(ItemKind);
}

namespace {
melee_web::RuntimeFiles files;
std::unique_ptr<melee_web::GameplayMatchSession> match;
bool attempted=false;
char error[256]{};
unsigned audio_phase=0;
std::array<float,1068> pcm{};
std::array<uint8_t,44> input_bytes{};
struct Observation {
    uint32_t frame, rng, ready, ending, complete, paused, outcome;
    int32_t winner;
    uint32_t pcm_frames, audio_phase;
    int32_t stocks[2], motion[2], fireballs;
    uint32_t objects, processes;
    MeleeWebMatchStats players[2];
    uint8_t pad_history[MELEE_WEB_PAD_STATE_BYTES];
} observation{};

void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class F> int checked(F function){
    error[0]='\0';
    try{function();return 1;}
    catch(const std::exception& failure){std::snprintf(error,sizeof(error),"%s",failure.what());return 0;}
}
int unavailable(char* text,size_t size){
    std::snprintf(text,size,"Menu scene execution is outside the snapshot fixture");return 0;
}
void observe(){
    std::memset(&observation,0,sizeof(observation));
    observation.frame=match->source_frames();observation.rng=match->random_seed();
    observation.ready=match->ready();observation.ending=match->ending();
    observation.complete=match->complete();observation.paused=match->paused();
    int winner=-1;observation.outcome=match->outcome(winner);observation.winner=winner;
    observation.audio_phase=audio_phase;
    for(unsigned slot=0;slot<2;slot++){
        observation.players[slot]=match->player_stats(slot);
        observation.stocks[slot]=observation.players[slot].stocks;
        observation.motion[slot]=observation.players[slot].motion_id;
    }
    observation.fireballs=it_8026B3C0(It_Kind_Mario_Fire);
    const auto world=melee_web_gameplay_stats();
    observation.objects=world.objects;observation.processes=world.processes;
    melee_web_pad_state_capture(observation.pad_history);
}
}

extern "C" {
EMSCRIPTEN_KEEPALIVE const char* melee_web_snapshot_error(){return error;}
EMSCRIPTEN_KEEPALIVE const char* melee_web_snapshot_source_identity(){return MELEE_WEB_SNAPSHOT_PROBE_SHA256;}
EMSCRIPTEN_KEEPALIVE int melee_web_snapshot_init(const char* root){
    if(attempted){std::snprintf(error,sizeof(error),"Snapshot fixture is one-shot; create a fresh module");return 0;}
    attempted=true;
    const int result=checked([&]{
    for(const auto& entry:std::filesystem::directory_iterator(root)){
        if(!entry.is_regular_file())continue;
        require(entry.file_size()<=64*1024*1024,"Snapshot fixture asset exceeds 64 MiB");
        std::ifstream stream(entry.path(),std::ios::binary);
        require(stream.good(),"Cannot read snapshot fixture asset");
        files[entry.path().filename().string()]={std::istreambuf_iterator<char>(stream),{}};
    }
    lbLang_SetLanguageSetting(LANG_US);lbLang_SetSavedLanguage(LANG_US);
    // Reuse the source menu default payload without entering a menu scene.
    // Unexpected scene callbacks fail; this target never substitutes success.
    MeleeWebMenuRuntime services{nullptr,
        [](void*,MeleeWebMenuScene,char* e,size_t n){return unavailable(e,n);},
        [](void*,char* e,size_t n){return unavailable(e,n);},
        [](void*,MeleeWebMenuScene,int*,char* e,size_t n){return unavailable(e,n);}};
    auto* menu=melee_web_menu_session_create(&services,nullptr,error,sizeof(error));
    require(menu,error);
    MeleeWebMenuMatchSelection selection{};
    selection.start=melee_web_menu_css(menu)->vs.start;
    require(melee_web_menu_session_destroy(menu,error,sizeof(error)),error);
    selection.start.rules.match_kind=MatchKind_Stock;
    selection.start.rules.is_stock=true;selection.start.rules.is_vs=true;
    selection.start.rules.xB=-1;selection.start.rules.stkind=St_Kind_Last;
    selection.start.rules.x0_3=2;
    selection.player_count=2;selection.hud_layout=2;selection.random_seed=0x13579bdf;
    for(unsigned slot=0;slot<GM_MAX_PLAYERS;slot++){
        auto& player=selection.start.players[slot];
        player.slot_type=slot<2?Gm_PKind_Human:Gm_PKind_NA;
        if(slot>=2)continue;
        player.ckind=CKIND_MARIO;player.color=slot;player.stocks=4;
        player.rumble_enabled=1;
        selection.players[slot]={slot,4,slot,0};
    }
    match=std::make_unique<melee_web::GameplayMatchSession>(files,selection);
    observe();
    });
    if(!result){match.reset();files.clear();}
    return result;
}

// PAD values are indexed by the caller's simulation sample, never host time.
// The caller retains this sample index outside restored Wasm state.
EMSCRIPTEN_KEEPALIVE int melee_web_snapshot_step(unsigned sample){return checked([&]{
    require(match&&!match->complete()&&!match->paused(),"Snapshot fixture is not an active match");
    PADStatus pads[4]{};pads[2].err=pads[3].err=PAD_ERR_NO_CONTROLLER;
    if(sample>=300){
        const unsigned phase=(sample-300)%120;
        pads[0].stickX=phase<60?80:-80;
        pads[1].stickX=phase<60?-80:80;
        if(phase==0||phase==40){
            pads[0].stickX=0;pads[0].button=PAD_BUTTON_B;
            pads[1].button=PAD_BUTTON_A;
        }
        if(phase==20){pads[0].button=PAD_BUTTON_X;pads[1].button=PAD_BUTTON_X;}
        if(phase>=80&&phase<90){pads[0].triggerLeft=140;pads[1].triggerRight=140;}
    }
    for(unsigned slot=0;slot<4;slot++){
        const auto& pad=pads[slot];auto* bytes=input_bytes.data()+11*slot;
        bytes[0]=pad.button>>8;bytes[1]=pad.button;
        bytes[2]=pad.stickX;bytes[3]=pad.stickY;bytes[4]=pad.substickX;bytes[5]=pad.substickY;
        bytes[6]=pad.triggerLeft;bytes[7]=pad.triggerRight;
        bytes[8]=pad.analogA;bytes[9]=pad.analogB;bytes[10]=pad.err;
    }
    match->tick(pads);
    audio_phase+=32000;const unsigned frames=audio_phase/60;audio_phase%=60;
    pcm.fill(0);
    require(melee_web_audio_render(match->audio(),pcm.data(),frames,error,sizeof(error)),error);
    require(melee_web_source_memory_healthy(),"Source memory owner became unhealthy");
    require(melee_web_fighter_assets_check_owned("snapshot-step",error,sizeof(error)),error);
    observe();observation.pcm_frames=frames;
});}
EMSCRIPTEN_KEEPALIVE int melee_web_snapshot_quiescent(){return checked([&]{
    require(match!=nullptr,"Snapshot fixture is closed");
    require(DVDGetDriveStatus()==DVD_STATE_END,"Snapshot boundary has pending source/HPS/SSM transfers");
});}
EMSCRIPTEN_KEEPALIVE const void* melee_web_snapshot_observation(){return &observation;}
EMSCRIPTEN_KEEPALIVE unsigned melee_web_snapshot_observation_size(){return sizeof(observation);}
EMSCRIPTEN_KEEPALIVE const float* melee_web_snapshot_pcm(){return pcm.data();}
EMSCRIPTEN_KEEPALIVE unsigned melee_web_snapshot_pcm_size(){return sizeof(pcm);}
EMSCRIPTEN_KEEPALIVE const uint8_t* melee_web_snapshot_input(){return input_bytes.data();}
EMSCRIPTEN_KEEPALIVE uint32_t* melee_web_snapshot_rng_address(){return seed_ptr;}
EMSCRIPTEN_KEEPALIVE int melee_web_snapshot_close(){return checked([&]{
    if(match){match->close();match.reset();}
    files.clear();
});}
}
