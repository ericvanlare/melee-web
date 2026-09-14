#include "pipeline_provenance_runtime.h"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace {
MeleeWebPipelineRecorder* recorder = nullptr;
std::atomic<uint64_t> device_generation{0}, renderer_generation{0}, packet_id{0};
std::atomic<bool> capture_finished{false};
thread_local MeleeWebPipelineWorkContext current{};
std::mutex binding_mutex;
uint64_t coverage_case = 0;
std::array<uint8_t,32> input_digest{};
bool bound_input = false;
std::array<char,MELEE_WEB_PIPELINE_MAX_JSON_BYTES> output{};
size_t output_bytes = 0;

int nibble(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    return -1;
}
}
extern "C" {
void melee_web_provenance_initialize() {
    if(recorder)return;
    MeleeWebPipelineSourceContext boot{};
    for(auto& player:boot.players){player.motion_id=-1;player.stocks=-1;}
    boot.scene=MELEE_WEB_PIPELINE_SCENE_BOOT;
    boot.phase=MELEE_WEB_PIPELINE_PHASE_PREPARATION;
    boot.route_epoch=1;
    boot.owner_kind=MELEE_WEB_PIPELINE_OWNER_ROUTE_COMPOSITE;
    MeleeWebPipelineRecorderConfig config{};
    config.max_records=MELEE_WEB_PIPELINE_MAX_RECORDS;
    config.max_descriptors=MELEE_WEB_PIPELINE_MAX_DESCRIPTORS;
    config.max_scopes=MELEE_WEB_PIPELINE_MAX_SCOPES;
    config.max_json_bytes=MELEE_WEB_PIPELINE_MAX_JSON_BYTES;
    config.boot_context=boot;
    recorder=melee_web_pipeline_recorder_create(&config,nullptr,0);
    if(recorder)melee_web_pipeline_capture_begin(recorder,&boot,nullptr,0);
}
MeleeWebPipelineRecorder* melee_web_provenance_recorder(){return recorder;}
MeleeWebPipelineWorkContext melee_web_provenance_current(){return current;}
void melee_web_provenance_invalid(uint32_t reason) {
    if(capture_finished.load())return;
    melee_web_pipeline_invalidate(recorder,reason,"Private provenance integration rejected metadata",nullptr,0);
}
void melee_web_provenance_scope_begin(const MeleeWebPipelineSourceContext* supplied,MeleeWebPipelineScope* scope) {
    if(!supplied||!scope){melee_web_provenance_invalid(MELEE_WEB_PIPELINE_INVALID_MISSING_CONTEXT);return;}
    *scope={};
    scope->previous=current.source;
    if(capture_finished.load())return;
    auto context=*supplied;
    if(!context.coverage_case_id){
        std::lock_guard lock(binding_mutex);
        context.coverage_case_id=coverage_case;
        context.input_binding_sha256_present=bound_input;
        std::memcpy(context.input_binding_sha256,input_digest.data(),input_digest.size());
    }
    if(melee_web_pipeline_source_scope_begin(recorder,&context,&scope->token,nullptr,0))
        current.source=scope->token;
}
void melee_web_provenance_scope_end(MeleeWebPipelineScope* scope) {
    if(!scope){melee_web_provenance_invalid(MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE);return;}
    if(!capture_finished.load())
        melee_web_pipeline_source_scope_end(recorder,&scope->token,nullptr,0);
    current.source=scope->previous;
}
void melee_web_provenance_owner_begin(uint32_t classifier,uint32_t link,uint32_t pass,MeleeWebPipelineScope* scope) {
    auto context=current.source.context;
    context.gobj_classifier=classifier;context.gx_link=link;context.render_pass=pass;
    // These are source HSD_GOBJ_CLASS_* values, not a material/hash inference.
    // They describe a dispatch family, not an independently certified group.
    context.owner_kind=MELEE_WEB_PIPELINE_OWNER_ROUTE_COMPOSITE;
    context.owner_id=classifier;
    context.owner_effect_bank=UINT32_MAX;
    context.owner_explicit_unknown=1;
    if(classifier==4)context.owner_kind=MELEE_WEB_PIPELINE_OWNER_FIGHTER;
    else if(classifier==3||classifier==13)context.owner_kind=MELEE_WEB_PIPELINE_OWNER_STAGE;
    else if(classifier==8)context.owner_kind=MELEE_WEB_PIPELINE_OWNER_EFFECT;
    else if(context.scene==MELEE_WEB_PIPELINE_SCENE_MATCH&&(classifier==14||classifier==17))
        context.owner_kind=MELEE_WEB_PIPELINE_OWNER_HUD;
    else if(classifier==6||classifier==7)context.owner_kind=MELEE_WEB_PIPELINE_OWNER_ITEM;
    melee_web_provenance_scope_begin(&context,scope);
}
void melee_web_provenance_frame(uint64_t value){current.frame_id=value;}
uint64_t melee_web_provenance_next_packet(){return packet_id.fetch_add(1)+1;}
void melee_web_provenance_device_begin(){
    if(capture_finished.load())return;
    melee_web_pipeline_device_begin(recorder,device_generation.fetch_add(1)+1,nullptr,0);
}
void melee_web_provenance_device_end(){
    if(capture_finished.load())return;
    melee_web_pipeline_device_end(recorder,device_generation.load(),nullptr,0);
}
void melee_web_provenance_renderer_begin(){
    if(capture_finished.load())return;
    melee_web_pipeline_renderer_begin(recorder,renderer_generation.fetch_add(1)+1,nullptr,0);
}
void melee_web_provenance_renderer_end(){
    if(capture_finished.load())return;
    melee_web_pipeline_renderer_end(recorder,renderer_generation.load(),nullptr,0);
}
int melee_web_provenance_set_case(uint32_t id,const char* hash){
    MeleeWebPipelineStatus status{};
    if(capture_finished.load())return 0;
    if(!recorder||!hash||!id||!melee_web_pipeline_status(recorder,&status,nullptr,0)||
       status.open_scopes||status.active_execution_depth||std::strlen(hash)!=64){
        melee_web_provenance_invalid(MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST);return 0;
    }
    std::array<uint8_t,32> digest{};
    for(size_t i=0;i<32;++i){
        const int a=nibble(hash[i*2]),b=nibble(hash[i*2+1]);
        if(a<0||b<0){melee_web_provenance_invalid(MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST);return 0;}
        digest[i]=static_cast<uint8_t>((a<<4)|b);
    }
    std::lock_guard lock(binding_mutex);
    coverage_case=id;input_digest=digest;bound_input=true;return 1;
}
size_t melee_web_provenance_chunk_bytes(){return output_bytes;}
const char* melee_web_provenance_drain(){
    output_bytes=0;
    if(capture_finished.load())return nullptr;
    size_t written=0;
    if(!melee_web_pipeline_json_drain(recorder,output.data(),output.size(),&written,0,nullptr,0))return nullptr;
    output_bytes=written;
    return output.data();
}
const char* melee_web_provenance_finish(){
    size_t written=0;
    // A failed finalization remains evidence: drain its retained records with
    // the recorder's sticky invalid status instead of hiding the failure.
    if(!capture_finished.exchange(true))melee_web_pipeline_capture_end(recorder,nullptr,0);
    if(!melee_web_pipeline_json_drain(recorder,output.data(),output.size(),&written,1,nullptr,0))return nullptr;
    output_bytes=written;
    return output.data();
}
const char* melee_web_provenance_status(){
    size_t written=0;
    if(!melee_web_pipeline_json_status(recorder,output.data(),output.size(),&written,nullptr,0))return nullptr;
    output_bytes=written;
    return output.data();
}
}
namespace melee_web::provenance {
Execution::Execution(const MeleeWebPipelineWorkContext& work):previous_(current){
    pushed_=!capture_finished.load()&&
        melee_web_pipeline_execution_push(recorder,&work.source,&execution_,nullptr,0)!=0;
    current=work;
}
Execution::~Execution(){
    if(pushed_)melee_web_pipeline_execution_pop(recorder,&execution_,nullptr,0);
    current=previous_;
}
void observe(uint32_t kind,uint32_t type,uint64_t reference,uint32_t version,
             const void* descriptor,size_t size,uint32_t outcome,uint64_t packet){
    if(capture_finished.load())return;
    melee_web_pipeline_observe_at(recorder,kind,type,reference,version,descriptor,size,
                                 outcome,current.frame_id,packet,nullptr,0);
}
}
