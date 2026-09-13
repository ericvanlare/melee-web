#include <sysdolphin/baselib/spline.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The controls and arc table are the type-0 spline in the pinned GrSt.dat.
 * GrSt.dat SHA-256: 1ef0ccc51fc69bf2e06f55377111ec1b67e00438df0597bf6195c3032ae29f83
 * descriptor: data+0x18518; controls: data+0x18448; arc table: data+0x184e4.
 * The captured translations are from moving-diagnostic.jsonl, SHA-256:
 * 3130e36ecceb55e28ed1638300f7398ae09b7563de76eb45124cea691cf70fe7.
 * Retail main.dol SHA-1: 08e0bf20134dfcb260699671004527b2d6bb1a45.
 */

typedef struct {
    int index;
    uint32_t time_bits;
    uint32_t output[3];
} SplineCase;

static float from_bits(uint32_t value)
{
    float result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static uint32_t bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

/* The type-0 source branch only needs this symbol when the rest of spline.c
 * is linked into the small harness. None of the cases below enters a
 * polynomial arc-length branch. */
float sqrtf__Ff(float value)
{
    return __builtin_sqrtf(value);
}

static uint32_t read_be32(const uint8_t* bytes)
{
    return ((uint32_t) bytes[0] << 24) | ((uint32_t) bytes[1] << 16) |
           ((uint32_t) bytes[2] << 8) | (uint32_t) bytes[3];
}

static uint16_t read_be16(const uint8_t* bytes)
{
    return (uint16_t) (((uint16_t) bytes[0] << 8) | bytes[1]);
}

static int load_spline(HSD_Spline* spline, const char* path)
{
    enum { HeaderSize = 0x20, DescriptorOffset = 0x18518 };
    FILE* file = fopen(path, "rb");
    uint8_t* bytes;
    long file_size;
    uint32_t data_size;
    uint32_t controls_offset;
    uint32_t segments_offset;
    uint32_t expected_size;
    static Vec3 controls[13];
    static float segments[13];

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (file_size = ftell(file)) < HeaderSize || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL)
            fclose(file);
        return 0;
    }
    bytes = (uint8_t*) malloc((size_t) file_size);
    if (bytes == NULL || fread(bytes, 1, (size_t) file_size, file) != (size_t) file_size) {
        free(bytes);
        fclose(file);
        return 0;
    }
    fclose(file);
    data_size = read_be32(bytes + 4);
    expected_size = read_be32(bytes);
    if (expected_size != (uint32_t) file_size ||
        data_size > (uint32_t) file_size - HeaderSize ||
        DescriptorOffset + 24 > data_size) {
        free(bytes);
        return 0;
    }
    if (bytes[HeaderSize + DescriptorOffset] != 0 ||
        read_be16(bytes + HeaderSize + DescriptorOffset + 2) != 13) {
        free(bytes);
        return 0;
    }
    controls_offset = read_be32(bytes + HeaderSize + DescriptorOffset + 8);
    segments_offset = read_be32(bytes + HeaderSize + DescriptorOffset + 16);
    if (controls_offset > data_size - 13 * 12 ||
        segments_offset > data_size - 13 * 4) {
        free(bytes);
        return 0;
    }
    for (int i = 0; i < 13; ++i) {
        const uint8_t* control = bytes + HeaderSize + controls_offset + i * 12;
        controls[i].x = from_bits(read_be32(control));
        controls[i].y = from_bits(read_be32(control + 4));
        controls[i].z = from_bits(read_be32(control + 8));
        segments[i] = from_bits(read_be32(bytes + HeaderSize + segments_offset + i * 4));
    }
    spline->type = 0;
    spline->numcv = 13;
    spline->tension = 0.0f;
    spline->cv = controls;
    spline->totalLength = from_bits(read_be32(bytes + HeaderSize + DescriptorOffset + 12));
    spline->segLength = segments;
    spline->segPoly = NULL;
    free(bytes);
    return 1;
}

/* Deliberately separate multiply/add arithmetic: this is the pre-patch
 * fallback and must differ on at least one captured parameter. */
static void unfused_point(Vec3* result, const HSD_Spline* spline, float u)
{
    float scaled = u * (float) (spline->numcv - 1);
    int index = (int) scaled;
    float t = scaled - (float) index;
    const Vec3* first = &spline->cv[index];
    volatile float x_product = t * (first[1].x - first[0].x);
    volatile float y_product = t * (first[1].y - first[0].y);
    volatile float z_product = t * (first[1].z - first[0].z);
    result->x = x_product + first[0].x;
    result->y = y_product + first[0].y;
    result->z = z_product + first[0].z;
}

int main(int argc, char** argv)
{
    static const SplineCase cases[] = {
        {5916, 0x448ba000, {0x434872fa, 0xc2950008, 0x00000000}},
        {5917, 0x448bc000, {0x4348f4bf, 0xc2950009, 0x00000000}},
        {5918, 0x448be000, {0x43497685, 0xc2950009, 0x00000000}},
        {5919, 0x448c0000, {0x4349f853, 0xc2950009, 0x00000000}},
        {5967, 0x44920000, {0x43567fff, 0xc2761dc7, 0x00000000}},
        {5972, 0x4492a000, {0x43568000, 0xc26bfa5d, 0x00000000}},
        {5975, 0x44930000, {0x43568000, 0xc265e517, 0x00000000}},
        {6125, 0x42fc0000, {0x431420c3, 0xc23a000c, 0x00000000}},
        {6126, 0x42fe0000, {0x43139efd, 0xc23a000c, 0x00000000}},
    };
    HSD_Spline spline;
    int matched = 0;
    if (argc != 2 || !load_spline(&spline, argv[1])) {
        fprintf(stderr, "cannot load pinned GrSt.dat\n");
        return 3;
    }
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Vec3 result;
        const float time = from_bits(cases[i].time_bits);
        /* Raw FObj fields are p0=0, d0=0x3a5a740e (1/1200), and the
         * production FObj path uses the same rounded fused update. */
        const float path_parameter = __builtin_fmaf(from_bits(0x3a5a740e),
                                                    time, 0.0f);
        splArcLengthPoint(&result, &spline, path_parameter);
        if (bits(result.x) != cases[i].output[0] ||
            bits(result.y) != cases[i].output[1] ||
            bits(result.z) != cases[i].output[2]) {
            fprintf(stderr, "case %d: %08x %08x %08x\n", cases[i].index,
                    bits(result.x), bits(result.y), bits(result.z));
            return 1;
        }
        matched++;
    }

    /* Use the exact reconstructed parameter for the ordinary linear
     * function, then ensure the patched source's fused lerp is observable. */
    {
        const float time = from_bits(cases[3].time_bits);
        const float u = __builtin_fmaf(from_bits(0x3a5a740e), time, 0.0f);
        Vec3 fused;
        Vec3 unfused;
        splGetSplinePoint(&fused, &spline, splArcLengthGetParameter(&spline, u));
        unfused_point(&unfused, &spline, splArcLengthGetParameter(&spline, u));
        if (bits(fused.x) == bits(unfused.x) && bits(fused.y) == bits(unfused.y) &&
            bits(fused.z) == bits(unfused.z)) {
            fprintf(stderr, "unfused negative control unexpectedly matched\n");
            return 2;
        }
    }

    printf("spline-linear cases=%d retail=all fused=x-y-z negative=diff\n", matched);
    return 0;
}
