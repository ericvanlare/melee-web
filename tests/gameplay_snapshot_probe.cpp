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
#include "gameplay_audio_bank_transport.h"
#include "runtime_archive_cache.hpp"
extern "C" {
#include <melee/gm/gm_1A45.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjplink.h>
}
#include <emscripten.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

extern "C" uint32_t slippi_rng_profile_reset_seed(uint32_t offset);
extern "C" uint32_t slippi_rng_profile_sync_seed(uint32_t frame, uint32_t offset);

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
std::unique_ptr<melee_web::RuntimeArchiveCache> archive_cache;
bool attempted=false;
char error[256]{};
unsigned audio_phase=0;
std::array<float,1068> pcm{};
std::array<uint8_t,44> input_bytes{};
constexpr uint32_t default_source_seed=0x13579bdf;
uint32_t configured_source_seed=default_source_seed;
constexpr uint32_t rng_profile_offset=0x00001234u;
constexpr uint32_t invalid_profile_frame=0xffffffffu;
bool rng_profile_enabled=false;
HSD_GObj* rng_profile_gobj=nullptr;
struct RngProfileObservation {
    uint32_t enabled, offset, callback_count, callback_frame;
    uint32_t callback_seed, source_global_frame;
    uint32_t constructor_requested_seed, constructor_initial_seed;
} rng_profile_observation{0,rng_profile_offset,0,invalid_profile_frame,0,0,0,0};
static_assert(sizeof(RngProfileObservation)==32,"RNG profile trace layout must stay eight u32 fields");
struct Observation {
    uint32_t frame, rng, ready, ending, complete, paused, outcome;
    int32_t winner;
    uint32_t pcm_frames, audio_phase;
    int32_t stocks[2], motion[2], fireballs;
    uint32_t objects, processes;
    MeleeWebMatchStats players[2];
    uint8_t pad_history[MELEE_WEB_PAD_STATE_BYTES];
} observation{};
/* Fixture-only transfer ownership sample.  This is deliberately separate
 * from Observation: the legacy 1216-byte observation ABI stays unchanged. */
struct TransferDiagnostic {
    uint32_t sample;
    int32_t before_tick_drive, before_tick_source, before_tick_bank;
    int32_t after_tick_drive, after_tick_source, after_tick_bank;
    int32_t after_audio_drive, after_audio_source, after_audio_bank;
} transfer_diagnostic{};

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
    if(rng_profile_enabled)rng_profile_observation.source_global_frame=gm_801A4BB8();
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
void canonicalize_input(const PADStatus pads[4]){
    for(unsigned slot=0;slot<4;slot++){
        const auto& pad=pads[slot];auto* bytes=input_bytes.data()+11*slot;
        bytes[0]=static_cast<uint8_t>(pad.button>>8);bytes[1]=static_cast<uint8_t>(pad.button);
        bytes[2]=static_cast<uint8_t>(pad.stickX);bytes[3]=static_cast<uint8_t>(pad.stickY);
        bytes[4]=static_cast<uint8_t>(pad.substickX);bytes[5]=static_cast<uint8_t>(pad.substickY);
        bytes[6]=pad.triggerLeft;bytes[7]=pad.triggerRight;
        bytes[8]=pad.analogA;bytes[9]=pad.analogB;bytes[10]=static_cast<uint8_t>(pad.err);
    }
}
void decode_input(PADStatus pads[4]){
    for(unsigned slot=0;slot<4;slot++){
        const auto* bytes=input_bytes.data()+11*slot;auto& pad=pads[slot];
        pad.button=static_cast<uint16_t>(uint16_t(bytes[0])<<8|bytes[1]);
        pad.stickX=static_cast<int8_t>(bytes[2]);pad.stickY=static_cast<int8_t>(bytes[3]);
        pad.substickX=static_cast<int8_t>(bytes[4]);pad.substickY=static_cast<int8_t>(bytes[5]);
        pad.triggerLeft=bytes[6];pad.triggerRight=bytes[7];
        pad.analogA=bytes[8];pad.analogB=bytes[9];pad.err=static_cast<int8_t>(bytes[10]);
    }
}
void capture_transfer_status(int32_t& drive,int32_t& source,int32_t& bank){
    /* Keep DVDGetDriveStatus' existing inactive-owner stop behavior.  The two
     * owner queries are read-only and do not pump or invoke callbacks. */
    source=melee_web_source_file_drive_busy();
    bank=melee_web_audio_bank_transport_busy();
    drive=DVDGetDriveStatus();
}
void rng_profile_proc(HSD_GObj*){
    require(rng_profile_enabled,"RNG profile callback ran while disabled");
    require(seed_ptr!=nullptr,"RNG profile lost the original seed_ptr");
    const uint32_t frame=gm_801A4BB8();
    if(rng_profile_observation.callback_count &&
       frame!=rng_profile_observation.callback_frame+1u)
        throw std::runtime_error("RNG profile callback frame is not monotonic");
    *seed_ptr=slippi_rng_profile_sync_seed(frame,rng_profile_offset);
    rng_profile_observation.callback_count++;
    rng_profile_observation.callback_frame=frame;
    rng_profile_observation.callback_seed=*seed_ptr;
}
void install_rng_profile(){
    require(rng_profile_enabled,"RNG profile was not enabled");
    require(seed_ptr!=nullptr,"RNG profile cannot reset a missing seed_ptr");
    // InitOnlinePlay.asm:133-136 writes the configured offset before creating
    // its GObj; keep that boundary before any deferred stage/fighter work.
    *seed_ptr=slippi_rng_profile_reset_seed(rng_profile_offset);
    rng_profile_gobj=GObj_Create(HSD_GOBJ_CLASS_FIGHTER,HSD_GOBJ_CLASS_ITEMLINK,0);
    require(rng_profile_gobj!=nullptr,"RNG profile GObj_Create failed");
    require(HSD_GObj_SetupProc(rng_profile_gobj,rng_profile_proc,0)!=nullptr,
            "RNG profile HSD_GObj_SetupProc failed");
    rng_profile_observation.enabled=1;
    rng_profile_observation.offset=rng_profile_offset;
}
void release_rng_profile_gobj(){
    if(!rng_profile_gobj)return;
    require(HSD_GObj_804D781C==nullptr && HSD_GObj_804D7814==nullptr,
            "Cannot release RNG profile GObj during a source callback");
    require(HSD_GObj_Entities!=nullptr,"RNG profile GObj entity lists are unavailable");
    bool found=false;unsigned found_link=0;
    for(unsigned link=0;link<=HSD_GObjLibInitData.p_link_max;link++){
        for(auto* object=((HSD_GObj**)HSD_GObj_Entities)[link];object;object=object->next){
            if(object!=rng_profile_gobj)continue;
            found=true;found_link=link;
            break;
        }
        if(found)break;
    }
    require(found,"RNG profile GObj ownership was lost");
    require(found_link==HSD_GOBJ_CLASS_ITEMLINK,
            "RNG profile GObj moved to an unexpected process link");
    require(rng_profile_gobj->classifier==HSD_GOBJ_CLASS_FIGHTER &&
            rng_profile_gobj->p_link==HSD_GOBJ_CLASS_ITEMLINK &&
            rng_profile_gobj->p_priority==0,
            "RNG profile GObj ownership metadata changed");
    HSD_GObjPLink_80390228(rng_profile_gobj);
    rng_profile_gobj=nullptr;
}
void step_native(const PADStatus pads[4]){
    transfer_diagnostic.sample=match->source_frames();
    capture_transfer_status(transfer_diagnostic.before_tick_drive,
        transfer_diagnostic.before_tick_source,transfer_diagnostic.before_tick_bank);
    const uint32_t callback_count_before=rng_profile_observation.callback_count;
    const uint32_t source_global_frame_before=rng_profile_enabled?gm_801A4BB8():0;
    match->tick(pads);
    if(rng_profile_enabled){
        require(rng_profile_observation.callback_count==callback_count_before+1u,
                "RNG profile callback was skipped or repeated");
        require(rng_profile_observation.callback_frame==source_global_frame_before,
                "RNG profile callback frame differs from the source pre-step clock");
    }
    capture_transfer_status(transfer_diagnostic.after_tick_drive,
        transfer_diagnostic.after_tick_source,transfer_diagnostic.after_tick_bank);
    audio_phase+=32000;const unsigned frames=audio_phase/60;audio_phase%=60;
    pcm.fill(0);
    require(melee_web_audio_render(match->audio(),pcm.data(),frames,error,sizeof(error)),error);
    capture_transfer_status(transfer_diagnostic.after_audio_drive,
        transfer_diagnostic.after_audio_source,transfer_diagnostic.after_audio_bank);
    require(melee_web_source_memory_healthy(),"Source memory owner became unhealthy");
    require(melee_web_fighter_assets_check_owned("snapshot-step",error,sizeof(error)),error);
    observe();observation.pcm_frames=frames;
}
}

extern "C" {
EMSCRIPTEN_KEEPALIVE const char* melee_web_snapshot_error(){return error;}
EMSCRIPTEN_KEEPALIVE const char* melee_web_snapshot_source_identity(){return MELEE_WEB_SNAPSHOT_PROBE_SHA256;}
EMSCRIPTEN_KEEPALIVE const char* melee_web_snapshot_rng_profile_identity(){return MELEE_WEB_SNAPSHOT_RNG_PROFILE_SHA256;}
EMSCRIPTEN_KEEPALIVE int melee_web_snapshot_configure_rng_profile(uint32_t offset){
    if(attempted){std::snprintf(error,sizeof(error),"RNG profile must be configured before one-shot init");return 0;}
    if(offset!=rng_profile_offset){std::snprintf(error,sizeof(error),"RNG profile offset must be exactly 0x1234");return 0;}
    rng_profile_enabled=true;rng_profile_observation={1,offset,0,invalid_profile_frame,0,0,0,0};error[0]='\0';return 1;
}
EMSCRIPTEN_KEEPALIVE int melee_web_snapshot_configure_seed(uint32_t seed){
    if(attempted){std::snprintf(error,sizeof(error),"Snapshot seed must be configured before one-shot init");return 0;}
    configured_source_seed=seed;error[0]='\0';return 1;
}
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
    selection.player_count=2;selection.hud_layout=2;selection.random_seed=configured_source_seed;
    for(unsigned slot=0;slot<GM_MAX_PLAYERS;slot++){
        auto& player=selection.start.players[slot];
        player.slot_type=slot<2?Gm_PKind_Human:Gm_PKind_NA;
        if(slot>=2)continue;
        player.ckind=CKIND_MARIO;player.color=slot;player.stocks=4;
        player.rumble_enabled=1;
        selection.players[slot]={slot,4,slot,0};
    }
    rng_profile_observation.constructor_requested_seed=configured_source_seed;
    if(rng_profile_enabled){
        archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
        match=std::make_unique<melee_web::GameplayMatchSession>(
            files,selection,*archive_cache,melee_web::GameplayMatchConstruction::Deferred);
        require(seed_ptr!=nullptr,"RNG profile match context did not publish seed_ptr");
        rng_profile_observation.constructor_initial_seed=*seed_ptr;
        install_rng_profile();
        while(!match->construction_complete())(void)match->advance_construction();
    }else{
        match=std::make_unique<melee_web::GameplayMatchSession>(files,selection);
    }
    observe();
    });
    if(!result){
        char init_error[sizeof(error)]{};
        std::snprintf(init_error,sizeof(init_error),"%s",error);
        try{
            release_rng_profile_gobj();
            std::snprintf(error,sizeof(error),"%s",init_error);
        }catch(const std::exception& cleanup){
            std::snprintf(error,sizeof(error),"%s; cleanup failed: %s",init_error,cleanup.what());
        }
        match.reset();archive_cache.reset();files.clear();
    }
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
    canonicalize_input(pads);step_native(pads);
});}
EMSCRIPTEN_KEEPALIVE int melee_web_snapshot_step_raw(){return checked([&]{
    require(match&&!match->complete()&&!match->paused(),"Snapshot fixture is not an active match");
    PADStatus pads[4]{};decode_input(pads);canonicalize_input(pads);step_native(pads);
});}
EMSCRIPTEN_KEEPALIVE int melee_web_snapshot_quiescent(){return checked([&]{
    require(match!=nullptr,"Snapshot fixture is closed");
    require(DVDGetDriveStatus()==DVD_STATE_END,"Snapshot boundary has pending source/HPS/SSM transfers");
});}
EMSCRIPTEN_KEEPALIVE const void* melee_web_snapshot_observation(){return &observation;}
EMSCRIPTEN_KEEPALIVE const void* melee_web_snapshot_rng_profile_observation(){return &rng_profile_observation;}
EMSCRIPTEN_KEEPALIVE unsigned melee_web_snapshot_rng_profile_observation_size(){return sizeof(rng_profile_observation);}
EMSCRIPTEN_KEEPALIVE unsigned melee_web_snapshot_observation_size(){return sizeof(observation);}
EMSCRIPTEN_KEEPALIVE const void* melee_web_snapshot_transfer_diagnostic(){return &transfer_diagnostic;}
EMSCRIPTEN_KEEPALIVE unsigned melee_web_snapshot_transfer_diagnostic_size(){return sizeof(transfer_diagnostic);}
EMSCRIPTEN_KEEPALIVE const float* melee_web_snapshot_pcm(){return pcm.data();}
EMSCRIPTEN_KEEPALIVE unsigned melee_web_snapshot_pcm_size(){return sizeof(pcm);}
EMSCRIPTEN_KEEPALIVE const uint8_t* melee_web_snapshot_input(){return input_bytes.data();}
EMSCRIPTEN_KEEPALIVE uint32_t* melee_web_snapshot_rng_address(){return seed_ptr;}
EMSCRIPTEN_KEEPALIVE int melee_web_snapshot_close(){return checked([&]{
    release_rng_profile_gobj();
    if(match){match->close();match.reset();}
    archive_cache.reset();
    files.clear();
});}
}
