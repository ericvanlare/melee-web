/* Retain the original allocation fixture's checked boot/heap construction and
 * continue in the same fresh process through the joined audio service owner. */
#define main melee_web_source_hsd_prefix_main
#include "original_startup_alloc_trace.cpp"
#undef main
#include "source_synth_joined_services.h"
#include <climits>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 7) fail("expected mode, four source-derived driver arguments, SRAM settings path, then boot argument pairs");
    int driver[4];
    for (unsigned i = 0; i < 4; ++i) {
        const u32 value = number(argv[i + 2], "source-derived audio argument");
        if (value > INT_MAX) fail("audio argument is outside original signed-int range");
        driver[i] = static_cast<int>(value);
    }
    unsigned char settings[64];
    FILE* input = std::fopen(argv[6], "rb");
    if (!input) fail("owned SRAM settings file is unavailable");
    const size_t bytes = std::fread(settings, 1, sizeof(settings), input);
    const int tail = std::fgetc(input);
    const bool read_failed = std::ferror(input) != 0;
    std::fclose(input);
    if (bytes != sizeof(settings) || tail != EOF || read_failed)
        fail("SRAM settings must be exactly 64 independently owned bytes");
    std::vector<char*> prefix_arguments{argv[0]};
    for (int i = 7; i < argc; ++i) prefix_arguments.push_back(argv[i]);
    const int prefix_argc = static_cast<int>(prefix_arguments.size());
    prefix_arguments.push_back(nullptr);
    const int status = melee_web_source_hsd_prefix_main(prefix_argc, prefix_arguments.data());
    if (status) return status;
    return melee_web_source_synth_joined_run(driver[0], driver[1], driver[2], driver[3],
                                            settings, sizeof(settings), argv[1]);
}
