#ifndef __SSD_STAT__
#define __SSD_STAT__

#include <atomic>

#include "ftl.h"

struct ssd_stat {
    // total issue counter by request count
    atomic_uint64_t block_rissue_count;
    atomic_uint64_t block_wissue_count;
    atomic_uint64_t byte_rissue_count;
    atomic_uint64_t byte_wissue_count;
    // total traffic
    atomic_uint64_t block_rissue_traffic;
    atomic_uint64_t block_wissue_traffic;
    atomic_uint64_t byte_rissue_traffic;
    atomic_uint64_t byte_wissue_traffic;
    // meta data
    atomic_uint64_t inode_traffic;
    atomic_uint64_t superblock_traffic;
    atomic_uint64_t bitmap_traffic;
    atomic_uint64_t journal_traffic;
    atomic_uint64_t dp_traffic;
    // traffic
    atomic_uint64_t block_metadata_issue_traffic_r;
    atomic_uint64_t block_metadata_issue_traffic_w;
    atomic_uint64_t block_data_traffic_r;
    atomic_uint64_t block_data_traffic_w;
    atomic_uint64_t byte_metadata_issue_traffic_r;
    atomic_uint64_t byte_metadata_issue_traffic_w;
    atomic_uint64_t byte_data_traffic_r;
    atomic_uint64_t byte_data_traffic_w;
    // log based stats
    atomic_uint64_t log_wr_op;
    atomic_uint64_t log_rd_op;
    atomic_uint64_t log_rd_log_page_partial_hit;
    atomic_uint64_t log_rd_log_page_hit;
    atomic_uint64_t log_direct_rd_page;
    atomic_uint64_t log_coalescing_rd_page;
    atomic_uint64_t log_wr_page;
    atomic_uint64_t log_append;
    atomic_uint64_t log_flushes;
    atomic_uint64_t byte_issue_nand_wr_modified_distribution[PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY];
    atomic_uint64_t byte_issue_nand_rd_modified_distribution[PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY];
    // latency
    atomic_uint64_t total_r_lat;
    atomic_uint64_t total_w_lat;
    atomic_uint64_t prog_lat;
    // page cache related 
    atomic_uint64_t page_cache_rd_hit;
    atomic_uint64_t page_cache_rd_miss;
    atomic_uint64_t page_cache_wr_hit;
    atomic_uint64_t page_cache_wr_miss;
    atomic_uint64_t page_cache_flush_traffic;
    atomic_uint64_t page_cache_actuall_w_traffic;
    // internal traffic
    atomic_uint64_t nand_read_user;
    atomic_uint64_t nand_read_internal;
    atomic_uint64_t nand_read_gc;
    atomic_uint64_t nand_write_user;
    atomic_uint64_t nand_write_internal;
    atomic_uint64_t nand_write_gc;

    /*LSSD*/
    //Hit ratio
    atomic_uint64_t total_access_num;
    atomic_uint64_t host_dram_hit_num;
    atomic_uint64_t hostandssdDram_hit_num;
    atomic_uint64_t total_flash_miss_num;
    atomic_uint64_t total_miss_latency;

};


extern ssd_stat stat;
extern int stat_flag;

inline int check_stat_state(void) {
    return stat_flag;
}

int turn_on_stat(void);
int reset_ssd_stat(void);
int print_stat(void);

#define SSD_STAT_ATOMIC_ADD(name, value) {  \
    if (check_stat_state()) {               \
        stat.name += value;                 \
    }                                       \
}

#define SSD_STAT_ATOMIC_SUB(name, value) {  \
    if (check_stat_state()) {               \
        stat.name -= value;                 \
    }                                       \
}

#define SSD_STAT_ATOMIC_INC(name) {         \
    if (check_stat_state()) {               \
        stat.name++;                        \
    }                                       \
}

#define SSD_STAT_ATOMIC_DEC(name) {         \
    if (check_stat_state()) {               \
        stat.name--;                        \
    }                                       \
}

#define SSD_STAT_ATOMIC_SET(name, value) {  \
    if (check_stat_state()) {               \
        stat.name = value;                  \
    }                                       \
}

#define SSD_STAT_ADD(name, value) {     \
    SSD_STAT_ATOMIC_ADD(name, value);   \
}

#define SSD_STAT_SUB(name, value) {     \
    SSD_STAT_ATOMIC_SUB(name, value);   \
}

#define SSD_STAT_SET(name, value) {     \
    SSD_STAT_ATOMIC_SET(name, value);   \
}



#endif
