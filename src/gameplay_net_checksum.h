#ifndef MELEE_WEB_GAMEPLAY_NET_CHECKSUM_H
#define MELEE_WEB_GAMEPLAY_NET_CHECKSUM_H

/* Per-tick state checksums for the opt-in networked-session diagnostics.
 * Every function only reads source state. Nothing here is compiled into the
 * public or audio-preview players. */

#include <stddef.h>
#include <stdint.h>
#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebMenuHost MeleeWebMenuHost;

enum {
    MELEE_WEB_NET_CHECKSUM_RING = 8192,
    MELEE_WEB_NET_ARENA_RECORDS = 64,
};

/* Component flags recorded with each tick. */
enum {
    MELEE_WEB_NET_HASHED_FIGHTERS = 1u << 0,
    MELEE_WEB_NET_HASHED_CSS = 1u << 1,
    MELEE_WEB_NET_HASHED_SSS = 1u << 2,
    MELEE_WEB_NET_HASHED_OBJECTS = 1u << 3,
};

/* 64-byte little-endian record exported to the harness. `total` is one
 * FNV-1a64 pass over the header fields (tick, scene, seed, frame count)
 * followed by the PAD, master-status and scene-state component bytes in order.
 * Those components are also hashed separately so that a first divergence names
 * its channel. `object_state` is a supplementary channel that never enters
 * `total`: it covers every GObj's link identity and root transform. */
typedef struct MeleeWebNetChecksumRecord {
    uint32_t tick;
    uint32_t scene;
    uint32_t seed;
    uint32_t frame;
    uint32_t flags;
    uint32_t objects;
    uint64_t input;
    uint64_t pad;
    uint64_t scene_state;
    uint64_t object_state;
    uint64_t total;
} MeleeWebNetChecksumRecord;

typedef struct MeleeWebNetArenaRecord {
    uint32_t tick;
    uint32_t scene;
    uint32_t base;
    uint32_t bytes;
    uint64_t hash;
} MeleeWebNetArenaRecord;

uint64_t melee_web_net_fnv1a64(uint64_t hash, const void* bytes, size_t size);

/* Compute one tick record after the source owner consumed `pads`. The host
 * is used only for its session-owned CSS/SSS selection payload. */
void melee_web_net_checksum_compute(uint32_t tick, uint32_t scene,
                                    const PADStatus pads[4],
                                    const MeleeWebMenuHost* host,
                                    MeleeWebNetChecksumRecord* out);

/* Hash the session arena as little-endian u64 words (FNV-1a64 word variant).
 * Returns 0 when no session arena is owned. */
int melee_web_net_arena_hash(uint32_t* base, uint32_t* bytes, uint64_t* hash);

#ifdef __cplusplus
}
#endif

#endif
