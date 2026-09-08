#include "gameplay_bootstrap.h"
#include "gameplay_fighter_probe.h"
#include "dat_common.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <vector>

// The original global is never left pointing at this probe's partial context.
struct ftCommonData;
extern "C" ftCommonData* p_ftCommonData;
static char error[256];
static void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s: %s\n", message, error); std::exit(1); }
}
static MeleeWebFighterInputProbe* create(float threshold)
{
    MeleeWebCommonScalars scalars{};
    scalars.walk_stick_threshold = threshold;
    auto* probe = melee_web_fighter_input_probe_create(&scalars, error, sizeof(error));
    check(probe, "original Fighter/GObj storage allocation");
    return probe;
}
static MeleeWebFighterInputResult sample(MeleeWebFighterInputProbe* probe, float stick, float facing)
{
    MeleeWebFighterInputResult result{};
    check(melee_web_fighter_input_probe_sample(probe, stick, facing, &result, error, sizeof(error)),
          "original walk threshold predicate");
    return result;
}
static void reset_state(const MeleeWebFighterInputResult& result)
{
    check(result.stick_x == 0 && result.held_buttons == 0 && result.pressed_buttons == 0 &&
          result.released_buttons == 0 && result.stick_x_timer == 0xfe && result.stick_y_timer == 0xfe &&
          result.trigger_timer == 0xfe, "original input reset zeroes input and sets source timer sentinels");
}
static void destroy(MeleeWebFighterInputProbe* probe)
{
    check(melee_web_fighter_input_probe_destroy(probe, error, sizeof(error)), "fighter owner destruction");
}
int main(int argc, char** argv)
{
    MeleeWebCommonScalars common{};
    common.walk_stick_threshold = .3f;
    check(!melee_web_fighter_input_probe_create(&common, error, sizeof(error)), "probe rejects missing world");
    check(melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)), "world startup");
    const auto generation = melee_web_gameplay_stats().generation;
    auto* first = create(.3f);
    auto* second = create(.7f);
    check(melee_web_gameplay_stats().objects == 2, "each probe owns an actual source GObj");
    MeleeWebFighterInputResult result{};
    check(melee_web_fighter_input_probe_read(first, &result, error, sizeof(error)), "read reset source state");
    reset_state(result);
    check(!result.can_walk && result.walk_threshold == .3f, "zero input does not enter walk");
    check(!sample(first, std::nextafter(.3f, 0.f), 1.f).can_walk, "adjacent float below threshold rejects");
    check(sample(first, .3f, 1.f).can_walk, "exact threshold enters walk");
    check(sample(first, -.3f, -1.f).can_walk, "source facing sign reverses the threshold");
    check(!sample(first, .3f, -1.f).can_walk, "input away from facing does not enter forward walk");
    check(sample(first, .5f, 1.f).can_walk && !sample(second, .5f, 1.f).can_walk,
          "owned common contexts do not leak between source consumers");
    check(p_ftCommonData == nullptr, "partial common roots are not published after consumption");
    // A real preexisting global also survives scoped consumption unchanged.
    alignas(4) unsigned char existing_context[0x818]{};
    p_ftCommonData = reinterpret_cast<ftCommonData*>(existing_context);
    check(sample(first, .5f, 1.f).can_walk, "scoped original consumer with prior context");
    check(p_ftCommonData == reinterpret_cast<ftCommonData*>(existing_context), "prior global context restored");
    p_ftCommonData = nullptr;
    const float invalid[] = {std::numeric_limits<float>::quiet_NaN(), 1.01f, -1.01f};
    for (float value : invalid)
        check(!melee_web_fighter_input_probe_sample(first, value, 1.f, &result, error, sizeof(error)),
              "invalid normalized input rejects");
    check(!melee_web_fighter_input_probe_sample(first, .8f, 0.f, &result, error, sizeof(error)),
          "invalid facing rejects");
    check(melee_web_fighter_input_probe_read(first, &result, error, sizeof(error)) && result.stick_x == .5f,
          "rejected samples leave original state unchanged");
    check(melee_web_fighter_input_probe_reset(first, error, sizeof(error)), "original source input reset");
    check(melee_web_fighter_input_probe_read(first, &result, error, sizeof(error)), "read reset state");
    reset_state(result);
    destroy(second);
    check(melee_web_gameplay_stats().objects == 1, "source user-data destructor frees one owned fighter");

    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        const auto size = file.tellg();
        check(file && size > 0 && size <= melee_web::DatArchive::max_archive_bytes, "bounded local PlCo input");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        check(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))), "read local PlCo");
        const melee_web::DatArchive archive(bytes);
        const melee_web::DatCommon decoded(archive);
        auto* actual = melee_web_fighter_input_probe_create(&decoded.scalars, error, sizeof(error));
        check(actual, "decoded typed root0 reaches original consumer");
        const float threshold = decoded.scalars.walk_stick_threshold;
        check(threshold > 0 && threshold <= 1 && sample(actual, threshold, 1.f).can_walk &&
              !sample(actual, std::nextafter(threshold, 0.f), 1.f).can_walk,
              "real decoded threshold preserves exact original comparison boundary");
        destroy(actual);
        std::puts("Local PlCo typed root0 consumed by original walk predicate: passed");
    }
    check(melee_web_gameplay_shutdown(error, sizeof(error)), "world destroys live fighter through original userdata callback");
    check(!melee_web_fighter_input_probe_read(first, &result, error, sizeof(error)), "surviving owner is invalid after world destruction");
    check(melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)), "fresh world startup");
    check(melee_web_gameplay_stats().generation != generation, "fresh world invalidates prior private pools");
    auto* restarted = create(.2f);
    check(sample(restarted, .2f, 1.f).can_walk, "restarted original pool has no stale arena pointers");
    destroy(first);
    check(melee_web_gameplay_stats().objects == 1, "destroying old owner leaves restarted world intact");
    destroy(restarted);
    check(melee_web_gameplay_shutdown(error, sizeof(error)), "final world shutdown");
    check(p_ftCommonData == nullptr, "no partial common context outlives probe");
    std::puts("Original Fighter input consumer and lifetime trace: passed");
}
