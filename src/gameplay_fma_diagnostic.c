#include "gameplay_fma_diagnostic.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FmaSite {
    const char* pc;
    const char* name;
} FmaSite;

#ifdef MELEE_WEB_FMA_COLLISION_DIAGNOSTIC
static const FmaSite collision_sites[MELEE_WEB_FMA_COLLISION_SITE_COUNT] = {
    {"80006e80", "broadphase_radius"},
    {"80007130", "segment_dot_xy"},
    {"80007140", "segment_dot_z"},
    {"80007134", "hurt_length_xy"},
    {"80007144", "hurt_length_z"},
    {"80007160", "hit_start_dot_xy"},
    {"8000716c", "hit_start_dot_z"},
    {"80007164", "hurt_start_dot_xy"},
    {"80007170", "hurt_start_dot_z"},
    {"80007174", "closest_denominator"},
    {"80007280", "parallel_start_distance_x"},
    {"8000728c", "parallel_start_distance_z"},
    {"80007288", "parallel_end_distance_x"},
    {"80007290", "parallel_end_distance_z"},
    {"800072ec", "endpoint_start_denominator_x"},
    {"80007300", "endpoint_start_denominator_z"},
    {"80007320", "endpoint_start_dot_x"},
    {"80007324", "endpoint_start_dot_z"},
    {"800073a8", "endpoint_end_denominator_x"},
    {"800073bc", "endpoint_end_denominator_z"},
    {"800073dc", "endpoint_end_dot_x"},
    {"800073e0", "endpoint_end_dot_z"},
    {"80007420", "hit_param_numerator"},
    {"80007424", "hurt_param_numerator"},
    {"80007500", "hurt_closest_y"},
    {"80007508", "hurt_closest_z"},
    {"80007538", "hurt_closest_x"},
    {"8000750c", "hit_closest_x"},
    {"8000751c", "hit_closest_y"},
    {"8000752c", "hit_closest_z"},
    {"80007574", "closest_distance_x"},
    {"80007578", "closest_distance_z"},
    {"8000768c", "local_distance_x"},
    {"80007690", "local_distance_z"},
    {"8000771c", "contact_x"},
    {"80007730", "contact_y"},
    {"80007744", "contact_z"},
};
#endif

typedef struct FmaObservation {
    uint32_t calls;
    uint32_t operands[3];
    uint32_t result;
} FmaObservation;

static FmaObservation collision_observations[MELEE_WEB_FMA_COLLISION_SITE_COUNT];
static FmaObservation velocity_y_observation;
static uint32_t selected_cursor = UINT32_MAX;

static uint32_t float_bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static int supported_input(float value)
{
    const int category = fpclassify(value);
    return isfinite(value) && (category == FP_NORMAL || category == FP_ZERO);
}

static float checked_fmaf(unsigned site, const char* name, float a, float b,
                         float c)
{
    if (!supported_input(a) || !supported_input(b) || !supported_input(c)) {
        fprintf(stderr,
            "FMA_DIAGNOSTIC unsupported input domain site=%s a=%08x b=%08x c=%08x\n",
            name, float_bits(a), float_bits(b), float_bits(c));
        abort();
    }

    /* The binary64 expression is used only to reject an NI underflow domain.
     * The returned value always comes from correctly rounded binary32 fmaf. */
    const double exact = (double) a * (double) b + (double) c;
    if (!isfinite(exact) || (exact != 0.0 && fabs(exact) < (double) FLT_MIN)) {
        fprintf(stderr,
            "FMA_DIAGNOSTIC unsupported NI underflow domain site=%s a=%08x b=%08x c=%08x\n",
            name, float_bits(a), float_bits(b), float_bits(c));
        abort();
    }
    /* Binary64 can round a value just below the normal boundary to FLT_MIN.
     * Reject equality conservatively; do not claim this predicts PPC NI. */
    if (fabs(exact) == (double) FLT_MIN) {
        fprintf(stderr,
            "FMA_DIAGNOSTIC ambiguous NI boundary site=%s a=%08x b=%08x c=%08x\n",
            name, float_bits(a), float_bits(b), float_bits(c));
        abort();
    }

    const float result = fmaf(a, b, c);
    if (!isfinite(result) || fpclassify(result) == FP_SUBNORMAL) {
        fprintf(stderr,
            "FMA_DIAGNOSTIC unsupported result domain site=%s a=%08x b=%08x c=%08x result=%08x\n",
            name, float_bits(a), float_bits(b), float_bits(c), float_bits(result));
        abort();
    }
    (void) site;
    return result;
}

static void record(FmaObservation* observation, float a, float b, float c,
                   float result)
{
    if (selected_cursor == UINT32_MAX)
        return;
    if (observation->calls == UINT32_MAX)
        abort();
    ++observation->calls;
    observation->operands[0] = float_bits(a);
    observation->operands[1] = float_bits(b);
    observation->operands[2] = float_bits(c);
    observation->result = float_bits(result);
}

#ifdef MELEE_WEB_FMA_COLLISION_DIAGNOSTIC
float melee_web_fma_collision(unsigned site, float a, float b, float c)
{
    if (site >= MELEE_WEB_FMA_COLLISION_SITE_COUNT)
        abort();
    const float result = checked_fmaf(site, collision_sites[site].name, a, b, c);
    record(&collision_observations[site], a, b, c, result);
    return result;
}
#endif

#ifdef MELEE_WEB_FMA_VELOCITY_Y_DIAGNOSTIC
float melee_web_fma_velocity_y(float multiplier, float delta, float base)
{
    const float result = checked_fmaf(MELEE_WEB_FMA_COLLISION_SITE_COUNT,
        "common_velocity_y_interpolation_8006bc90", multiplier, delta, base);
    record(&velocity_y_observation, multiplier, delta, base, result);
    return result;
}
#endif

void melee_web_fma_diagnostic_cursor(uint32_t cursor)
{
    selected_cursor = cursor;
    memset(collision_observations, 0, sizeof(collision_observations));
    memset(&velocity_y_observation, 0, sizeof(velocity_y_observation));
}

void melee_web_fma_diagnostic_finish_cursor(void)
{
    selected_cursor = UINT32_MAX;
}

static void append(char* output, size_t capacity, size_t* used,
                   const char* format, ...)
{
    if (!output || !used || *used >= capacity)
        abort();
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(output + *used, capacity - *used, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t) written >= capacity - *used)
        abort();
    *used += (size_t) written;
}

static void append_observation(char* output, size_t capacity, size_t* used,
                               const FmaObservation* observation)
{
    if (observation->calls == 0) {
        append(output, capacity, used, "null");
        return;
    }
    append(output, capacity, used,
        "{\"calls\":%u,\"last_operands\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"last_result\":\"%08x\"}",
        observation->calls, observation->operands[0], observation->operands[1],
        observation->operands[2], observation->result);
}

void melee_web_fma_diagnostic_append_json(char* output, size_t capacity,
                                          size_t* used)
{
    if (selected_cursor == UINT32_MAX)
        abort();
    append(output, capacity, used,
        ",\"fma_diagnostic\":{\"schema\":\"melee-web-fma-diagnostic-v1\","
        "\"cursor\":%u", selected_cursor);
#ifdef MELEE_WEB_FMA_COLLISION_DIAGNOSTIC
    append(output, capacity, used,
        ",\"collision\":{\"enabled\":true,\"site_count\":%u,\"sites\":[",
        (unsigned) MELEE_WEB_FMA_COLLISION_SITE_COUNT);
    for (unsigned site = 0; site < MELEE_WEB_FMA_COLLISION_SITE_COUNT; ++site) {
        append(output, capacity, used, "%s{\"pc\":\"%s\",\"name\":\"%s\",\"observation\":",
            site ? "," : "", collision_sites[site].pc, collision_sites[site].name);
        append_observation(output, capacity, used, &collision_observations[site]);
        append(output, capacity, used, "}");
    }
    append(output, capacity, used, "]}");
#else
    append(output, capacity, used, ",\"collision\":{\"enabled\":false}");
#endif
#ifdef MELEE_WEB_FMA_VELOCITY_Y_DIAGNOSTIC
    append(output, capacity, used,
        ",\"velocity_y\":{\"enabled\":true,\"pc\":\"8006bc90\","
        "\"candidate_executed\":%s,\"observation\":",
        velocity_y_observation.calls ? "true" : "false");
    append_observation(output, capacity, used, &velocity_y_observation);
    append(output, capacity, used, "}");
#else
    append(output, capacity, used, ",\"velocity_y\":{\"enabled\":false}");
#endif
    append(output, capacity, used, "}");
}
