#include "pipeline_provenance_runtime.h"
#include <cassert>
#include <cstring>
#include <string>

int main() {
    melee_web_provenance_initialize();
    assert(melee_web_provenance_set_case(1,
        "abababababababababababababababababababababababababababababababab"));
    melee_web_provenance_device_begin();
    melee_web_provenance_renderer_begin();
    MeleeWebPipelineSourceContext context{};
    context.scene=MELEE_WEB_PIPELINE_SCENE_BOOT;
    context.phase=MELEE_WEB_PIPELINE_PHASE_PREPARATION;
    context.route_epoch=1;
    context.owner_kind=MELEE_WEB_PIPELINE_OWNER_ROUTE_COMPOSITE;
    for(auto& player:context.players){player.motion_id=-1;player.stocks=-1;}
    const unsigned char descriptor[]={1,2,3,4};
    {
        const melee_web::provenance::Scope scope(context);
        melee_web::provenance::observe(MELEE_WEB_PIPELINE_EVENT_READY,1,1,1,
            descriptor,sizeof(descriptor),MELEE_WEB_PIPELINE_OUTCOME_READY);
    }
    const char* chunk=melee_web_provenance_drain();
    assert(chunk&&melee_web_provenance_chunk_bytes()==std::strlen(chunk));
    assert(melee_web_provenance_chunk_bytes()>100);
    const char* final=melee_web_provenance_finish();
    assert(final&&melee_web_provenance_chunk_bytes()==std::strlen(final));
    assert(final&&std::strstr(final,"\"valid\":true")&&std::strstr(final,"\"final\":true"));
    const std::string frozen_status=melee_web_provenance_status();
    // A completed capture has a declared end boundary. Subsequent idle frames
    // must neither append demand nor invalidate the already completed capture.
    {
        const melee_web::provenance::Scope later(context);
        const auto copied=melee_web_provenance_current();
        const melee_web::provenance::Execution execution(copied);
        melee_web::provenance::observe(MELEE_WEB_PIPELINE_EVENT_READY,1,1,1,
            descriptor,sizeof(descriptor),MELEE_WEB_PIPELINE_OUTCOME_READY);
    }
    melee_web_provenance_renderer_end();
    melee_web_provenance_device_end();
    assert(!melee_web_provenance_drain());
    assert(melee_web_provenance_chunk_bytes()==0);
    assert(frozen_status==melee_web_provenance_status());
    assert(!melee_web_provenance_set_case(2,
        "abababababababababababababababababababababababababababababababab"));
    assert(frozen_status==melee_web_provenance_status());
}
