#ifndef MELEE_WEB_SIS_STREAM_H
#define MELEE_WEB_SIS_STREAM_H
#include <stdint.h>

/* SIS programs and the source-written style stack are byte-encoded in
 * big-endian order. Unaligned host casts change their meaning in Wasm. */
static inline uint16_t melee_web_sis_u16(const void* data) {
    const uint8_t* p = data;
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static inline int16_t melee_web_sis_s16(const void* data) {
    uint16_t value = melee_web_sis_u16(data);
    return (int16_t)(value < 0x8000 ? value : (int32_t)value - 0x10000);
}
static inline uint32_t melee_web_sis_u32(const void* data) {
    const uint8_t* p = data;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}
#endif
