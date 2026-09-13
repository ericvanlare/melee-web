#include "gameplay_rumble.h"
#include <melee/lb/types.h>
#include <stdio.h>

struct MeleeWebRumble {
    struct Fighter_804D653C_t* rows;
    struct Fighter_804D653C_t* previous;
};
extern struct Fighter_804D653C_t* melee_web_rumble_exchange(struct Fighter_804D653C_t*);
static MeleeWebRumble* active;

MeleeWebRumble* melee_web_rumble_decode(const MeleeWebNativeDat* r,
                                       uint32_t root, unsigned rows)
{
    if (!rows || rows > 256) r->reject(r->context, "Invalid rumble table length");
    r->region(r->context, root, rows * 8);
    MeleeWebRumble* h = r->allocate(r->context, 1, sizeof(*h));
    h->rows = r->allocate(r->context, rows, sizeof(*h->rows));
    for (unsigned i = 0; i < rows; ++i) {
        uint32_t at = r->pointer(r->context, root + i * 8, 2);
        if (at == UINT32_MAX || (at & 1) || at >= root)
            r->reject(r->context, "Missing or unaligned rumble program");
        uint32_t end = root;
        for (unsigned j = 0; j < rows; ++j) {
            uint32_t other = r->pointer(r->context, root + j * 8, 2);
            if (other > at && other < end) end = other;
        }
        uint16_t words[1024];
        unsigned count = 0, loop_start = 0;
        int has_wait = 0, loop_wait = 0, has_loop = 0;
        for (;;) {
            if (count == 1024 || at + count * 2 >= end)
                r->reject(r->context, "Rumble program exceeds bounded command count");
            uint16_t word = r->half(r->context, at + count * 2);
            unsigned op = word >> 13, operand = word & 0x1fff;
            words[count++] = word;
            if (op == 0) {
                if (!has_wait)
                    r->reject(r->context, "Rumble program cannot advance a frame");
                break;
            }
            if (op >= 6)
                r->reject(r->context, "Unknown source rumble opcode");
            if (op <= 3 && operand) has_wait = loop_wait = 1;
            if (op == 4) {
                /* Original interpreter has one loop register, not a stack. */
                has_loop = 1; loop_start = count; loop_wait = 0;
            }
            if (op == 5 && (!has_loop || loop_start == count - 1 || !loop_wait))
                r->reject(r->context, "Rumble loop cannot advance a frame");
        }
        uint16_t* native = r->allocate(r->context, count, sizeof(*native));
        for (unsigned j = 0; j < count; ++j) native[j] = words[j];
        h->rows[i].unk = native;
        h->rows[i].unk4 = r->byte(r->context, root + i * 8 + 4);
        h->rows[i].unk5 = r->byte(r->context, root + i * 8 + 5);
    }
    return h;
}

int melee_web_rumble_begin(MeleeWebRumble* h, char* e, size_t n)
{
    if (!h || active) {
        if (e && n) snprintf(e, n, "Rumble data owner missing or already active");
        return 0;
    }
    h->previous = melee_web_rumble_exchange(h->rows); active = h;
    return 1;
}

int melee_web_rumble_end(MeleeWebRumble* h, char* e, size_t n)
{
    if (!h || h != active) {
        if (e && n) snprintf(e, n, "Rumble data publication lost ownership");
        return 0;
    }
    melee_web_rumble_exchange(h->previous); active = NULL;
    return 1;
}
