/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_PERF_MEASUREMENT_H
#define MELEE_WEB_SLIPPI_PERF_MEASUREMENT_H

#include <stdint.h>
#include <sysdolphin/baselib/perf.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called only by the private particle-profile bootstrap before original HSD
 * startup. The typed values persist when a world ends or is restored. */
int slippi_hsd_perf_measurement_world_begin(uint64_t world_generation);
int slippi_hsd_perf_measurement_world_end(uint64_t world_generation);

/* These return NULL without an admitted active world or if canonical source
 * symbols have changed. Source callers must not fall back to them. */
HSD_PerfStat* slippi_hsd_perf_measurement_current(void);
HSD_PerfStat* slippi_hsd_perf_measurement_last_for_init(void);
const HSD_PerfStat* slippi_hsd_perf_measurement_last(void);
s64* slippi_hsd_perf_measurement_start_time(void);

#ifdef __cplusplus
}
#endif

#endif
