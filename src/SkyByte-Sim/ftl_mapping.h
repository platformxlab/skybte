#ifndef __FTL_CACHE_H__
#define __FTL_CACHE_H__

#include "ftl.h"
#include "bytefs_heap.h"


// DRAM LAYOUT
#define DRAM_START                      (0UL)

#define BYTEFS_LOG_REGION_START         (DRAM_START)
#define BYTEFS_LOG_REGION_END           (BYTEFS_LOG_REGION_START + BYTEFS_LOG_REGION_SIZE)

#define DRAM_END                        (BYTEFS_LOG_REGION_END)
#define DRAM_LAST_ADDR                  (DRAM_END - 1UL)

#endif
