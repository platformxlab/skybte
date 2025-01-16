#ifndef __BYTEFS_FTL_H__
#define __BYTEFS_FTL_H__

#include <fstream>
#include <unordered_map>
#include <map>
#include <pthread.h>
#include <time.h>
#include <mutex>

#include "timing_model.h"
#include "ftl_mapping.h"
#include "bytefs_heap.h"
#include "bytefs_utils.h"
#include "bytefs_gc.h"
#include "ring.h"
#include "utils.h"

using std::unordered_map;
using std::map;
using std::mutex;

// #include <linux/kthread.h>  // for threads
// #include <linux/time.h>   // for using jiffies
// #include <linux/timer.h>

// #include "backend.c"

// // define of ssd parameters
#define CH_COUNT            (16)
#define WAY_COUNT           (1)

// #define PL_COUNT 1
#define BLOCK_COUNT         (8 * 8 * 128)
// #define PG_COUNT            (128)
#define PG_COUNT            (256)
#define PG_SIZE             (4 * 1024)


//change to 40 GB
#define CACHE_SIZE          (1UL * DRAM_END)
// #define TOTAL_SIZE          (2147483648UL)
// #define ALL_TOTAL_SIZE      (4294967296UL)
#define TOTAL_SIZE          (1UL * CH_COUNT * WAY_COUNT * BLOCK_COUNT * PG_COUNT * PG_SIZE)
#define ALL_TOTAL_SIZE      (CACHE_SIZE + TOTAL_SIZE)

#define NUM_POLLERS         (1)
#define MAX_REQ             (65536)
#define MAX_EVENT_HEAP      (1024)


// defines about buffer
#define BYTEFS_LOG_REGION_SIZE_IN_PAGE      (32768UL)
#define BYTEFS_LOG_REGION_SIZE              (BYTEFS_LOG_REGION_SIZE_IN_PAGE * PG_SIZE)
#define BYTEFS_LOG_FLUSH_HI_THRESHOLD       (BYTEFS_LOG_REGION_SIZE * 51 / 100)
#define BYTEFS_LOG_FLUSH_LO_THRESHOLD       (BYTEFS_LOG_REGION_SIZE * 1 / 100)
#define BYTEFS_LOG_REGION_GRANDULARITY      (64UL)
#define BYTEFS_LOG_REGION_MAX_ENTRY_NUM     (BYTEFS_LOG_REGION_SIZE / BYTEFS_LOG_REGION_GRANDULARITY)
#define BYTEFS_LOG_MAX_ENTRY_DATA_SIZE      (32768)


#define PG_MASK                 (PG_SIZE - 1)
#define GRANDULARITY_MASK       (BYTEFS_LOG_REGION_GRANDULARITY - 1)

#define INVALID_UINT8_T         (0xFFU)
#define INVALID_UINT16_T        (0xFFFFU)
#define INVALID_UINT32_T        (0xFFFFFFFFU)
#define INVALID_UINT64_T        (0xFFFFFFFFFFFFFFFFUL)

#define INVALID_LOG_OFFSET      (INVALID_UINT64_T)

// sanity check
#if PG_SIZE % 1024 != 0
#error "Page size is not 1k aligned"
#endif
#if TOTAL_SIZE % 1024 != 0
#error "Total size is not 1k aligned"
#endif
#if BYTEFS_LOG_REGION_GRANDULARITY & (BYTEFS_LOG_REGION_GRANDULARITY - 1) != 0
#error "Log grandularity is not power of 2"
#endif
#if PG_SIZE % BYTEFS_LOG_REGION_GRANDULARITY != 0
#error "Page size cannot be divided by grandularity"
#endif

/* CPUs binded to ftl threads */
#define SSD_THREAD_CPU  0
#define SSD_POLLING_CPU 1

/* Filesystem pa region */
#define BYTEFS_PA_START (32ULL<<30)
#define BYTEFS_PA_END   (64ULL<<30)

/* byte issue related fields*/
// to stop enabling the following features, make these feature zero.
#define BYTE_ISSUE_64_ALIGN              1
#define BYTE_BLOCK_MIX					 0

/* DRAM backend structure */
struct SsdDramBackend {
    void *phy_loc;      /* Emulated physical location */
    void *virt_loc;     /* Virtual address (in host's DRAM) used to emulate DRAM in 2B-SSD */
    unsigned long size; /* in bytes */
};

/* nand request type */
#define NAND_READ   0
#define NAND_WRITE  1
#define NAND_ERASE  2


/* io type */
#define USER_IO             0
#define GC_IO               1
#define INTERNAL_TRANSFER   2

/* page status */
#define PG_FREE     0
#define PG_INVALID  1
#define PG_VALID    2


/* Page mapping defines */
// @TODO check it inited using this value
#define UNMAPPED_PPA    (0xFFFFFFFFFFFFFFFFUL)
#define INVALID_LPN     (0xFFFFFFFFFFFFFFFFUL)


/* page index combination */
#define BLK_BITS    (16)
#define PG_BITS     (16)
// #define SEC_BITS    (8)
// #define PL_BITS     (8)
#define LUN_BITS    (8)
#define CH_BITS     (7)

#define ALLOCATION_SECHEM_LINE (1)  // 0 for block 1 for writeline

// struct gc_free_list;
// struct gc_candidate_list;
// struct bytefs_heap;

/* describe a physical page addr -> real_ppa */
struct ppa {
    // union {
    struct {
        uint64_t blk : BLK_BITS;
        uint64_t pg  : PG_BITS;
        // uint64_t sec : SEC_BITS;
        // uint64_t pl  : PL_BITS;
        uint64_t lun : LUN_BITS;
        uint64_t ch  : CH_BITS;
        uint64_t rsv : 1;
    } g;
    uint64_t realppa;
    // };
};

/**
 * struct page - page structure
 * @pg_num: physical page number of the page -> real_ppa
 * @status: status of the page
 */
struct nand_page {
    // struct ppa;
    int pg_num;
    int status;
};

/**
 * struct block - block structure
 * @pg: pages in this block )
 * @npgs: physical block number of the block
 * @vpc: valid page count of the block
 * @ipc: invalid page count of the block
 * @erase_cnt: times of the block being erased
 * @wp : write pointer -> speicificly which page are we going to write to
 */
struct nand_block {
    nand_page *pg;
    int npgs;
    int wp; /* current write pointer */

    int ipc; /* invalid page count */
    int vpc; /* valid page count */
    int erase_cnt;
    
    // GC specific
    int ch_idx;
    int way_idx;
    int blk_idx;
    int is_candidate;
    nand_block *next_blk;
};

/**
 * struct write_line - line structure
 * @blk_idx:
 */
struct writeline {
    int npgs;
    int wp; /* current write pointer should=ch_idx * way_idx of the page*/

    // @TODO check this when change gc with writeline
    int ipc; /* invalid page count */
    int vpc; /* valid page count */
    int erase_cnt;

    // GC specific
    int blk_idx;
    int pg_idx;
};

/**
 * struct superblock - superblock structure
 * @wtl: write lines in this superblock
 * @nwls: write line count of the superblock
 * @iwc: invalid write line count
 * @vwc: valid write line count
 * @erase_cnt: times of the superblock being erased
 * @wp: write pointer
 */
struct ssd_superblock {
    writeline *wtl;
    int line_wp;

    int nwls; /* number of write lines */
    int ipc; /* invalid page count */
    int vpc;   /* valid page count */
    int erase_cnt;


    // GC specific
    int blk_idx;
    int is_candidate;
    ssd_superblock *next_sb;
};


/**
 * struct write_line_set - write line set structure
 * @wtl: write lines in this set
 * @invalid_wl_cnt: invalid write line count
 * @valid_wl_cnt: valid write line count
 */

/**
 * struct lun - lun structure
 * @blk: blocks in this lun
 * @nblks: block count of the lun
 * @next_lun_avail_time: time for all request in this lun finishes
 * @busy: is lun working now? (not really used)
 */
struct nand_lun {
    nand_block *blk;
    int nblks;
    uint64_t next_lun_avail_time;
    uint64_t next_log_flush_lun_avail_time;
    uint64_t this_lun_avail_time;
    std::mutex timing_mutex;
    bool busy;
    uint64_t nrd = 0;
    uint64_t nwr = 0;
    uint64_t total_channel_latency = 0;
    // uint64_t gc_endtime;
};


/**
 * struct channel - channel structure
 * @lun: luns in this channel
 * @nluns: lun count of the channel
 * @next_ch_avail_time: time for all request in this channel finishes
 * @busy: is channel working now? (not really used)
 */
struct ssd_channel {
    nand_lun *lun;
    int nluns;
    uint64_t next_ch_avail_time;
    bool busy;
    // uint64_t gc_endtime;
};

struct ssdparams {

    int pgsz;          /* page size in bytes */

    int pgs_per_blk;  /* # of NAND pages per block */
    int blks_per_lun;  /* # of blocks per plane */
    // int pls_per_lun;  /* # of planes per LUN (Die) */
    int luns_per_ch;  /* # of LUNs per channel */
    int nchs;         /* # of channels in the SSD */

    //add for writeline
    int wls_per_sb;   /* # of write lines per superblock */
    int sb_per_ssd;   /* # of superblocks per LUN (Die) */
    int pgs_per_wl;    /* # of pages per write line */
    int tt_wls;       /* total # of write lines in the SSD */
    int pgs_per_sb;    /* # of pages per superblock */
    

    int pg_rd_lat;    /* NAND page read latency in nanoseconds */
    int pg_wr_lat;    /* NAND page program latency in nanoseconds */
    int blk_er_lat;   /* NAND block erase latency in nanoseconds */
    int ch_xfer_lat;  /* channel transfer latency for one page in nanoseconds
                       * this defines the channel bandwith
                       */

    // double gc_thres_pcent;
    // double gc_thres_pcent_high;
    // bool enable_gc_delay;

    /* below are all calculated values */
    // int pgs_per_lun;   /* # of pages per plane */
    int pgs_per_lun;  /* # of pages per LUN (Die) */
    int pgs_per_ch;   /* # of pages per channel */
    int tt_pgs;       /* total # of pages in the SSD */


    int blks_per_ch;  /* # of blocks per channel */
    int tt_blks;      /* total # of blocks in the SSD */

    int tt_luns;      /* total # of LUNs in the SSD */

    int num_poller;
};


/* wp: record next write page */
struct write_pointer {
    int ch;
    int next_ch;
    int lun;
    int blk;
    nand_block *blk_ptr;
    int pg;
};


struct nand_cmd {
    int type;
    int cmd;
    uint64_t stime; /* Coperd: request arrival time */
};

#define BYTEFS_LOG_VALID (1 << 0)

struct ftl_thread_info {
    int num_poller;
    Ring **to_ftl;
    Ring **to_poller;
};

#define INVALID_LPA (0xFFFFFFFFFFFFFFFF)
struct log_entry {
    uint64_t lpa;
    uint8_t data[64];
};

struct ssd {
    char *ssdname;
    struct ssdparams sp;
    // two views of allocation, one from channel->lun->block->page 
    // and the other from superblock->write line->page

    struct ssd_channel *ch;
    struct ssd_superblock *sb;
    
    // now lpa can exceed physical space size
    // ppa *maptbl; /* page level mapping table */
    mutex maptbl_update_mutex;
    unordered_map<uint64_t, ppa> maptbl;
    uint64_t *rmap;     /* reverse mapptbl, assume it's stored in OOB */
    struct write_pointer wp;
    // line_mgmt lm;

    /* lockless ring for communication with NVMe IO thread */
    struct Ring **to_ftl;
    struct Ring **to_poller;
    // bool *dataplane_started_ptr;

    // backend
    struct SsdDramBackend* bd;

    // thread
    const uint64_t n_log_writer_threads = 1;
    const uint64_t n_promotion_threads = 1;
    struct ftl_thread_info *thread_args;
    pthread_t *ftl_thread_id;
    pthread_t *polling_thread_id;
    pthread_t *log_writer_thread_id;
    pthread_t *promotion_thread_id;
    pthread_t *simulator_timer_id;
    volatile uint8_t terminate_flag = 0;

    // bytefs log region and mapping table
    struct log_entry *bytefs_log_region_start;
    struct log_entry *bytefs_log_region_end;
    uint64_t bytefs_log_region_size;
    mutex log_wp_lock;
    struct log_entry *log_rp;
    struct log_entry *log_wp;
    volatile atomic_uint64_t log_size;
    mutex indirection_mt_lock;
    map<uint64_t, uint64_t> indirection_mt; // must be populated first
    void *log_page_buffer;
    void *flush_page_buffer;
    size_t log_flush_lo_threshold;
    size_t log_flush_hi_threshold;
    volatile int log_flush_required;

    int log_read_cnt;
    int log_write_cnt;

    // GC
#if ALLOCATION_SECHEM_LINE
    int total_free_sbs;
    struct gc_free_list *free_sbs;
    struct gc_candidate_list *gc_candidate_sbs;
#else
    int total_free_blks;
    gc_free_list *free_blks;
    gc_candidate_list *gc_candidate_blks;
#endif
    struct bytefs_heap *gc_heaps;
    void *gc_buffer;
    int free_blk_lo_threshold;
    int free_blk_hi_threshold;
    volatile uint8_t undergoing_gc = 0;
    
    volatile uint8_t run_flag = 0;
};

int ssd_init(void);
int ssd_reset_skybyte(void);
int ssd_reset(void);

/** Backend.c */

extern SsdDramBackend *dram_backend;

int init_dram_space(void *phy_loc, void *virt_loc, unsigned int nbytes);

/* Memory Backend (mbe) for emulated SSD */

int init_dram_backend(SsdDramBackend **mbe, size_t nbytes, uint64_t phy_loc);


void free_dram_backend(SsdDramBackend *b);

// read or write to dram location
int backend_rw(SsdDramBackend *b, unsigned long ppa, void* data, bool is_write);

// read or write to dram location
int cache_rw(SsdDramBackend *b, unsigned long off, void* data, bool is_write, unsigned long size);

void *cache_mapped(SsdDramBackend *b, unsigned long off);

enum issue_status : uint8_t {
    NORMAL                      = 0,
    HOST_DRAM_HIT               = (1 << 0),
    WRITE_LOG_W                 = (1 << 1),
    WRITE_LOG_R                 = (1 << 2),
    SSD_CACHE_HIT               = (1 << 3),
    SSD_CACHE_MISS              = (1 << 4),
    ONGOING_DELAY               = (1 << 5)
};

enum nand_type : uint8_t {
    HLL_NAND            = (1 << 0),
    SLC_NAND            = (1 << 1),
    MLC_NAND            = (1 << 2),
    TLC_NAND            = (1 << 3) 
}; 

struct issue_response {
    uint64_t latency;
    uint64_t estimated_latency;
    issue_status flag;
};

/** ftl.c */
extern ssd gdev;
extern uint64_t start, cur;

extern int ssd_init(void);
extern void bytefs_stop_threads_gracefully(void);
extern int ssd_reset(void);
/* SSD API not using BIO */
extern int nvme_issue(int is_write, uint64_t lba, uint64_t len, struct issue_response *resp);
extern int byte_issue(int is_write, uint64_t lpa, uint64_t size, struct issue_response *resp);

void ppa2pgidx(ssd *ssd, ppa *ppa);
void pgidx2ppa(ssd *ssd, ppa *ppa);

/**
 * get the physical address of the page
 * @param lpn: logical page number
 * @param ssd: ssd structure
 * @return: physical address of the page by struct ppa
 */
inline ppa get_maptbl_ent(ssd *ssd, uint64_t lpn)
{
    // LPA range can exceed physical space now
    // bytefs_assert_msg(lpn < ssd->sp.tt_pgs,
    //         "LPN: %ld exceeds #tt_pgs: %d", lpn, ssd->sp.tt_pgs);
    if (ssd->maptbl.find(lpn) == ssd->maptbl.end()) {
        ppa ret;
        ret.realppa = UNMAPPED_PPA;
        return ret;
    }
    return ssd->maptbl[lpn];
}

/**
 * set the physical address of the page
 */
inline void set_maptbl_ent(ssd *ssd, uint64_t lpn, ppa *ppa)
{
    // LPA range can exceed physical space now
    // bytefs_assert_msg(lpn < ssd->sp.tt_pgs,
    //         "LPN: %ld exceeds #tt_pgs: %d", lpn, ssd->sp.tt_pgs);
    ssd->maptbl[lpn] = *ppa;
}

inline uint64_t get_rmap_ent(ssd *ssd, ppa *ppa)
{
    ppa2pgidx(ssd, ppa);
    return ssd->rmap[ppa->realppa];
}

/* set rmap[page_no(ppa)] -> lpn */
inline void set_rmap_ent(ssd *ssd, uint64_t lpn, ppa *ppa)
{
    ppa2pgidx(ssd, ppa);
    ssd->rmap[ppa->realppa] = lpn;
}


ppa get_new_page(struct ssd *ssd);
void mark_page_invalid(struct ssd *ssd, struct ppa *ppa);
void mark_page_valid(struct ssd *ssd, struct ppa *ppa);
void mark_block_free(struct ssd *ssd, struct ppa *ppa);
void mark_sb_free(struct ssd *ssd, struct ssd_superblock *sb);
int is_sb_free(struct ssd *ssd, struct ssd_superblock *sb);
int is_block_free(struct ssd *ssd, struct nand_block *nand_block);
bool backend_prefill_data(uint64_t addr);
void ssd_backend_reset_timestamp();

void ssd_advance_write_pointer(struct ssd *ssd);
uint64_t ssd_advance_status(struct ssd *ssd, struct ppa *ppa, struct nand_cmd *ncmd);

void *log_writer_thread(void *thread_args);
void *promotion_thread(void *thread_args);
void *simulator_timer_thread(void *thread_args);

void force_flush_log(void);
std::pair<uint64_t, uint64_t> flush_log_region_warmup(struct ssd *ssd, uint64_t tmp_array[64]);

int64_t get_thecache_dirty_page_num();
int64_t get_hostdram_dirty_page_num();
int64_t get_thecache_accessed_page_num();
int64_t get_hostdram_accessed_page_num();

int64_t get_thecache_dirty_marked_page_num();
int64_t get_hostdram_dirty_marked_page_num();
int64_t get_thecache_accessed_marked_page_num();
int64_t get_hostdram_accessed_marked_page_num();

void copy_dram_system(FILE* output_file);
void replay_dram_system(FILE* input_file);

void copy_tpp_system(FILE* output_file);
void replay_tpp_system(FILE* input_file);

void warmup_write_log(uint64_t read_pgnum, uint64_t write_pgnum);

void the_cache_mark_workup();
void host_dram_mark_workup();

/**
 * Check lba within disk.
*/
static inline bool lba_is_legal(uint64_t lba) {
    return (lba * PG_SIZE < TOTAL_SIZE);
}

void *ftl_thread(void *arg);
void *request_poller_thread(void *arg);
void ssd_parameter_dump(void);

void bytefs_fill_data(uint64_t current_lpn);
void warmup_ssd_dram(double warmup_dirty_ratio_cache, double warmup_dirty_ratio_dram, uint64_t read_pgnum, uint64_t write_pgnum,
double cache_overall_cover_rate, double host_overall_cover_rate, double cache_uncovered_dirty_rate, double host_uncovered_dirty_rate);
void bytefs_advance_timer(uint64_t timer);

nand_lun *ssd_get_lun(struct ssd *ssd, uint64_t ch_idx, uint64_t lun_idx);

//         ︿
//        / >》，———＜^｝
//       /┄┄/≠≠ ┄┄┄┄┄┄ヽ.
//      /┄┄//┄┄┄┄ ／｝┄┄ハ
//     /┄┄┄||┄┄／  ﾉ／  }┄}
//    /┄┄┄┄瓜亻 ＞ ´＜ ,'┄ﾉ
//   /┄┄┄┄|ノ\､  (フ_ノ 亻
//  │┄┄┄┄┄|  ╱ ｝｀ｽ/￣￣￣￣￣ /
// │┄┄┄┄┄┄┄|じ::つ / BeauGar /________
// ￣￣￣￣￣￣\___/________ /

#endif