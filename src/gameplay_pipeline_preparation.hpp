#pragma once

#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
#include <aurora/pipeline_prepare.h>
#include "pipeline_preparation.generated.hpp"
#include "gameplay_menu_host.h"
#include <cstring>
#include <stdexcept>
#include <vector>

namespace melee_web::pipeline_preparation {
inline AuroraPipelinePrepareStatus status() {
    AuroraPipelinePrepareStatus state{};
    if (!aurora_pipeline_prepare_status(&state) || !state.configured || !state.valid ||
        state.unknown_count || state.error_count ||
        state.state == AURORA_PIPELINE_PREPARE_ENABLED_NO_REQUEST) {
        throw std::runtime_error("Pipeline preparation failed: unverified or unknown descriptor membership");
    }
    return state;
}

inline void request(const MeleeWebPipelinePreparationDescriptor* source, size_t count) {
    if (!source || !count || count > 1024)
        throw std::runtime_error("Pipeline preparation requires a nonempty certified descriptor union");
    std::vector<AuroraPipelineDescriptor> descriptors(count);
    const auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a') + 10;
        throw std::runtime_error("Pipeline preparation descriptor digest is malformed");
    };
    for (size_t i = 0; i < count; ++i) {
        const auto& input = source[i];
        if (!input.sha256 || std::strlen(input.sha256) != 64)
            throw std::runtime_error("Pipeline preparation descriptor digest is missing");
        auto& output = descriptors[i];
        output.type = input.type;
        output.pipeline_ref = input.pipeline_ref;
        output.config_version = input.config_version;
        output.config_size = input.size;
        for (size_t byte = 0; byte < 32; ++byte)
            output.sha256[byte] = static_cast<uint8_t>(nibble(input.sha256[2 * byte]) * 16 +
                                                       nibble(input.sha256[2 * byte + 1]));
    }
    if (!aurora_prepare_pipeline_union(descriptors.data(), descriptors.size()))
        throw std::runtime_error("Pipeline preparation rejected a missing or incompatible descriptor union");
    (void)status();
}

inline void bootstrap() {
    request(melee_web_pipeline_preparation_union,
            melee_web_pipeline_preparation_unionCount);
}
// One conservative union stays active for this device generation. Scene
// transitions rebuild source owners, while Aurora retains prepared GPU handles.
inline void css() { (void)status(); }
inline void sss() { (void)status(); }
inline void match(const MeleeWebMenuMatchSelection&) { (void)status(); }
} // namespace melee_web::pipeline_preparation
#endif
