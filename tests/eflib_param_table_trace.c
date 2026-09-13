#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int32_t s32;
typedef uint8_t u8;

typedef struct HSD_GObj {
    uint32_t id;
} HSD_GObj;

typedef struct EF_ParamEntry {
    void* gobj;
    uint16_t gfx_id;
    uint16_t alpha;
} EF_ParamEntry;

/* Keep these as distinct objects. The regression is that the setters must
 * address the parameter table by name, rather than relying on queue
 * adjacency in the linker layout. */
EF_ParamEntry efLib_AnimQueue[0x10];
uint32_t gFtDataList_neighbor[4];
EF_ParamEntry efLib_ParamTable[0x8];

/* The two setters from the patched source are inserted above main. */
/* EFLIB_PARAM_SETTERS */

static int check(int condition, const char* message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        return 0;
    }
    return 1;
}

static int equal_queue(const EF_ParamEntry* expected)
{
    return memcmp(efLib_AnimQueue, expected, sizeof(efLib_AnimQueue)) == 0;
}

int main(void)
{
    HSD_GObj target = { 17 };
    HSD_GObj other = { 23 };
    EF_ParamEntry queue_before[0x10];
    uint32_t neighbor_before[4];

    for (unsigned i = 0; i < 0x10; ++i) {
        efLib_AnimQueue[i].gobj = (void*) (uintptr_t) (0x1000u + i * 4u);
        efLib_AnimQueue[i].gfx_id = (uint16_t) (0x300u + i);
        efLib_AnimQueue[i].alpha = (uint16_t) (0x400u + i);
    }
    for (unsigned i = 0; i < 4; ++i) {
        gFtDataList_neighbor[i] = 0x895FA90u + i;
    }
    for (unsigned i = 0; i < 8; ++i) {
        efLib_ParamTable[i].gobj = NULL;
        efLib_ParamTable[i].gfx_id = (uint16_t) (0x500u + i);
        efLib_ParamTable[i].alpha = (uint16_t) (0x600u + i);
    }
    memcpy(queue_before, efLib_AnimQueue, sizeof(queue_before));
    memcpy(neighbor_before, gFtDataList_neighbor, sizeof(neighbor_before));

    efLib_SetParamAlpha(&target, 0x37);
    if (!check(efLib_ParamTable[0].gobj == &target &&
                   efLib_ParamTable[0].alpha == 0x37 &&
                   efLib_ParamTable[0].gfx_id == 0x500,
               "SetParamAlpha did not update the first real parameter entry") ||
        !check(equal_queue(queue_before),
               "SetParamAlpha changed the animation queue") ||
        !check(memcmp(gFtDataList_neighbor, neighbor_before,
                      sizeof(neighbor_before)) == 0,
               "SetParamAlpha changed a neighboring global")) {
        return 1;
    }

    efLib_SetParamGfxId(&target, 0x417);
    if (!check(efLib_ParamTable[0].gobj == &target &&
                   efLib_ParamTable[0].gfx_id == 0x417 &&
                   efLib_ParamTable[0].alpha == 0x37,
               "SetParamGfxId did not update the existing real parameter entry") ||
        !check(equal_queue(queue_before),
               "SetParamGfxId changed the animation queue") ||
        !check(memcmp(gFtDataList_neighbor, neighbor_before,
                      sizeof(neighbor_before)) == 0,
               "SetParamGfxId changed a neighboring global")) {
        return 1;
    }

    efLib_SetParamGfxId(&other, 0x419);
    if (!check(efLib_ParamTable[1].gobj == &other &&
                   efLib_ParamTable[1].gfx_id == 0x419 &&
                   efLib_ParamTable[1].alpha == 0x601,
               "SetParamGfxId did not allocate the next real parameter entry") ||
        !check(equal_queue(queue_before),
               "SetParamGfxId changed the animation queue on a new entry") ||
        !check(memcmp(gFtDataList_neighbor, neighbor_before,
                      sizeof(neighbor_before)) == 0,
               "SetParamGfxId changed a neighboring global on a new entry")) {
        return 1;
    }

    puts("efLib parameter setters preserve queue and update ParamTable: passed");
    return 0;
}
