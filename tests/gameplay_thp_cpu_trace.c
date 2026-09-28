#include "gameplay_thp_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void melee_web_platform_unavailable(const char* operation)
{
    fprintf(stderr, "unexpected unsupported platform call: %s\n", operation);
    abort();
}

static void require(int condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "THP CPU ownership trace: %s\n", message);
        exit(1);
    }
}

int main(void)
{
    _Alignas(32) unsigned char output[256] = {0};
    _Alignas(32) unsigned char outside[64] = {0};
    require(!melee_web_thp_cpu_scratch(), "inactive scope exposed scratch memory");
    require(melee_web_thp_cpu_begin(), "MoviePlayer CPU-cache scope did not begin");
    require(!melee_web_thp_cpu_begin(), "a second CPU-cache owner was accepted");
    require(melee_web_thp_cpu_scratch() != NULL, "owned scratch was unavailable");
    require(melee_web_thp_cpu_output_register(output, sizeof(output)),
            "decoder output region did not register");
    require(!melee_web_thp_cpu_output_register(outside, sizeof(outside)),
            "a second decoder output region was accepted");

    unsigned char* scratch = melee_web_thp_cpu_scratch();
    for (size_t i = 0; i < 64; ++i) scratch[i] = (unsigned char)(i * 11 + 3);
    require(melee_web_thp_cpu_store_valid(output, scratch, 64),
            "an owned aligned cache store was rejected");
    require(!melee_web_thp_cpu_store_valid(outside, scratch, 64),
            "a store escaped its registered decoder output");
    require(!melee_web_thp_cpu_store_valid(output, outside, 64),
            "a store read outside its CPU locked-cache workspace");
    require(!melee_web_thp_cpu_store_valid(output, scratch, 33),
            "an unaligned byte count was accepted for LCStoreData");
    require(melee_web_thp_cpu_store(output, scratch, 64) == 64,
            "owned CPU cache store did not complete");
    require(memcmp(output, scratch, 64) == 0,
            "CPU cache store changed authored bytes");
    require(melee_web_thp_cpu_zero_valid(output + 64, 64),
            "owned decoder zero range was rejected");
    require(!melee_web_thp_cpu_zero_valid(outside, 64),
            "decoder zero escaped its registered output");
    melee_web_thp_cpu_zero(output + 64, 64);
    for (size_t i = 64; i < 128; ++i)
        require(output[i] == 0, "DCZeroRange did not zero its owned bytes");
    require(melee_web_thp_cpu_wait_valid(3) &&
            !melee_web_thp_cpu_wait_valid(4),
            "locked-cache queue wait did not enforce the source's queue bound");
    melee_web_thp_cpu_wait(3);
    require(!melee_web_thp_cpu_end(), "scope closed with a registered output");
    require(melee_web_thp_cpu_output_unregister(output, sizeof(output)),
            "decoder output region did not release");
    require(!melee_web_thp_cpu_output_unregister(output, sizeof(output)),
            "decoder output release was accepted twice");
    require(melee_web_thp_cpu_end(), "CPU-cache owner did not close");
    require(melee_web_thp_cpu_scratch() == NULL,
            "closed scope retained CPU scratch access");
    puts("Original THP CPU cache stores, range ownership, and teardown passed");
    return 0;
}
