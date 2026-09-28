#include "gameplay_thp_cpu.h"

#include "gameplay_platform.h"
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

enum { THP_CPU_CACHE_BYTES = 0x4000 };

static _Alignas(32) uint8_t cache[THP_CPU_CACHE_BYTES];
static int active;
static uintptr_t output_begin;
static size_t output_size;

static int contains(uintptr_t base, size_t extent, const void* pointer,
                    size_t size)
{
    const uintptr_t address = (uintptr_t) pointer;
    if (!pointer || address > UINTPTR_MAX - size || base > UINTPTR_MAX - extent)
        return 0;
    return address >= base && address - base <= extent &&
           size <= extent - (address - base);
}

int melee_web_thp_cpu_begin(void)
{
    if (active || output_begin || output_size) return 0;
    memset(cache, 0, sizeof(cache));
    active = 1;
    return 1;
}

void* melee_web_thp_cpu_scratch(void)
{
    return active ? cache : NULL;
}

int melee_web_thp_cpu_output_register(void* output, size_t size)
{
    if (!active || output_begin || output_size || !output || !size ||
        ((uintptr_t) output & 31u) || size > UINTPTR_MAX - (uintptr_t) output)
        return 0;
    output_begin = (uintptr_t) output;
    output_size = size;
    return 1;
}

int melee_web_thp_cpu_output_unregister(void* output, size_t size)
{
    if (!active || !output_begin || output_begin != (uintptr_t) output ||
        output_size != size)
        return 0;
    output_begin = 0;
    output_size = 0;
    return 1;
}

int melee_web_thp_cpu_store_valid(const void* destination, const void* source,
                                  uint32_t size)
{
    return active && output_begin && size && !(size & 31u) &&
           !((uintptr_t) destination & 31u) &&
           !((uintptr_t) source & 31u) &&
           contains((uintptr_t) cache, sizeof(cache), source, size) &&
           contains(output_begin, output_size, destination, size);
}

int melee_web_thp_cpu_zero_valid(const void* destination, uint32_t size)
{
    return active && output_begin && size &&
           contains(output_begin, output_size, destination, size);
}

int melee_web_thp_cpu_wait_valid(uint32_t length)
{
    return active && length <= 3;
}

uint32_t melee_web_thp_cpu_store(void* destination, void* source,
                                 uint32_t size)
{
    if (!melee_web_thp_cpu_store_valid(destination, source, size))
        melee_web_platform_unavailable("unowned THP CPU cache store");
    memcpy(destination, source, size);
    atomic_thread_fence(memory_order_seq_cst);
    return size;
}

void melee_web_thp_cpu_zero(void* destination, uint32_t size)
{
    if (!melee_web_thp_cpu_zero_valid(destination, size))
        melee_web_platform_unavailable("unowned THP CPU range zero");
    memset(destination, 0, size);
}

void melee_web_thp_cpu_wait(uint32_t length)
{
    if (!melee_web_thp_cpu_wait_valid(length))
        melee_web_platform_unavailable("unsupported THP CPU queue wait");
    atomic_thread_fence(memory_order_seq_cst);
}

int melee_web_thp_cpu_end(void)
{
    if (!active || output_begin || output_size) return 0;
    memset(cache, 0, sizeof(cache));
    active = 0;
    return 1;
}
