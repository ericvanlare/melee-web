/* Direct original ARAlloc/ARFree/ARGetSize differential fixture. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../.deps/melee/extern/dolphin/src/dolphin/ar/ar.c"

static u32 oracle_base;
static u32 oracle_capacity;
static u32 oracle_hardware_size;
static u32 oracle_lengths[256];

/* ARInit executes hardware/MMIO probing. Set the original TU's private
 * allocator globals directly so only the stack operations under test run. */
static void oracle_setup(u32 base, u32 capacity, u32 hardware_size)
{
    memset(oracle_lengths, 0, sizeof(oracle_lengths));
    oracle_base = base;
    oracle_capacity = capacity;
    oracle_hardware_size = hardware_size;
    __AR_Callback = NULL;
    __AR_Size = hardware_size;
    __AR_StackPointer = base;
    __AR_FreeBlocks = capacity;
    __AR_BlockLength = oracle_lengths;
    __AR_init_flag = 1;
}

static u32 offset(u32 address)
{
    return address ? address - oracle_base : 0;
}

static unsigned active_count(void)
{
    return oracle_capacity - __AR_FreeBlocks;
}

static void emit_state(const char* op, const char* status,
                       u32 address, u32 size, int has_result)
{
    unsigned count = active_count();
    u32 current = oracle_base;
    unsigned i;
    printf("{\"op\":\"%s\",\"status\":\"%s\"", op, status);
    if (has_result)
        printf(",\"address\":%u,\"size\":%u", offset(address), size);
    printf(",\"initialized\":%s,\"stack\":%u,\"capacity\":%u,\"free_blocks\":%u,\"hardware_size\":%u,\"allocations\":[",
           __AR_init_flag ? "true" : "false", offset(__AR_StackPointer),
           oracle_capacity, __AR_FreeBlocks,
           ARGetSize() - oracle_base);
    for (i = 0; i < count; ++i) {
        printf("%s{\"address\":%u,\"size\":%u}",
               i ? "," : "", offset(current), oracle_lengths[i]);
        current += oracle_lengths[i];
    }
    puts("]}");
    fflush(stdout);
}

static unsigned parse_u32(const char* text)
{
    char* end = NULL;
    const unsigned long long value = strtoull(text, &end, 0);
    if (!end || *end || value > 0xffffffffULL) abort();
    return (unsigned)value;
}

int main(int argc, char** argv)
{
    char line[256];
    if (argc != 4) return 2;
    oracle_base = parse_u32(argv[1]);
    oracle_capacity = parse_u32(argv[2]);
    oracle_hardware_size = parse_u32(argv[3]);
    memset(oracle_lengths, 0, sizeof(oracle_lengths));
    __AR_Size = 0;
    __AR_StackPointer = 0;
    __AR_FreeBlocks = 0;
    __AR_BlockLength = oracle_lengths;
    __AR_init_flag = 0;
    while (fgets(line, sizeof line, stdin)) {
        char op = 0, first[64] = {}, second[64] = {};
        const int count = sscanf(line, " %c %63s %63s", &op, first, second);
        if (count < 1 || op == '#') continue;
        if (op == 'i') {
            if (__AR_init_flag) {
                emit_state("init", "already_initialized", 0, 0, 0);
            } else {
                oracle_setup(oracle_base, oracle_capacity, oracle_hardware_size);
                emit_state("init", "ok", 0, 0, 0);
            }
        } else if (op == 'a') {
            u32 length;
            u32 address;
            if (count != 3) return 2;
            length = parse_u32(second);
            address = ARAlloc(length);
            emit_state("alloc", "ok", address, length, 1);
        } else if (op == 'f') {
            u32 length = 0;
            const u32 address = ARFree(&length);
            emit_state("free", "ok", address, length, 1);
        } else if (op == 'g') {
            (void)ARGetSize();
            emit_state("get_size", "ok", 0, 0, 0);
        } else {
            return 2;
        }
    }
    return 0;
}
