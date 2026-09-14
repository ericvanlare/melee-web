#include "pipeline_provenance.h"

#if defined(MELEE_WEB_PUBLIC_RUNTIME)
#error "pipeline provenance is private and cannot be compiled into the public runtime"
#endif

#if defined(MELEE_WEB_PIPELINE_PROVENANCE)

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr unsigned kMaxThreads = 16;
constexpr uint32_t kDefaultRecords = MELEE_WEB_PIPELINE_MAX_RECORDS;
constexpr uint32_t kDefaultDescriptors = MELEE_WEB_PIPELINE_MAX_DESCRIPTORS;
constexpr uint32_t kDefaultScopes = MELEE_WEB_PIPELINE_MAX_SCOPES;
constexpr uint32_t kDefaultJsonBytes = MELEE_WEB_PIPELINE_MAX_JSON_BYTES;

int write_error(char* output, size_t capacity, const char* message) {
    if (output && capacity) {
        const size_t length = std::min(capacity - 1, std::strlen(message));
        std::memcpy(output, message, length);
        output[length] = 0;
    }
    return 0;
}

int clear_error(char* output, size_t capacity) {
    if (output && capacity) output[0] = 0;
    return 1;
}

const char* reason_name(uint32_t reason) {
    switch (reason) {
    case MELEE_WEB_PIPELINE_INVALID_NONE: return "none";
    case MELEE_WEB_PIPELINE_INVALID_NOT_STARTED: return "not_started";
    case MELEE_WEB_PIPELINE_INVALID_MISSING_CONTEXT: return "missing_context";
    case MELEE_WEB_PIPELINE_INVALID_INVALID_CONTEXT: return "invalid_context";
    case MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST: return "invalid_request";
    case MELEE_WEB_PIPELINE_INVALID_OVERFLOW: return "overflow";
    case MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW: return "descriptor_overflow";
    case MELEE_WEB_PIPELINE_INVALID_STALE_CAPTURE: return "stale_capture";
    case MELEE_WEB_PIPELINE_INVALID_STALE_DEVICE: return "stale_device";
    case MELEE_WEB_PIPELINE_INVALID_STALE_RENDERER: return "stale_renderer";
    case MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE: return "unpaired_scope";
    case MELEE_WEB_PIPELINE_INVALID_LIFECYCLE: return "lifecycle";
    case MELEE_WEB_PIPELINE_INVALID_JSON_OVERFLOW: return "json_overflow";
    case MELEE_WEB_PIPELINE_INVALID_OUTPUT_TOO_SMALL: return "output_too_small";
    case MELEE_WEB_PIPELINE_INVALID_THREAD_OVERFLOW: return "thread_overflow";
    default: return "unknown";
    }
}

bool valid_scene(uint32_t scene) {
    return scene >= MELEE_WEB_PIPELINE_SCENE_BOOT &&
           scene <= MELEE_WEB_PIPELINE_SCENE_RETURN;
}

bool valid_phase(uint32_t phase) {
    return phase >= MELEE_WEB_PIPELINE_PHASE_PREPARATION &&
           phase <= MELEE_WEB_PIPELINE_PHASE_RETURN;
}

bool valid_owner(uint32_t owner) {
    return owner <= MELEE_WEB_PIPELINE_OWNER_TEARDOWN;
}

bool context_valid(const MeleeWebPipelineSourceContext& context) {
    if (!valid_scene(context.scene) || !valid_phase(context.phase) ||
        context.active_player_count > 4 || !valid_owner(context.owner_kind) ||
        context.route_epoch == 0) {
        return false;
    }
    if (context.owner_kind == MELEE_WEB_PIPELINE_OWNER_UNKNOWN &&
        !context.owner_explicit_unknown) {
        return false;
    }
    /* Before world construction, only an explicitly preparation-phase BOOT,
     * CSS or MATCH context may carry world zero.  Never manufacture a world
     * generation for an unloaded route. */
    if (context.world_generation == 0 &&
        (context.phase != MELEE_WEB_PIPELINE_PHASE_PREPARATION ||
         (context.scene != MELEE_WEB_PIPELINE_SCENE_BOOT &&
          context.scene != MELEE_WEB_PIPELINE_SCENE_CSS &&
          context.scene != MELEE_WEB_PIPELINE_SCENE_MATCH))) {
        return false;
    }
    for (unsigned i = context.active_player_count; i != 4; ++i) {
        if (context.players[i].motion_id != -1 || context.players[i].stocks != -1) {
            return false;
        }
    }
    return true;
}

bool context_equal(const MeleeWebPipelineSourceContext& a,
                   const MeleeWebPipelineSourceContext& b) {
    if (a.scene != b.scene || a.phase != b.phase ||
        a.world_generation != b.world_generation || a.route_epoch != b.route_epoch ||
        a.source_tick != b.source_tick || a.coverage_case_id != b.coverage_case_id ||
        a.input_binding_sha256_present != b.input_binding_sha256_present ||
        a.stage != b.stage || a.ground != b.ground || a.hud_layout != b.hud_layout ||
        a.gobj_classifier != b.gobj_classifier || a.gx_link != b.gx_link ||
        a.render_pass != b.render_pass ||
        a.active_player_count != b.active_player_count ||
        a.owner_kind != b.owner_kind || a.owner_id != b.owner_id ||
        a.owner_effect_bank != b.owner_effect_bank ||
        a.owner_explicit_unknown != b.owner_explicit_unknown) {
        return false;
    }
    if (a.input_binding_sha256_present &&
        std::memcmp(a.input_binding_sha256, b.input_binding_sha256, 32) != 0) {
        return false;
    }
    for (unsigned i = 0; i != 4; ++i) {
        const auto& x = a.players[i];
        const auto& y = b.players[i];
        if (x.character != y.character || x.fighter_kind != y.fighter_kind ||
            x.costume != y.costume || x.subcolor != y.subcolor ||
            x.effect_bank != y.effect_bank || x.motion_id != y.motion_id ||
            x.stocks != y.stocks) {
            return false;
        }
    }
    return true;
}

uint64_t token_nonce(uint64_t capture_generation, uint64_t scope_id,
                    const MeleeWebPipelineSourceContext& context,
                    uint64_t device_generation, uint64_t renderer_generation,
                    uint64_t frame_id, uint64_t packet_id) {
    /* A deterministic nonce means a closed token can be checked without
     * retaining an unbounded history of closed scopes. */
    uint64_t x = capture_generation ^ (scope_id + UINT64_C(0x9e3779b97f4a7c15));
    x ^= device_generation + UINT64_C(0x632be59bd9b4e019);
    x ^= renderer_generation + UINT64_C(0x8cb92baa5f4a5f37);
    x ^= frame_id + UINT64_C(0x517cc1b727220a95);
    x ^= packet_id + UINT64_C(0x6eed0e9da4d94a4f);
    // Authenticate semantic fields in a fixed order. Struct padding and
    // reserved bytes are not source metadata and may differ after a POD copy.
    const auto mix = [&x](uint64_t value) {
        for(unsigned i=0;i<8;++i){
            x ^= static_cast<uint8_t>(value >> (i*8));
            x *= UINT64_C(0x100000001b3);
        }
    };
    mix(context.scene);
    mix(context.phase);
    mix(context.world_generation);
    mix(context.route_epoch);
    mix(context.source_tick);
    mix(context.coverage_case_id);
    mix(context.input_binding_sha256_present);
    mix(context.stage);
    mix(context.ground);
    mix(context.hud_layout);
    mix(context.gobj_classifier);
    mix(context.gx_link);
    mix(context.render_pass);
    mix(context.active_player_count);
    mix(context.owner_kind);
    mix(context.owner_id);
    mix(context.owner_effect_bank);
    mix(context.owner_explicit_unknown);
    if(context.input_binding_sha256_present)
        for(const auto byte:context.input_binding_sha256)mix(byte);
    for(const auto& player:context.players){
        mix(player.character);mix(player.fighter_kind);mix(player.costume);
        mix(player.subcolor);mix(player.effect_bank);
        mix(static_cast<uint64_t>(player.motion_id));
        mix(static_cast<uint64_t>(player.stocks));
    }
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x ? x : UINT64_C(1);
}

class Sha256 {
public:
    Sha256() {
        state_ = {UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
                  UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
                  UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
                  UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)};
    }

    void update(const uint8_t* bytes, size_t length) {
        while (length) {
            const size_t take = std::min(length, sizeof(buffer_) - buffered_);
            std::memcpy(buffer_.data() + buffered_, bytes, take);
            buffered_ += take;
            bytes += take;
            length -= take;
            bit_count_ += static_cast<uint64_t>(take) * 8;
            if (buffered_ == sizeof(buffer_)) {
                compress(buffer_.data());
                buffered_ = 0;
            }
        }
    }

    std::array<uint8_t, 32> finish() {
        const uint64_t original_bits = bit_count_;
        buffer_[buffered_++] = 0x80;
        if (buffered_ > 56) {
            while (buffered_ < 64) buffer_[buffered_++] = 0;
            compress(buffer_.data());
            buffered_ = 0;
        }
        while (buffered_ < 56) buffer_[buffered_++] = 0;
        for (unsigned i = 0; i != 8; ++i) {
            buffer_[56 + i] = static_cast<uint8_t>(original_bits >> (56 - i * 8));
        }
        compress(buffer_.data());

        std::array<uint8_t, 32> digest{};
        for (unsigned i = 0; i != 8; ++i) {
            digest[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
        }
        return digest;
    }

private:
    static uint32_t rotr(uint32_t value, unsigned amount) {
        return (value >> amount) | (value << (32 - amount));
    }

    void compress(const uint8_t* block) {
        static constexpr uint32_t k[64] = {
            UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf),
            UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
            UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98),
            UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
            UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7),
            UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
            UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f),
            UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
            UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8),
            UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
            UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85),
            UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
            UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e),
            UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
            UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819),
            UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
            UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c),
            UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
            UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee),
            UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
            UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7),
            UINT32_C(0xc67178f2)};
        uint32_t words[64]{};
        for (unsigned i = 0; i != 16; ++i) {
            words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                       static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (unsigned i = 16; i != 64; ++i) {
            const uint32_t s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^
                                (words[i - 15] >> 3);
            const uint32_t s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^
                                (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (unsigned i = 0; i != 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t choice = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + choice + k[i] + words[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> buffer_{};
    size_t buffered_ = 0;
    uint64_t bit_count_ = 0;
};

std::array<uint8_t, 32> digest(const void* bytes, size_t length) {
    Sha256 sha;
    if (length) sha.update(static_cast<const uint8_t*>(bytes), length);
    return sha.finish();
}

struct Descriptor {
    uint32_t type = 0;
    uint32_t config_version = 0;
    uint32_t bytes = 0;
    std::array<uint8_t, 32> sha256{};
};

/* The memo owns only private copies of the most recently observed descriptor
 * bytes for each Aurora row identity.  It is deliberately separate from the
 * exported typed digest dictionary: a row may change its bytes during a
 * capture, in which case the prior digest remains observable in the record
 * stream while this entry is replaced. */
struct DescriptorMemoEntry {
    uint32_t type = 0;
    uint64_t pipeline_ref = 0;
    uint32_t config_version = 0;
    std::vector<uint8_t> bytes;
    std::array<uint8_t, 32> sha256{};
};

struct Event {
    uint64_t sequence = 0;
    uint32_t kind = 0;
    uint64_t scope_id = 0;
    uint32_t thread_id = 0;
    MeleeWebPipelineSourceContext context{};
    uint64_t renderer_generation = 0;
    uint64_t device_generation = 0;
    uint64_t frame_id = 0;
    uint64_t packet_id = 0;
    uint32_t type = 0;
    uint64_t pipeline_ref = 0;
    uint32_t config_version = 0;
    std::array<uint8_t, 32> descriptor_sha256{};
    bool has_descriptor = false;
    uint32_t outcome = 0;
};

struct ActiveScope {
    uint64_t scope_id = 0;
    uint64_t nonce = 0;
    uint64_t device_generation = 0;
    uint64_t renderer_generation = 0;
    MeleeWebPipelineSourceContext context{};
};

struct ExecutionEntry {
    MeleeWebPipelineExecutionToken token{};
};

struct ThreadState {
    bool claimed = false;
    uint32_t thread_slot = 0;
    std::thread::id thread_id{};
    uint64_t source_scopes[MELEE_WEB_PIPELINE_MAX_SCOPES]{};
    uint32_t source_depth = 0;
    ExecutionEntry execution[MELEE_WEB_PIPELINE_MAX_EXECUTION_DEPTH]{};
    uint32_t execution_depth = 0;
    bool frame_active = false;
    bool packet_active = false;
    uint64_t frame_id = 0;
    uint64_t packet_id = 0;
};

} // namespace

struct MeleeWebPipelineRecorder {
    mutable std::mutex mutex;
    uint32_t max_records = kDefaultRecords;
    uint32_t max_descriptors = kDefaultDescriptors;
    uint32_t max_scopes = kDefaultScopes;
    uint32_t max_json_bytes = kDefaultJsonBytes;
    MeleeWebPipelineSourceContext boot_context{};
    bool boot_context_valid = false;
    bool capture_active = false;
    bool capture_final = false;
    bool valid = true;
    uint32_t invalid_reason = MELEE_WEB_PIPELINE_INVALID_NONE;
    uint64_t capture_generation = 0;
    uint64_t device_generation = 0;
    uint64_t renderer_generation = 0;
    uint64_t previous_device_generation = 0;
    uint64_t previous_renderer_generation = 0;
    bool device_active = false;
    bool renderer_active = false;
    uint64_t next_sequence = 1;
    uint64_t next_scope_id = 1;
    uint64_t next_execution_id = 1;
    uint64_t errors = 0;
    uint64_t dropped = 0;
    uint64_t total_records = 0;
    uint64_t drained_records = 0;
    std::vector<Event> records;
    std::vector<Descriptor> descriptors;
    std::vector<DescriptorMemoEntry> descriptor_memo;
    size_t descriptor_memo_bytes = 0;
    std::array<ActiveScope, MELEE_WEB_PIPELINE_MAX_SCOPES> scopes{};
    uint32_t active_scope_count = 0;
    std::array<ThreadState, kMaxThreads> threads{};
};

namespace {

int fail_locked(MeleeWebPipelineRecorder* recorder, uint32_t reason,
                const char* message, char* error, size_t error_size,
                bool dropped = false) {
    if (recorder) {
        recorder->valid = false;
        if (recorder->invalid_reason == MELEE_WEB_PIPELINE_INVALID_NONE) {
            recorder->invalid_reason = reason;
        }
        ++recorder->errors;
        if (dropped) ++recorder->dropped;
    }
    return write_error(error, error_size, message);
}

bool operational(const MeleeWebPipelineRecorder* recorder) {
    return recorder && recorder->capture_active && recorder->valid &&
           !recorder->capture_final;
}

ThreadState* thread_state_locked(MeleeWebPipelineRecorder* recorder,
                                 char* error, size_t error_size) {
    const auto current = std::this_thread::get_id();
    for (auto& state : recorder->threads) {
        if (state.claimed && state.thread_id == current) return &state;
    }
    for (unsigned i = 0; i != recorder->threads.size(); ++i) {
        auto& state = recorder->threads[i];
        if (!state.claimed) {
            state = ThreadState{};
            state.claimed = true;
            state.thread_slot = i + 1;
            state.thread_id = current;
            return &state;
        }
    }
    fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_THREAD_OVERFLOW,
                "pipeline provenance thread-state capacity exceeded", error,
                error_size, true);
    return nullptr;
}

ActiveScope* find_scope_locked(MeleeWebPipelineRecorder* recorder,
                               uint64_t scope_id) {
    for (unsigned i = 0; i != MELEE_WEB_PIPELINE_MAX_SCOPES; ++i) {
        if (recorder->scopes[i].scope_id == scope_id) return &recorder->scopes[i];
    }
    return nullptr;
}

const ActiveScope* find_scope_locked(const MeleeWebPipelineRecorder* recorder,
                                     uint64_t scope_id) {
    for (unsigned i = 0; i != MELEE_WEB_PIPELINE_MAX_SCOPES; ++i) {
        if (recorder->scopes[i].scope_id == scope_id) return &recorder->scopes[i];
    }
    return nullptr;
}

const MeleeWebPipelineSourceContext* current_context_locked(
    MeleeWebPipelineRecorder* recorder, ThreadState* thread,
    uint64_t* device_generation, uint64_t* renderer_generation,
    uint64_t* frame_id, uint64_t* packet_id) {
    if (thread->execution_depth) {
        const auto& token = thread->execution[thread->execution_depth - 1].token;
        *device_generation = token.device_generation;
        *renderer_generation = token.renderer_generation;
        *frame_id = token.frame_id;
        *packet_id = token.packet_id;
        return &token.context;
    }
    if (thread->source_depth) {
        const ActiveScope* scope = find_scope_locked(
            recorder, thread->source_scopes[thread->source_depth - 1]);
        if (!scope) return nullptr;
        *device_generation = scope->device_generation;
        *renderer_generation = scope->renderer_generation;
        *frame_id = thread->frame_active ? thread->frame_id : 0;
        *packet_id = thread->packet_active ? thread->packet_id : 0;
        return &scope->context;
    }
    return nullptr;
}

bool known_event(uint32_t kind) {
    return kind >= MELEE_WEB_PIPELINE_EVENT_LAST_REF &&
           kind <= MELEE_WEB_PIPELINE_EVENT_PACKET_USE;
}

bool known_outcome(uint32_t outcome) {
    return outcome >= MELEE_WEB_PIPELINE_OUTCOME_LAST_REF &&
           outcome <= MELEE_WEB_PIPELINE_OUTCOME_USED;
}

uint32_t expected_outcome(uint32_t kind) {
    switch (kind) {
    case MELEE_WEB_PIPELINE_EVENT_LAST_REF:
        return MELEE_WEB_PIPELINE_OUTCOME_LAST_REF;
    case MELEE_WEB_PIPELINE_EVENT_READY:
        return MELEE_WEB_PIPELINE_OUTCOME_READY;
    case MELEE_WEB_PIPELINE_EVENT_PENDING:
        return MELEE_WEB_PIPELINE_OUTCOME_PENDING;
    case MELEE_WEB_PIPELINE_EVENT_CREATE:
        return MELEE_WEB_PIPELINE_OUTCOME_CREATED;
    case MELEE_WEB_PIPELINE_EVENT_IMPORT:
        return MELEE_WEB_PIPELINE_OUTCOME_IMPORTED;
    case MELEE_WEB_PIPELINE_EVENT_DRAW_CACHE_REUSE:
        return MELEE_WEB_PIPELINE_OUTCOME_REUSED;
    case MELEE_WEB_PIPELINE_EVENT_MERGE:
        return MELEE_WEB_PIPELINE_OUTCOME_MERGED;
    case MELEE_WEB_PIPELINE_EVENT_PACKET_USE:
        return MELEE_WEB_PIPELINE_OUTCOME_USED;
    default:
        return 0;
    }
}

const char* event_name(uint32_t kind) {
    switch (kind) {
    case MELEE_WEB_PIPELINE_EVENT_SCOPE_BEGIN: return "scope_begin";
    case MELEE_WEB_PIPELINE_EVENT_SCOPE_END: return "scope_end";
    case MELEE_WEB_PIPELINE_EVENT_LAST_REF: return "last_ref";
    case MELEE_WEB_PIPELINE_EVENT_READY: return "ready";
    case MELEE_WEB_PIPELINE_EVENT_PENDING: return "pending";
    case MELEE_WEB_PIPELINE_EVENT_CREATE: return "create";
    case MELEE_WEB_PIPELINE_EVENT_IMPORT: return "import";
    case MELEE_WEB_PIPELINE_EVENT_DRAW_CACHE_REUSE: return "draw_cache_reuse";
    case MELEE_WEB_PIPELINE_EVENT_MERGE: return "merge";
    case MELEE_WEB_PIPELINE_EVENT_PACKET_USE: return "packet_use";
    default: return "unknown";
    }
}

const char* outcome_name(uint32_t outcome) {
    switch (outcome) {
    case MELEE_WEB_PIPELINE_OUTCOME_SCOPE_OPEN: return "scope_open";
    case MELEE_WEB_PIPELINE_OUTCOME_SCOPE_CLOSED: return "scope_closed";
    case MELEE_WEB_PIPELINE_OUTCOME_LAST_REF: return "last_ref";
    case MELEE_WEB_PIPELINE_OUTCOME_READY: return "ready";
    case MELEE_WEB_PIPELINE_OUTCOME_PENDING: return "pending";
    case MELEE_WEB_PIPELINE_OUTCOME_CREATED: return "created";
    case MELEE_WEB_PIPELINE_OUTCOME_IMPORTED: return "imported";
    case MELEE_WEB_PIPELINE_OUTCOME_REUSED: return "reused";
    case MELEE_WEB_PIPELINE_OUTCOME_MERGED: return "merged";
    case MELEE_WEB_PIPELINE_OUTCOME_USED: return "used";
    default: return "unknown";
    }
}

void append_u64(std::string& output, uint64_t value) {
    output += std::to_string(value);
}

void append_sha(std::string& output, const uint8_t* digest_bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i != 32; ++i) {
        output.push_back(hex[digest_bytes[i] >> 4]);
        output.push_back(hex[digest_bytes[i] & 15]);
    }
}

void append_ref(std::string& output, uint64_t value) {
    char buffer[19]{};
    std::snprintf(buffer, sizeof(buffer), "0x%016llx",
                  static_cast<unsigned long long>(value));
    output += buffer;
}

void append_context(std::string& output,
                    const MeleeWebPipelineSourceContext& context) {
    output += "{\"scene\":";
    append_u64(output, context.scene);
    output += ",\"phase\":";
    append_u64(output, context.phase);
    output += ",\"world_generation\":";
    append_u64(output, context.world_generation);
    output += ",\"route_epoch\":";
    append_u64(output, context.route_epoch);
    output += ",\"source_tick\":";
    append_u64(output, context.source_tick);
    output += ",\"coverage_case_id\":";
    append_u64(output, context.coverage_case_id);
    output += ",\"input_binding_sha256\":\"";
    if (context.input_binding_sha256_present) {
        append_sha(output, context.input_binding_sha256);
    }
    output += "\",\"stage\":";
    append_u64(output, context.stage);
    output += ",\"ground\":";
    append_u64(output, context.ground);
    output += ",\"hud_layout\":";
    append_u64(output, context.hud_layout);
    output += ",\"gobj_classifier\":";
    append_u64(output, context.gobj_classifier);
    output += ",\"gx_link\":";
    append_u64(output, context.gx_link);
    output += ",\"render_pass\":";
    append_u64(output, context.render_pass);
    output += ",\"active_player_count\":";
    append_u64(output, context.active_player_count);
    output += ",\"players\":[";
    for (unsigned i = 0; i != 4; ++i) {
        if (i) output.push_back(',');
        const auto& player = context.players[i];
        output += "{\"character\":";
        append_u64(output, player.character);
        output += ",\"fighter_kind\":";
        append_u64(output, player.fighter_kind);
        output += ",\"costume\":";
        append_u64(output, player.costume);
        output += ",\"subcolor\":";
        append_u64(output, player.subcolor);
        output += ",\"effect_bank\":";
        append_u64(output, player.effect_bank);
        output += ",\"motion_id\":";
        output += std::to_string(player.motion_id);
        output += ",\"stocks\":";
        output += std::to_string(player.stocks);
        output.push_back('}');
    }
    output += "],\"owner_kind\":";
    append_u64(output, context.owner_kind);
    output += ",\"owner_id\":";
    append_u64(output, context.owner_id);
    output += ",\"owner_effect_bank\":";
    append_u64(output, context.owner_effect_bank);
    output += ",\"owner_explicit_unknown\":";
    output += context.owner_explicit_unknown ? "true" : "false";
    output.push_back('}');
}

void append_status(std::string& output, const MeleeWebPipelineRecorder& recorder,
                   uint32_t pending_records, uint64_t drained_records,
                   bool capture_active, bool capture_final) {
    const uint64_t reported_renderer_generation = recorder.renderer_active
                                                    ? recorder.renderer_generation
                                                    : recorder.previous_renderer_generation;
    const uint64_t reported_device_generation = recorder.device_active
                                                  ? recorder.device_generation
                                                  : recorder.previous_device_generation;
    output += "{\"capture_active\":";
    output += capture_active ? "true" : "false";
    output += ",\"valid\":";
    output += recorder.valid ? "true" : "false";
    output += ",\"final\":";
    output += capture_final ? "true" : "false";
    output += ",\"invalid_reason\":";
    append_u64(output, recorder.invalid_reason);
    output += ",\"invalid_reason_name\":\"";
    output += reason_name(recorder.invalid_reason);
    output += "\",\"capture_generation\":";
    append_u64(output, recorder.capture_generation);
    output += ",\"renderer_generation\":";
    append_u64(output, reported_renderer_generation);
    output += ",\"device_generation\":";
    append_u64(output, reported_device_generation);
    output += ",\"next_sequence\":";
    append_u64(output, recorder.next_sequence);
    output += ",\"errors\":";
    append_u64(output, recorder.errors);
    output += ",\"dropped\":";
    append_u64(output, recorder.dropped);
    output += ",\"open_scopes\":";
    append_u64(output, recorder.active_scope_count);
    output += ",\"total_records\":";
    append_u64(output, recorder.total_records);
    output += ",\"drained_records\":";
    append_u64(output, drained_records);
    output += ",\"pending_records\":";
    append_u64(output, pending_records);
    output += ",\"descriptor_count\":";
    append_u64(output, recorder.descriptors.size());
    output += "}";
}

std::string event_json(const Event& event, size_t context_index) {
    std::string output;
    output.reserve(1200);
    output += "{\"sequence\":";
    append_u64(output, event.sequence);
    output += ",\"kind\":\"";
    output += event_name(event.kind);
    output += "\",\"scope_id\":";
    append_u64(output, event.scope_id);
    output += ",\"thread_id\":";
    append_u64(output, event.thread_id);
    output += ",\"context_index\":";
    append_u64(output, context_index);
    output += ",\"renderer_generation\":";
    append_u64(output, event.renderer_generation);
    output += ",\"device_generation\":";
    append_u64(output, event.device_generation);
    output += ",\"frame_id\":";
    append_u64(output, event.frame_id);
    output += ",\"packet_id\":";
    append_u64(output, event.packet_id);
    output += ",\"type\":";
    append_u64(output, event.type);
    output += ",\"pipeline_ref\":\"";
    append_ref(output, event.pipeline_ref);
    output += "\",\"config_version\":";
    append_u64(output, event.config_version);
    output += ",\"descriptor_sha256\":\"";
    if (event.has_descriptor) append_sha(output, event.descriptor_sha256.data());
    output += "\",\"outcome\":\"";
    output += outcome_name(event.outcome);
    output += "\"}";
    return output;
}

std::string descriptor_json(const Descriptor& descriptor) {
    std::string output;
    output += "{\"sha256\":\"";
    append_sha(output, descriptor.sha256.data());
    output += "\",\"type\":";
    append_u64(output, descriptor.type);
    output += ",\"config_version\":";
    append_u64(output, descriptor.config_version);
    output += ",\"bytes\":";
    append_u64(output, descriptor.bytes);
    output += "}";
    return output;
}

bool build_drain_json(const MeleeWebPipelineRecorder& recorder, size_t count,
                      uint64_t drained_after, bool final_after,
                      std::string* result) {
    std::vector<unsigned> descriptor_indices;
    descriptor_indices.reserve(recorder.descriptors.size());
    for (size_t i = 0; i != count; ++i) {
        const auto& event = recorder.records[i];
        if (!event.has_descriptor) continue;
        const auto found = std::find_if(
            descriptor_indices.begin(), descriptor_indices.end(),
            [&](unsigned index) {
                const auto& descriptor = recorder.descriptors[index];
                return descriptor.type == event.type &&
                       descriptor.config_version == event.config_version &&
                       descriptor.sha256 == event.descriptor_sha256;
            });
        if (found == descriptor_indices.end()) {
            for (unsigned index = 0; index != recorder.descriptors.size(); ++index) {
                const auto& descriptor = recorder.descriptors[index];
                if (descriptor.type == event.type &&
                    descriptor.config_version == event.config_version &&
                    descriptor.sha256 == event.descriptor_sha256) {
                    descriptor_indices.push_back(index);
                    break;
                }
            }
        }
    }

    // Context is immutable for a source scope. Export it once per chunk,
    // preserving every demand event and every context field losslessly.
    std::vector<const Event*> contexts;
    std::vector<size_t> context_indices;
    contexts.reserve(count);
    context_indices.reserve(count);
    for (size_t i = 0; i != count; ++i) {
        const auto& event = recorder.records[i];
        size_t index = 0;
        for (; index != contexts.size(); ++index) {
            if (contexts[index]->scope_id == event.scope_id &&
                context_equal(contexts[index]->context, event.context)) break;
        }
        if (index == contexts.size()) contexts.push_back(&event);
        context_indices.push_back(index);
    }
    std::string output;
    output.reserve(std::min<size_t>(recorder.max_json_bytes, 4096));
    output += "{\"schema\":\"" MELEE_WEB_PIPELINE_PROVENANCE_SCHEMA
              "\",\"version\":1,\"capture_generation\":";
    append_u64(output, recorder.capture_generation);
    const uint64_t sequence_begin = count ? recorder.records.front().sequence : 0;
    const uint64_t sequence_end = count ? recorder.records[count - 1].sequence : 0;
    output += ",\"sequence_begin\":";
    append_u64(output, sequence_begin);
    output += ",\"sequence_end\":";
    append_u64(output, sequence_end);
    output += ",\"contexts\":[";
    for (size_t i = 0; i != contexts.size(); ++i) {
        if (i) output.push_back(',');
        append_context(output, contexts[i]->context);
    }
    output += "]";
    output += ",\"records\":[";
    for (size_t i = 0; i != count; ++i) {
        if (i) output.push_back(',');
        output += event_json(recorder.records[i], context_indices[i]);
    }
    output += "],\"descriptors\":[";
    for (size_t i = 0; i != descriptor_indices.size(); ++i) {
        if (i) output.push_back(',');
        output += descriptor_json(recorder.descriptors[descriptor_indices[i]]);
    }
    output += "],\"status\":";
    append_status(output, recorder,
                  static_cast<uint32_t>(recorder.records.size() - count),
                  drained_after, final_after ? false : recorder.capture_active,
                  final_after || recorder.capture_final);
    output.push_back('}');
    *result = std::move(output);
    return true;
}

bool copy_status(const MeleeWebPipelineRecorder& recorder,
                 MeleeWebPipelineStatus* status, const ThreadState* thread) {
    if (!status) return false;
    *status = MeleeWebPipelineStatus{};
    status->capture_active = recorder.capture_active ? 1 : 0;
    status->valid = recorder.valid ? 1 : 0;
    status->final = recorder.capture_final ? 1 : 0;
    status->invalid_reason = recorder.invalid_reason;
    status->capture_generation = recorder.capture_generation;
    status->renderer_generation = recorder.renderer_active
                                    ? recorder.renderer_generation
                                    : recorder.previous_renderer_generation;
    status->device_generation = recorder.device_active
                                  ? recorder.device_generation
                                  : recorder.previous_device_generation;
    status->next_sequence = recorder.next_sequence;
    status->errors = recorder.errors;
    status->dropped = recorder.dropped;
    status->total_records = recorder.total_records;
    status->drained_records = recorder.drained_records;
    status->pending_records = static_cast<uint32_t>(recorder.records.size());
    status->descriptor_count = static_cast<uint32_t>(recorder.descriptors.size());
    status->open_scopes = recorder.active_scope_count;
    status->active_execution_depth = thread ? thread->execution_depth : 0;
    if (thread) {
        status->frame_id = thread->frame_active ? thread->frame_id : 0;
        status->packet_id = thread->packet_active ? thread->packet_id : 0;
    }
    return true;
}

bool stale_generation(const MeleeWebPipelineRecorder& recorder,
                      uint64_t device_generation, uint64_t renderer_generation,
                      uint32_t event_kind, uint32_t* reason,
                      const char** message) {
    if (event_kind == MELEE_WEB_PIPELINE_EVENT_IMPORT) return false;
    if (!recorder.device_active || !device_generation ||
        device_generation != recorder.device_generation) {
        *reason = device_generation != recorder.device_generation
                      ? MELEE_WEB_PIPELINE_INVALID_STALE_DEVICE
                      : MELEE_WEB_PIPELINE_INVALID_LIFECYCLE;
        *message = "pipeline provenance event has no current device generation";
        return true;
    }
    if (renderer_generation &&
        (!recorder.renderer_active || renderer_generation != recorder.renderer_generation)) {
        *reason = MELEE_WEB_PIPELINE_INVALID_STALE_RENDERER;
        *message = "pipeline provenance event has a stale renderer generation";
        return true;
    }
    return false;
}

bool descriptor_memo_key_matches(const DescriptorMemoEntry& entry,
                                 uint32_t type, uint64_t pipeline_ref,
                                 uint32_t config_version) {
    return entry.type == type && entry.pipeline_ref == pipeline_ref &&
           entry.config_version == config_version;
}

bool memoize_descriptor_locked(MeleeWebPipelineRecorder* recorder,
                               uint32_t type, uint64_t pipeline_ref,
                               uint32_t config_version, const void* blob,
                               size_t blob_length,
                               std::array<uint8_t, 32>* digest_out,
                               char* error, size_t error_size) {
    for (auto& entry : recorder->descriptor_memo) {
        if (!descriptor_memo_key_matches(entry, type, pipeline_ref,
                                         config_version)) {
            continue;
        }
        /* The pointer is only a source hint.  The full copied bytes and exact
         * length authenticate a hit, so a reused/mutated source buffer is
         * re-hashed and cannot silently inherit a stale digest. */
        if (entry.bytes.size() == blob_length &&
            (!blob_length || std::memcmp(entry.bytes.data(), blob, blob_length) == 0)) {
            *digest_out = entry.sha256;
            return true;
        }

        const size_t old_bytes = entry.bytes.size();
        if (blob_length > MELEE_WEB_PIPELINE_MAX_DESCRIPTOR_MEMO_BYTES ||
            recorder->descriptor_memo_bytes - old_bytes >
                MELEE_WEB_PIPELINE_MAX_DESCRIPTOR_MEMO_BYTES - blob_length) {
            return fail_locked(
                recorder, MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW,
                "pipeline provenance descriptor memo byte capacity exceeded",
                error, error_size, true);
        }

        try {
            std::vector<uint8_t> replacement;
            replacement.resize(blob_length);
            if (blob_length) std::memcpy(replacement.data(), blob, blob_length);
            const auto descriptor_sha256 = digest(blob, blob_length);
            entry.bytes.swap(replacement);
            entry.sha256 = descriptor_sha256;
            recorder->descriptor_memo_bytes =
                recorder->descriptor_memo_bytes - old_bytes + blob_length;
            *digest_out = entry.sha256;
            return true;
        } catch (const std::bad_alloc&) {
            return fail_locked(
                recorder, MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW,
                "pipeline provenance descriptor memo allocation failed",
                error, error_size, true);
        }
    }

    if (recorder->descriptor_memo.size() >= recorder->max_descriptors) {
        return fail_locked(
            recorder, MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW,
            "pipeline provenance descriptor memo entry capacity exceeded",
            error, error_size, true);
    }
    if (blob_length > MELEE_WEB_PIPELINE_MAX_DESCRIPTOR_MEMO_BYTES ||
        recorder->descriptor_memo_bytes >
            MELEE_WEB_PIPELINE_MAX_DESCRIPTOR_MEMO_BYTES - blob_length) {
        return fail_locked(
            recorder, MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW,
            "pipeline provenance descriptor memo byte capacity exceeded",
            error, error_size, true);
    }

    try {
        DescriptorMemoEntry entry{};
        entry.type = type;
        entry.pipeline_ref = pipeline_ref;
        entry.config_version = config_version;
        entry.bytes.resize(blob_length);
        if (blob_length) std::memcpy(entry.bytes.data(), blob, blob_length);
        const auto descriptor_sha256 = digest(blob, blob_length);
        entry.sha256 = descriptor_sha256;
        recorder->descriptor_memo.push_back(std::move(entry));
        recorder->descriptor_memo_bytes += blob_length;
        *digest_out = recorder->descriptor_memo.back().sha256;
        return true;
    } catch (const std::bad_alloc&) {
        return fail_locked(
            recorder, MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW,
            "pipeline provenance descriptor memo allocation failed", error,
            error_size, true);
    }
}

bool add_descriptor_locked(MeleeWebPipelineRecorder* recorder,
                           uint32_t type, uint32_t config_version,
                           const std::array<uint8_t, 32>& sha256,
                           uint32_t bytes, char* error, size_t error_size) {
    for (const auto& descriptor : recorder->descriptors) {
        if (descriptor.type == type && descriptor.config_version == config_version &&
            descriptor.sha256 == sha256) {
            return true;
        }
    }
    if (recorder->descriptors.size() >= recorder->max_descriptors) {
        fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW,
                    "pipeline provenance descriptor dictionary capacity exceeded",
                    error, error_size, true);
        return false;
    }
    recorder->descriptors.push_back(
        Descriptor{type, config_version, bytes, sha256});
    return true;
}

bool append_event_locked(MeleeWebPipelineRecorder* recorder, uint32_t kind,
                         const MeleeWebPipelineSourceContext& context,
                         uint64_t device_generation,
                         uint64_t renderer_generation, uint64_t frame_id,
                         uint64_t packet_id, uint32_t type, uint64_t pipeline_ref,
                         uint32_t config_version,
                         const std::array<uint8_t, 32>* descriptor_sha256,
                         uint32_t outcome, char* error, size_t error_size) {
    if (recorder->next_sequence == std::numeric_limits<uint64_t>::max()) {
        fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_OVERFLOW,
                    "pipeline provenance sequence counter exhausted", error,
                    error_size, true);
        return false;
    }
    if (recorder->records.size() >= recorder->max_records) {
        fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_OVERFLOW,
                    "pipeline provenance record ring capacity exceeded", error,
                    error_size, true);
        return false;
    }
    Event event{};
    event.sequence = recorder->next_sequence;
    event.kind = kind;
    event.context = context;
    event.renderer_generation = renderer_generation;
    event.device_generation = device_generation;
    event.frame_id = frame_id;
    event.packet_id = packet_id;
    event.type = type;
    event.pipeline_ref = pipeline_ref;
    event.config_version = config_version;
    event.outcome = outcome;
    if (descriptor_sha256) {
        event.has_descriptor = true;
        event.descriptor_sha256 = *descriptor_sha256;
    }
    recorder->records.push_back(event);
    ++recorder->next_sequence;
    ++recorder->total_records;
    return true;
}

void reset_thread_states(MeleeWebPipelineRecorder* recorder) {
    for (auto& thread : recorder->threads) thread = ThreadState{};
}

int observe_locked(MeleeWebPipelineRecorder* recorder, ThreadState* thread,
                   uint32_t event_kind, uint32_t type, uint64_t pipeline_ref,
                   uint32_t config_version, const void* blob, size_t blob_length,
                   uint32_t outcome, uint64_t frame_id, uint64_t packet_id,
                   char* error, size_t error_size) {
    if (!thread || !operational(recorder)) {
        return fail_locked(recorder, !recorder->capture_active
                                      ? MELEE_WEB_PIPELINE_INVALID_NOT_STARTED
                                      : MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST,
                            "pipeline observation requires an active valid capture",
                            error, error_size);
    }
    if (!known_event(event_kind) || !known_outcome(outcome) ||
        expected_outcome(event_kind) != outcome ||
        (blob_length && !blob) || blob_length > MELEE_WEB_PIPELINE_MAX_TYPE_BYTES) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST,
                            "pipeline observation has an invalid event, outcome or descriptor",
                            error, error_size);
    }
    const bool is_import = event_kind == MELEE_WEB_PIPELINE_EVENT_IMPORT;
    uint64_t device_generation = 0;
    uint64_t renderer_generation = 0;
    uint64_t current_frame = 0;
    uint64_t current_packet = 0;
    const MeleeWebPipelineSourceContext* context = nullptr;
    if (is_import) {
        /* Imports are background work and always use the copied boot context.
         * In particular, do not inherit the caller's active source scope or
         * render/frame identity. */
        if (recorder->boot_context_valid) context = &recorder->boot_context;
    } else {
        context = current_context_locked(recorder, thread, &device_generation,
                                         &renderer_generation, &current_frame,
                                         &current_packet);
    }
    if (!context) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_MISSING_CONTEXT,
                            "pipeline observation has no source context", error, error_size);
    }
    if (event_kind != MELEE_WEB_PIPELINE_EVENT_IMPORT) {
        uint32_t reason = MELEE_WEB_PIPELINE_INVALID_LIFECYCLE;
        const char* message = "pipeline observation has no current device generation";
        if (stale_generation(*recorder, device_generation, renderer_generation,
                             event_kind, &reason, &message)) {
            return fail_locked(recorder, reason, message, error, error_size);
        }
    }
    const uint64_t output_frame = is_import ? 0 : (frame_id ? frame_id : current_frame);
    const uint64_t output_packet = is_import ? 0 : (packet_id ? packet_id : current_packet);
    if (event_kind == MELEE_WEB_PIPELINE_EVENT_PACKET_USE &&
        (!output_packet || !output_frame)) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST,
                            "packet use requires copied nonzero frame and packet IDs",
                            error, error_size);
    }
    std::array<uint8_t, 32> descriptor_sha256{};
    if (!memoize_descriptor_locked(recorder, type, pipeline_ref, config_version,
                                   blob, blob_length, &descriptor_sha256,
                                   error, error_size)) {
        return 0;
    }
    if (!add_descriptor_locked(recorder, type, config_version, descriptor_sha256,
                               static_cast<uint32_t>(blob_length), error, error_size)) {
        return 0;
    }
    if (!append_event_locked(recorder, event_kind, *context, device_generation,
                             renderer_generation, output_frame, output_packet,
                             type, pipeline_ref, config_version, &descriptor_sha256,
                             outcome, error, error_size)) {
        return 0;
    }
    Event& event = recorder->records.back();
    event.thread_id = thread->thread_slot;
    if (!is_import) {
        event.scope_id = thread->execution_depth
                           ? thread->execution[thread->execution_depth - 1].token.scope_id
                           : (thread->source_depth
                                  ? thread->source_scopes[thread->source_depth - 1]
                                  : 0);
    }
    return clear_error(error, error_size);
}

} // namespace

extern "C" {

MeleeWebPipelineRecorder* melee_web_pipeline_recorder_create(
    const MeleeWebPipelineRecorderConfig* config, char* error, size_t error_size) {
    try {
        auto* recorder = new (std::nothrow) MeleeWebPipelineRecorder;
        if (!recorder) {
            write_error(error, error_size,
                        "cannot allocate pipeline provenance recorder");
            return nullptr;
        }
        if (config) {
            recorder->max_records = config->max_records ? config->max_records : kDefaultRecords;
            recorder->max_descriptors = config->max_descriptors ? config->max_descriptors : kDefaultDescriptors;
            recorder->max_scopes = config->max_scopes ? config->max_scopes : kDefaultScopes;
            recorder->max_json_bytes = config->max_json_bytes ? config->max_json_bytes : kDefaultJsonBytes;
            recorder->boot_context = config->boot_context;
            recorder->boot_context_valid = context_valid(recorder->boot_context);
            if (!recorder->boot_context_valid && config->boot_context.world_generation) {
                delete recorder;
                write_error(error, error_size, "recorder boot context is invalid");
                return nullptr;
            }
        }
        recorder->max_records = std::min(recorder->max_records, kDefaultRecords);
        recorder->max_descriptors = std::min(recorder->max_descriptors, kDefaultDescriptors);
        recorder->max_scopes = std::min(recorder->max_scopes, kDefaultScopes);
        recorder->max_json_bytes = std::min(recorder->max_json_bytes, kDefaultJsonBytes);
        recorder->records.reserve(recorder->max_records);
        recorder->descriptors.reserve(recorder->max_descriptors);
        recorder->descriptor_memo.reserve(recorder->max_descriptors);
        clear_error(error, error_size);
        return recorder;
    } catch (const std::bad_alloc&) {
        write_error(error, error_size, "cannot reserve bounded pipeline provenance storage");
        return nullptr;
    }
}

int melee_web_pipeline_recorder_destroy(MeleeWebPipelineRecorder* recorder,
                                        char* error, size_t error_size) {
    if (!recorder) return clear_error(error, error_size);
    {
        std::lock_guard<std::mutex> lock(recorder->mutex);
        if (recorder->capture_active && !recorder->capture_final) {
            return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE,
                                "cannot destroy an active pipeline provenance capture",
                                error, error_size);
        }
    }
    delete recorder;
    return clear_error(error, error_size);
}

int melee_web_pipeline_capture_begin(MeleeWebPipelineRecorder* recorder,
                                     const MeleeWebPipelineSourceContext* boot_context,
                                     char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    if (recorder->capture_active) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_LIFECYCLE,
                            "pipeline provenance capture is already active", error, error_size);
    }
    const MeleeWebPipelineSourceContext* selected = boot_context;
    if (!selected && recorder->boot_context_valid) selected = &recorder->boot_context;
    if (!selected || !context_valid(*selected) ||
        selected->scene != MELEE_WEB_PIPELINE_SCENE_BOOT ||
        selected->phase != MELEE_WEB_PIPELINE_PHASE_PREPARATION) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_INVALID_CONTEXT,
                            "capture begin requires a valid BOOT/PREPARATION context",
                            error, error_size);
    }
    if (recorder->capture_generation == std::numeric_limits<uint64_t>::max()) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_OVERFLOW,
                            "pipeline provenance capture generation exhausted", error, error_size);
    }
    ++recorder->capture_generation;
    recorder->boot_context = *selected;
    recorder->boot_context_valid = true;
    recorder->capture_active = true;
    recorder->capture_final = false;
    recorder->valid = true;
    recorder->invalid_reason = MELEE_WEB_PIPELINE_INVALID_NONE;
    recorder->device_active = false;
    recorder->renderer_active = false;
    recorder->device_generation = 0;
    recorder->renderer_generation = 0;
    recorder->previous_device_generation = 0;
    recorder->previous_renderer_generation = 0;
    recorder->next_sequence = 1;
    recorder->next_scope_id = 1;
    recorder->next_execution_id = 1;
    recorder->errors = 0;
    recorder->dropped = 0;
    recorder->total_records = 0;
    recorder->drained_records = 0;
    recorder->records.clear();
    recorder->descriptors.clear();
    recorder->descriptor_memo.clear();
    recorder->descriptor_memo_bytes = 0;
    recorder->scopes = {};
    recorder->active_scope_count = 0;
    reset_thread_states(recorder);
    return clear_error(error, error_size);
}

int melee_web_pipeline_capture_end(MeleeWebPipelineRecorder* recorder,
                                   char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    if (!recorder->capture_active) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_NOT_STARTED,
                            "pipeline provenance capture is not active", error, error_size);
    }
    if (recorder->active_scope_count) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE,
                            "pipeline provenance capture has open source scopes", error, error_size);
    }
    for (const auto& thread : recorder->threads) {
        if (thread.claimed && (thread.source_depth || thread.execution_depth ||
                               thread.frame_active || thread.packet_active)) {
            return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE,
                                "pipeline provenance capture has an open thread scope",
                                error, error_size);
        }
    }
    recorder->capture_active = false;
    return clear_error(error, error_size);
}

int melee_web_pipeline_device_begin(MeleeWebPipelineRecorder* recorder,
                                    uint64_t generation, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    if (!operational(recorder) || !generation) {
        return fail_locked(recorder, !recorder || !recorder->capture_active
                                      ? MELEE_WEB_PIPELINE_INVALID_NOT_STARTED
                                      : MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST,
                            "device begin requires an active capture and nonzero generation",
                            error, error_size);
    }
    if (recorder->device_active || recorder->renderer_active ||
        generation <= recorder->previous_device_generation) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_LIFECYCLE,
                            "device generation is active or not monotonic", error, error_size);
    }
    recorder->device_generation = generation;
    recorder->previous_device_generation = generation;
    recorder->device_active = true;
    return clear_error(error, error_size);
}

int melee_web_pipeline_device_end(MeleeWebPipelineRecorder* recorder,
                                  uint64_t generation, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    if (!operational(recorder) || !recorder->device_active ||
        generation != recorder->device_generation || recorder->renderer_active) {
        const uint32_t reason = recorder->device_active && generation != recorder->device_generation
                                    ? MELEE_WEB_PIPELINE_INVALID_STALE_DEVICE
                                    : MELEE_WEB_PIPELINE_INVALID_LIFECYCLE;
        return fail_locked(recorder, reason, "device generation end does not match the active device",
                            error, error_size);
    }
    recorder->device_active = false;
    recorder->device_generation = 0;
    return clear_error(error, error_size);
}

int melee_web_pipeline_renderer_begin(MeleeWebPipelineRecorder* recorder,
                                      uint64_t generation, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    if (!operational(recorder) || !recorder->device_active || !generation ||
        recorder->renderer_active || generation <= recorder->previous_renderer_generation) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_LIFECYCLE,
                            "renderer begin requires a new generation on an active device",
                            error, error_size);
    }
    recorder->renderer_generation = generation;
    recorder->previous_renderer_generation = generation;
    recorder->renderer_active = true;
    return clear_error(error, error_size);
}

int melee_web_pipeline_renderer_end(MeleeWebPipelineRecorder* recorder,
                                    uint64_t generation, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    if (!operational(recorder) || !recorder->renderer_active ||
        generation != recorder->renderer_generation) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_STALE_RENDERER,
                            "renderer generation end does not match the active renderer",
                            error, error_size);
    }
    for (const auto& thread : recorder->threads) {
        if (thread.claimed && (thread.frame_active || thread.packet_active)) {
            return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_LIFECYCLE,
                                "renderer end has an active frame or packet", error, error_size);
        }
    }
    recorder->renderer_active = false;
    recorder->renderer_generation = 0;
    return clear_error(error, error_size);
}

int melee_web_pipeline_frame_begin(MeleeWebPipelineRecorder* recorder,
                                   uint64_t frame_id, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    if (!operational(recorder) || !recorder->device_active ||
        !recorder->renderer_active || !frame_id || thread->frame_active) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_LIFECYCLE,
                            "frame begin requires an idle active renderer", error, error_size);
    }
    thread->frame_active = true;
    thread->frame_id = frame_id;
    thread->packet_active = false;
    thread->packet_id = 0;
    return clear_error(error, error_size);
}

int melee_web_pipeline_frame_end(MeleeWebPipelineRecorder* recorder,
                                 uint64_t frame_id, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    if (!operational(recorder) || !thread->frame_active ||
        thread->frame_id != frame_id || thread->packet_active) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_LIFECYCLE,
                            "frame end does not match the active frame", error, error_size);
    }
    thread->frame_active = false;
    thread->frame_id = 0;
    return clear_error(error, error_size);
}

int melee_web_pipeline_packet_begin(MeleeWebPipelineRecorder* recorder,
                                    uint64_t packet_id, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    if (!operational(recorder) || !thread->frame_active ||
        !packet_id || thread->packet_active) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_LIFECYCLE,
                            "packet begin requires an idle active frame", error, error_size);
    }
    thread->packet_active = true;
    thread->packet_id = packet_id;
    return clear_error(error, error_size);
}

int melee_web_pipeline_packet_end(MeleeWebPipelineRecorder* recorder,
                                  uint64_t packet_id, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    if (!operational(recorder) || !thread->packet_active ||
        thread->packet_id != packet_id) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_LIFECYCLE,
                            "packet end does not match the active packet", error, error_size);
    }
    thread->packet_active = false;
    thread->packet_id = 0;
    return clear_error(error, error_size);
}

int melee_web_pipeline_source_scope_begin(MeleeWebPipelineRecorder* recorder,
                                          const MeleeWebPipelineSourceContext* context,
                                          MeleeWebPipelineSourceToken* token,
                                          char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    if (!operational(recorder) || !context || !token || !context_valid(*context)) {
        return fail_locked(recorder, context && token
                                      ? MELEE_WEB_PIPELINE_INVALID_INVALID_CONTEXT
                                      : MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST,
                            "source scope requires a valid copied context and token output",
                            error, error_size);
    }
    if (thread->source_depth >= recorder->max_scopes ||
        recorder->active_scope_count >= recorder->max_scopes ||
        recorder->next_scope_id == 0) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_OVERFLOW,
                            "source scope capacity exceeded", error, error_size, true);
    }
    unsigned slot = MELEE_WEB_PIPELINE_MAX_SCOPES;
    for (unsigned i = 0; i != MELEE_WEB_PIPELINE_MAX_SCOPES; ++i) {
        if (recorder->scopes[i].scope_id == 0) {
            slot = i;
            break;
        }
    }
    if (slot == MELEE_WEB_PIPELINE_MAX_SCOPES) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_OVERFLOW,
                            "source scope storage exhausted", error, error_size, true);
    }
    const uint64_t scope_id = recorder->next_scope_id++;
    const uint64_t frame_id = thread->frame_active ? thread->frame_id : 0;
    const uint64_t packet_id = thread->packet_active ? thread->packet_id : 0;
    const uint64_t nonce = token_nonce(
        recorder->capture_generation, scope_id, *context,
        recorder->device_generation, recorder->renderer_generation,
        frame_id, packet_id);
    if (!append_event_locked(recorder, MELEE_WEB_PIPELINE_EVENT_SCOPE_BEGIN, *context,
                             recorder->device_generation, recorder->renderer_generation,
                             frame_id, packet_id, 0, 0, 0, nullptr,
                             MELEE_WEB_PIPELINE_OUTCOME_SCOPE_OPEN, error, error_size)) {
        --recorder->next_scope_id;
        return 0;
    }
    recorder->records.back().scope_id = scope_id;
    recorder->records.back().thread_id = thread->thread_slot;
    recorder->scopes[slot] = ActiveScope{scope_id, nonce, recorder->device_generation,
                                         recorder->renderer_generation, *context};
    ++recorder->active_scope_count;
    thread->source_scopes[thread->source_depth++] = scope_id;
    *token = MeleeWebPipelineSourceToken{};
    token->context = *context;
    token->capture_generation = recorder->capture_generation;
    token->device_generation = recorder->device_generation;
    token->renderer_generation = recorder->renderer_generation;
    token->frame_id = frame_id;
    token->packet_id = packet_id;
    token->scope_id = scope_id;
    token->token_nonce = nonce;
    token->valid = 1;
    return clear_error(error, error_size);
}

int melee_web_pipeline_source_scope_end(MeleeWebPipelineRecorder* recorder,
                                        MeleeWebPipelineSourceToken* token,
                                        char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    if (!operational(recorder) || !token || !token->valid ||
        token->capture_generation != recorder->capture_generation ||
        token->token_nonce != token_nonce(
            recorder->capture_generation, token->scope_id, token->context,
            token->device_generation, token->renderer_generation,
            token->frame_id, token->packet_id) ||
        token->closed || !thread->source_depth ||
        thread->source_scopes[thread->source_depth - 1] != token->scope_id) {
        const uint32_t reason = token &&
                                (token->capture_generation != recorder->capture_generation ||
                                 !recorder->capture_active)
                                  ? MELEE_WEB_PIPELINE_INVALID_STALE_CAPTURE
                                  : MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE;
        return fail_locked(recorder, reason, "source scope end is not the matching open scope",
                            error, error_size);
    }
    ActiveScope* scope = find_scope_locked(recorder, token->scope_id);
    if (!scope || scope->nonce != token->token_nonce ||
        scope->device_generation != token->device_generation ||
        scope->renderer_generation != token->renderer_generation ||
        !context_equal(scope->context, token->context)) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE,
                            "source scope token context does not match its open scope",
                            error, error_size);
    }
    if (!append_event_locked(recorder, MELEE_WEB_PIPELINE_EVENT_SCOPE_END, scope->context,
                             scope->device_generation, scope->renderer_generation,
                             token->frame_id, token->packet_id,
                             0, 0, 0, nullptr,
                             MELEE_WEB_PIPELINE_OUTCOME_SCOPE_CLOSED, error, error_size)) {
        return 0;
    }
    recorder->records.back().scope_id = token->scope_id;
    recorder->records.back().thread_id = thread->thread_slot;
    scope->scope_id = 0;
    --recorder->active_scope_count;
    --thread->source_depth;
    token->closed = 1;
    return clear_error(error, error_size);
}

int melee_web_pipeline_execution_push(MeleeWebPipelineRecorder* recorder,
                                       const MeleeWebPipelineSourceToken* token,
                                       MeleeWebPipelineExecutionToken* execution,
                                       char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    if (!operational(recorder) || !token || !execution || !token->valid ||
        token->capture_generation != recorder->capture_generation || !token->scope_id ||
        token->token_nonce != token_nonce(
            recorder->capture_generation, token->scope_id, token->context,
            token->device_generation, token->renderer_generation,
            token->frame_id, token->packet_id)) {
        return fail_locked(recorder,
                            token && (token->capture_generation != recorder->capture_generation ||
                                      !recorder->capture_active)
                              ? MELEE_WEB_PIPELINE_INVALID_STALE_CAPTURE
                              : MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST,
                            "execution push received an invalid or stale source token",
                            error, error_size);
    }
    if (thread->execution_depth >= MELEE_WEB_PIPELINE_MAX_EXECUTION_DEPTH) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_OVERFLOW,
                            "execution token stack capacity exceeded", error, error_size, true);
    }
    if (token->device_generation != recorder->device_generation ||
        (token->device_generation && !recorder->device_active)) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_STALE_DEVICE,
                            "execution token carries a stale device generation", error, error_size);
    }
    if (token->renderer_generation != recorder->renderer_generation ||
        (token->renderer_generation && !recorder->renderer_active)) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_STALE_RENDERER,
                            "execution token carries a stale renderer generation", error, error_size);
    }
    *execution = MeleeWebPipelineExecutionToken{};
    execution->context = token->context;
    execution->capture_generation = token->capture_generation;
    execution->device_generation = token->device_generation;
    execution->renderer_generation = token->renderer_generation;
    execution->frame_id = token->frame_id;
    execution->packet_id = token->packet_id;
    execution->scope_id = token->scope_id;
    execution->token_nonce = token->token_nonce;
    if (recorder->next_execution_id == std::numeric_limits<uint64_t>::max()) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_OVERFLOW,
                            "execution token identity exhausted", error, error_size,
                            true);
    }
    execution->execution_id = recorder->next_execution_id++;
    execution->valid = 1;
    thread->execution[thread->execution_depth++].token = *execution;
    return clear_error(error, error_size);
}

int melee_web_pipeline_execution_pop(MeleeWebPipelineRecorder* recorder,
                                      const MeleeWebPipelineExecutionToken* execution,
                                      char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    if (!operational(recorder) || !execution || !execution->valid ||
        execution->capture_generation != recorder->capture_generation) {
        return fail_locked(recorder, execution &&
                                      (execution->capture_generation != recorder->capture_generation ||
                                       !recorder->capture_active)
                                      ? MELEE_WEB_PIPELINE_INVALID_STALE_CAPTURE
                                      : MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE,
                            "execution pop received a stale or missing execution token",
                            error, error_size);
    }
    if (!thread->execution_depth ||
        thread->execution[thread->execution_depth - 1].token.execution_id != execution->execution_id ||
        thread->execution[thread->execution_depth - 1].token.scope_id != execution->scope_id ||
        thread->execution[thread->execution_depth - 1].token.token_nonce != execution->token_nonce ||
        thread->execution[thread->execution_depth - 1].token.device_generation != execution->device_generation ||
        thread->execution[thread->execution_depth - 1].token.renderer_generation != execution->renderer_generation ||
        thread->execution[thread->execution_depth - 1].token.frame_id != execution->frame_id ||
        thread->execution[thread->execution_depth - 1].token.packet_id != execution->packet_id ||
        !context_equal(thread->execution[thread->execution_depth - 1].token.context,
                       execution->context) ||
        execution->token_nonce != token_nonce(
            execution->capture_generation, execution->scope_id, execution->context,
            execution->device_generation, execution->renderer_generation,
            execution->frame_id, execution->packet_id)) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE,
                            "execution pop is not the matching LIFO token", error, error_size);
    }
    --thread->execution_depth;
    return clear_error(error, error_size);
}

int melee_web_pipeline_observe_at(MeleeWebPipelineRecorder* recorder,
                                  uint32_t event_kind, uint32_t type,
                                  uint64_t pipeline_ref, uint32_t config_version,
                                  const void* blob, size_t blob_length,
                                  uint32_t outcome, uint64_t frame_id,
                                  uint64_t packet_id, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    return observe_locked(recorder, thread, event_kind, type, pipeline_ref,
                          config_version, blob, blob_length, outcome, frame_id,
                          packet_id, error, error_size);
}

int melee_web_pipeline_observe(MeleeWebPipelineRecorder* recorder,
                               uint32_t event_kind, uint32_t type,
                               uint64_t pipeline_ref, uint32_t config_version,
                               const void* blob, size_t blob_length,
                               uint32_t outcome, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = thread_state_locked(recorder, error, error_size);
    if (!thread) return 0;
    const uint64_t frame_id = thread->frame_active ? thread->frame_id : 0;
    const uint64_t packet_id = thread->packet_active ? thread->packet_id : 0;
    return observe_locked(recorder, thread, event_kind, type, pipeline_ref,
                          config_version, blob, blob_length, outcome, frame_id,
                          packet_id, error, error_size);
}

int melee_web_pipeline_invalidate(MeleeWebPipelineRecorder* recorder,
                                  uint32_t reason, const char* message,
                                  char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    if (reason == MELEE_WEB_PIPELINE_INVALID_NONE || reason > MELEE_WEB_PIPELINE_INVALID_THREAD_OVERFLOW) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST,
                            "pipeline provenance invalidation reason is invalid", error, error_size);
    }
    return fail_locked(recorder, reason, message && *message ? message : reason_name(reason),
                       error, error_size);
}

int melee_web_pipeline_json_drain(MeleeWebPipelineRecorder* recorder,
                                  char* output, size_t output_size,
                                  size_t* written, int final_snapshot,
                                  char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    if (written) *written = 0;
    if (!output || !written || output_size < 2) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_OUTPUT_TOO_SMALL,
                            "pipeline provenance JSON output buffer is invalid or too large",
                            error, error_size);
    }
    if (!recorder->capture_active && !recorder->capture_final &&
        recorder->capture_generation == 0) {
        return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_NOT_STARTED,
                            "pipeline provenance capture has not started", error, error_size);
    }
    if (final_snapshot) {
        if (recorder->active_scope_count) {
            /* Keep the retained evidence available even when finalization
             * discovers an unclosed source pair.  The sticky invalid status
             * makes the chunk uncertifiable while preserving open_scopes. */
            fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE,
                        "final pipeline provenance snapshot has active source scopes",
                        error, error_size);
        }
        for (const auto& thread : recorder->threads) {
            if (thread.claimed && thread.execution_depth) {
                fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE,
                            "final pipeline provenance snapshot has deferred execution scopes",
                            error, error_size);
            }
        }
    }
    const size_t pending = recorder->records.size();
    size_t selected = pending;
    std::string json;
    const size_t output_limit = std::min(output_size, static_cast<size_t>(recorder->max_json_bytes));
    while (true) {
        build_drain_json(*recorder, selected, recorder->drained_records + selected,
                         final_snapshot != 0, &json);
        if (json.size() + 1 <= output_limit) break;
        if (!selected) {
            return fail_locked(recorder, MELEE_WEB_PIPELINE_INVALID_JSON_OVERFLOW,
                                "pipeline provenance JSON chunk exceeds its bounded output",
                                error, error_size);
        }
        // Bounded geometric search avoids repeatedly serializing almost the
        // entire chunk when only a prefix fits. Remaining records stay queued.
        selected /= 2;
    }
    std::memcpy(output, json.data(), json.size());
    output[json.size()] = 0;
    *written = json.size();
    if (selected) {
        recorder->records.erase(recorder->records.begin(),
                                recorder->records.begin() + static_cast<std::ptrdiff_t>(selected));
        recorder->drained_records += selected;
    }
    if (final_snapshot) {
        recorder->capture_active = false;
        recorder->capture_final = true;
    }
    return clear_error(error, error_size);
}

int melee_web_pipeline_json_status(const MeleeWebPipelineRecorder* recorder,
                                   char* output, size_t output_size,
                                   size_t* written, char* error, size_t error_size) {
    if (!recorder) return write_error(error, error_size, "pipeline provenance recorder is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    if (written) *written = 0;
    if (!output || !written || output_size < 2 || output_size > recorder->max_json_bytes) {
        return write_error(error, error_size, "pipeline provenance JSON status buffer is invalid");
    }
    std::string json;
    json += "{\"schema\":\"" MELEE_WEB_PIPELINE_PROVENANCE_SCHEMA
            "\",\"version\":1,\"capture_generation\":";
    append_u64(json, recorder->capture_generation);
    json += ",\"status\":";
    append_status(json, *recorder, static_cast<uint32_t>(recorder->records.size()),
                  recorder->drained_records, recorder->capture_active,
                  recorder->capture_final);
    json.push_back('}');
    if (json.size() + 1 >
        std::min(output_size, static_cast<size_t>(recorder->max_json_bytes))) {
        return write_error(error, error_size,
                           "pipeline provenance JSON status exceeds its bounded output");
    }
    std::memcpy(output, json.data(), json.size());
    output[json.size()] = 0;
    *written = json.size();
    return clear_error(error, error_size);
}

int melee_web_pipeline_status(const MeleeWebPipelineRecorder* recorder,
                              MeleeWebPipelineStatus* status,
                              char* error, size_t error_size) {
    if (!recorder || !status) return write_error(error, error_size, "pipeline provenance status output is missing");
    std::lock_guard<std::mutex> lock(recorder->mutex);
    ThreadState* thread = nullptr;
    const auto current = std::this_thread::get_id();
    for (auto& candidate : const_cast<MeleeWebPipelineRecorder*>(recorder)->threads) {
        if (candidate.claimed && candidate.thread_id == current) {
            thread = &candidate;
            break;
        }
    }
    copy_status(*recorder, status, thread);
    return clear_error(error, error_size);
}

} // extern "C"

#endif // MELEE_WEB_PIPELINE_PROVENANCE
