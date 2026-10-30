/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "perf_measurement.h"

#include <stddef.h>
#include <string.h>

extern s64 start_time;

typedef struct SlippiHsdPerfMeasurement {
    HSD_PerfStat current;
    HSD_PerfStat last;
    s64 start_time;
    HSD_PerfStat canonical_current_bytes;
    HSD_PerfStat canonical_last_bytes;
    s64 canonical_start_time;
    const HSD_PerfStat* canonical_current;
    const HSD_PerfStat* canonical_last;
    const s64* canonical_clock_start;
    uint64_t world_generation;
    unsigned seeded;
    unsigned failed;
} SlippiHsdPerfMeasurement;

static SlippiHsdPerfMeasurement measurement;

_Static_assert(sizeof(HSD_PerfStat) == 144,
               "Original HSD performance measurement layout changed");
_Static_assert(offsetof(HSD_PerfStat, env_blend) == 16,
               "Original HSD performance counter layout changed");

static int canonical_symbols_unchanged(void)
{
    return measurement.canonical_current == &HSD_PerfCurrentStat &&
           measurement.canonical_last == &HSD_PerfLastStat &&
           measurement.canonical_clock_start == &start_time &&
           memcmp(measurement.canonical_current,
                  &measurement.canonical_current_bytes,
                  sizeof(measurement.canonical_current_bytes)) == 0 &&
           memcmp(measurement.canonical_last,
                  &measurement.canonical_last_bytes,
                  sizeof(measurement.canonical_last_bytes)) == 0 &&
           memcmp(measurement.canonical_clock_start,
                  &measurement.canonical_start_time,
                  sizeof(measurement.canonical_start_time)) == 0;
}

static int available(void)
{
    if (!measurement.seeded || !measurement.world_generation ||
        measurement.failed)
        return 0;
    if (!canonical_symbols_unchanged()) {
        measurement.failed = 1;
        return 0;
    }
    return 1;
}

int slippi_hsd_perf_measurement_world_begin(uint64_t world_generation)
{
    if (!world_generation || measurement.world_generation || measurement.failed)
        return 0;
    if (measurement.seeded) {
        if (!canonical_symbols_unchanged()) {
            measurement.failed = 1;
            return 0;
        }
    } else {
        measurement.canonical_current = &HSD_PerfCurrentStat;
        measurement.canonical_last = &HSD_PerfLastStat;
        measurement.canonical_clock_start = &start_time;
        memcpy(&measurement.canonical_current_bytes, &HSD_PerfCurrentStat,
               sizeof(measurement.canonical_current_bytes));
        memcpy(&measurement.canonical_last_bytes, &HSD_PerfLastStat,
               sizeof(measurement.canonical_last_bytes));
        memcpy(&measurement.canonical_start_time, &start_time,
               sizeof(measurement.canonical_start_time));
        memcpy(&measurement.current, &HSD_PerfCurrentStat,
               sizeof(measurement.current));
        memcpy(&measurement.last, &HSD_PerfLastStat,
               sizeof(measurement.last));
        memcpy(&measurement.start_time, &start_time,
               sizeof(measurement.start_time));
        measurement.seeded = 1;
    }
    measurement.world_generation = world_generation;
    return 1;
}

int slippi_hsd_perf_measurement_world_end(uint64_t world_generation)
{
    if (!world_generation || measurement.world_generation != world_generation)
        return 0;
    if (measurement.failed || !canonical_symbols_unchanged()) {
        measurement.failed = 1;
        measurement.world_generation = 0;
        return 0;
    }
    measurement.world_generation = 0;
    return 1;
}

HSD_PerfStat* slippi_hsd_perf_measurement_current(void)
{
    return available() ? &measurement.current : NULL;
}

HSD_PerfStat* slippi_hsd_perf_measurement_last_for_init(void)
{
    return available() ? &measurement.last : NULL;
}

const HSD_PerfStat* slippi_hsd_perf_measurement_last(void)
{
    return available() ? &measurement.last : NULL;
}

s64* slippi_hsd_perf_measurement_start_time(void)
{
    return available() ? &measurement.start_time : NULL;
}
