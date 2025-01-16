#include <time.h>
#include <sched.h>
#include <cstring>
#include <mutex>
#include <deque>
#include <set>

#include "ftl.h"

#include "simple_nvme.h"
#include "backend.h"
#include "bytefs_heap.h"
#include "bytefs_gc.h"
#include "bytefs_utils.h"
#include "ssd_stat.h"
#include "cache_controller.h"
#include "utils.h"
#include "simulator_clock.h"

using std::max;
using std::pair;
using std::mutex;

//Parameters:
bool promotion_enable = true;
bool write_log_enable = true;
bool device_triggered_ctx_swt = true;
long cs_threshold = 43000;
nand_type n_type;
bool is_simulator_not_emulator = true;
bool print_page_locality = true;

long ssd_cache_size_byte = 512*1024*1024;
int ssd_cache_way = 16;
long host_dram_size_byte =1*1024*1024*1024;

double write_log_ratio = 0.125;

ssd gdev;
int inited_flag = 0;

void *dummy_buffer;

// lock for accessing
mutex mapping_table_mutex;
mutex log_tail_mutex;
mutex log_size_mutex;
mutex log_page_buffer_mutex;
mutex m_screen;

//SSD data cache and controller
cache_controller* dram_subsystem;

//The simulator clock
extern sim_clock* the_clock_pt;
extern param param;
extern std::vector<uint64_t> ordered_vector;

/**
 * Modifying the log tail would require a lock bc there could be concurrent writes to the log 
 * region at the same time. However, modifying the log head does not require a lock as the
 * log head is only advanced when there is log coalescing and flushing event triggered. Now log
 * coalescing and flushing procedure is moved to ftl_thread, means there would be no race 
 * condition happing, thus no lock associated.
*/

/* testing macro to reduce printk */
#define TEST_FTL_NODEBUG    0
#define TEST_FTL_FILL_LOG   1
#define TEST_FTL_SHOW_SIZE  2
#define TEST_FTL_NEW_BASE   4
#define TEST_FTL_DEBUG      TEST_FTL_NODEBUG

// this is a naive initialization of the nvme cmd, we just put addr at prp1
void bytefs_init_nvme(NvmeCmd* req, int op, uint64_t lba, uint32_t nlb, void* addr) {
    req->opcode = op;
    req->fuse = 0;
    req->psdt = 0;
    req->cid = 0;
    req->nsid = 1;
    req->mptr = 0;
    req->dptr.prp1 = (uint64_t) addr;
    req->dptr.prp2 = 0;
    req->cdw10 = lba;
    req->cdw11 = 0;
    req->cdw12 = nlb;
    req->cdw13 = 0;
    req->cdw14 = 0;
    req->cdw15 = 0;
}



//For TPP implementation:

std::deque<uint64_t> LRU_active_list;
std::set<uint64_t> LRU_inactive_list;
std::mutex tpp_LRU_mutex;
vector<uint64_t> ordered_memory_space;
uint64_t NUMA_scan_pointer = 0;
uint64_t NUMA_scan_threshold_ns = 100000000; //100 ms
long NUMA_scan_count = 0;
bool tpp_enable = false;
std::set<uint64_t> NUMA_scan_set;
std::mutex NUMA_scan_mutex;


//For AstriFlash implementation:
bool astriflash_enable = false;


/**
 * get the realppa (physical page index) of the page
 *
 */
void ppa2pgidx(struct ssd *ssd, ppa *ppa)
{
    ssdparams *spp = &ssd->sp;

    ppa->realppa = ppa->g.ch  * spp->pgs_per_ch  + \
                   ppa->g.lun * spp->pgs_per_lun + \
                   ppa->g.blk * spp->pgs_per_blk + \
                   ppa->g.pg;
    bytefs_assert_msg(ppa->realppa < spp->tt_pgs,
            "PPA: %ld exceeds #tt_pgs: %d", ppa->realppa, ssd->sp.tt_pgs);
}

void pgidx2ppa(struct ssd *ssd, ppa *ppa)
{
    ssdparams *spp = &ssd->sp;
    uint64_t pgidx = ppa->realppa;
    bytefs_assert_msg(ppa->realppa < spp->tt_pgs,
            "PPA: %ld exceeds #tt_pgs: %d", ppa->realppa, ssd->sp.tt_pgs);

    ppa->g.ch = pgidx / spp->pgs_per_ch;
    pgidx %= spp->pgs_per_ch;
    ppa->g.lun = pgidx / spp->pgs_per_lun;
    pgidx %= spp->pgs_per_lun;
    ppa->g.blk = pgidx / spp->pgs_per_blk;
    pgidx %= spp->pgs_per_blk;
    ppa->g.pg = pgidx;
}

#if ALLOCATION_SECHEM_LINE
static void ssd_init_write_pointer(struct ssd *ssd)
{
    write_pointer *wpp = &ssd->wp;
    ssd_superblock *sb = bytefs_get_next_free_sb(ssd);
    wpp->blk_ptr = NULL; // not used
    
    wpp->ch = 0;
    wpp->lun = 0;
    wpp->blk = sb->blk_idx;
    wpp->pg = 0;
}
#else
static void ssd_init_write_pointer(struct ssd *ssd)
{
    write_pointer *wpp = &ssd->wp;
    wpp->next_ch = 0;
    wpp->blk_ptr = bytefs_get_next_free_blk(ssd, &wpp->next_ch);
    wpp->ch = wpp->blk_ptr->ch_idx;
    wpp->lun = wpp->blk_ptr->way_idx;
    wpp->blk = wpp->blk_ptr->blk_idx;
    wpp->pg = 0;
    wpp->blk_ptr->wp = 0;

}
#endif

// static inline void check_addr(int a, int max)
// {
//     bytefs_assert(a >= 0 && a < max);
// }

#if ALLOCATION_SECHEM_LINE
void ssd_advance_write_pointer(struct ssd *ssd)
{
    ssdparams *spp = &ssd->sp;
    write_pointer *wpp = &ssd->wp;
    // line_mgmt *lm = &ssd->lm;

    wpp->ch++;
    ssd->sb[wpp->blk].wtl[wpp->pg].wp++;

    if (wpp->ch == spp->nchs) {
        wpp->ch = 0;
        wpp->lun++;

        if (wpp->lun == spp->luns_per_ch) {
            wpp->lun = 0;
            wpp->pg++;
            ssd->sb[wpp->blk].line_wp++;

            if (wpp->pg == spp->pgs_per_blk) {
                ssd_superblock *sb = bytefs_get_next_free_sb(ssd);
                wpp->ch = 0;
                wpp->lun = 0;
                wpp->blk = sb->blk_idx;
                wpp->pg = 0;

                sb->line_wp = 0;
            }
        }
    }
    bytefs_assert(wpp->ch < spp->nchs);
    bytefs_assert(wpp->lun < spp->luns_per_ch);
    bytefs_assert(wpp->blk < spp->blks_per_lun);
    bytefs_assert(wpp->pg < spp->pgs_per_blk);
    bytefs_assert(wpp->pg < spp->pgs_per_blk);
}
#else
void ssd_advance_write_pointer(struct ssd *ssd)
{
    ssdparams *spp = &ssd->sp;
    write_pointer *wpp = &ssd->wp;
    // line_mgmt *lm = &ssd->lm;

    // check_addr(wpp->ch, spp->nchs);
    wpp->pg++;
    // check if we reached last page in block
    if (wpp->pg == spp->pgs_per_blk) {
        // move wp to the next block
        wpp->blk_ptr = bytefs_get_next_free_blk(ssd, &wpp->next_ch);
        bytefs_assert(wpp->blk_ptr);
        wpp->ch = wpp->blk_ptr->ch_idx;
        wpp->lun = wpp->blk_ptr->way_idx;
        wpp->blk = wpp->blk_ptr->blk_idx;
        wpp->pg = 0;
        wpp->blk_ptr->wp = 0;
    } else {
        // move to next page
        wpp->blk_ptr->wp++;
    }
    // printf("wpp->blk_ptr->wp: %d, wpp->ch:%d, wpp->lun:%d, wpp->blk:%d, wpp->pg:%d", wpp->blk_ptr->wp, wpp->ch, wpp->lun, wpp->blk, wpp->pg);

    bytefs_assert(wpp->ch < spp->nchs);
    bytefs_assert(wpp->lun < spp->luns_per_ch);
    bytefs_assert(wpp->blk < spp->blks_per_lun);
    bytefs_assert(wpp->pg < spp->pgs_per_blk);

    bytefs_assert(wpp->blk_ptr->wp == wpp->pg);
    // bytefs_assert(wpp->blk_ptr == &(ssd->ch[wpp->ch].lun[wpp->lun].blk[wpp->blk]));
    // printk(KERN_ERR "REQ CNT %d\n", req_cnt);
}
#endif


ppa get_new_page(struct ssd *ssd)
{
    write_pointer *wpp = &ssd->wp;
    ppa ppa;
    ppa.realppa = 0;
    ppa.g.ch = wpp->ch;
    ppa.g.lun = wpp->lun;
    ppa.g.blk = wpp->blk;
    ppa.g.pg = wpp->pg;
    ppa2pgidx(ssd, &ppa);
    return ppa;
}

static void check_params(ssdparams *spp)
{

}

static void ssd_init_params(ssdparams *spp)
{

    spp->pgsz = PG_SIZE;

    spp->pgs_per_blk = PG_COUNT;
    spp->blks_per_lun = BLOCK_COUNT;
    spp->luns_per_ch = WAY_COUNT;
    spp->nchs = CH_COUNT;

    spp->pgs_per_wl = CH_COUNT * WAY_COUNT;
    spp->pgs_per_sb = (CH_COUNT * WAY_COUNT) * PG_COUNT;
    spp->wls_per_sb = BLOCK_COUNT;
    spp->sb_per_ssd = BLOCK_COUNT;
    spp->tt_wls = spp->wls_per_sb * spp->sb_per_ssd;
    // spp->blk_per_sb = BLOCK_COUNT;

    if (n_type & nand_type::HLL_NAND)
    {
        spp->pg_rd_lat = NAND_READ_LATENCY;
        spp->pg_wr_lat = NAND_PROG_LATENCY;
        spp->blk_er_lat = NAND_BLOCK_ERASE_LATENCY;
    }
    else if (n_type & nand_type::SLC_NAND)
    {
        spp->pg_rd_lat = 25000;
        spp->pg_wr_lat = 200000;
        spp->blk_er_lat = 1500000;
    }
    else if (n_type & nand_type::MLC_NAND)
    {
        spp->pg_rd_lat = 50000;
        spp->pg_wr_lat = 600000;
        spp->blk_er_lat = 3000000;
    }
    else if (n_type & nand_type::TLC_NAND)
    {
        spp->pg_rd_lat = 4000;
        spp->pg_wr_lat = 75000;
        spp->blk_er_lat = 853000;
    }
    else
    {
        assert(0);
    }
    
    spp->ch_xfer_lat = CHNL_TRANSFER_LATENCY_NS;

    spp->pgs_per_lun = spp->pgs_per_blk * spp->blks_per_lun;
    spp->pgs_per_ch = spp->pgs_per_lun * spp->luns_per_ch;
    spp->tt_pgs = spp-> pgs_per_ch * spp->nchs;


    spp->blks_per_ch = spp->blks_per_lun * spp->luns_per_ch;
    spp->tt_blks = spp->blks_per_ch * spp->nchs;
    spp->tt_luns = spp->luns_per_ch * spp->nchs;

    spp->num_poller = NUM_POLLERS;

    // GC related
    // spp->gc_thres_pcent = 0.75;
    // spp->gc_thres_lines = (int)((1 - spp->gc_thres_pcent) * spp->tt_lines);
    // spp->gc_thres_pcent_high = 0.95;
    // spp->gc_thres_lines_high = (int)((1 - spp->gc_thres_pcent_high) * spp->tt_lines);
    // spp->enable_gc_delay = true;

    check_params(spp);
}


static void ssd_init_nand_blk(nand_block *blk, ssdparams *spp)
{
    int i = 0;
    blk->npgs = spp->pgs_per_blk;
    blk->pg = (nand_page *) malloc(sizeof(nand_page) * blk->npgs);
    memset(blk->pg, 0, sizeof(nand_page) * blk->npgs);
    for (i = 0; i < blk->npgs; i++) {
        blk->pg[i].pg_num = i;
        blk->pg[i].status = PG_FREE;
    }
    blk->ipc = 0;
    blk->vpc = 0;
    blk->erase_cnt = 0;
    blk->wp = 0;
}


static void ssd_init_nand_lun(nand_lun *lun, ssdparams *spp)
{
    int i;
    lun->nblks = spp->blks_per_lun;
    lun->blk = (nand_block *) malloc(sizeof(nand_block) * lun->nblks);
    memset(lun->blk, 0, sizeof(nand_block) * lun->nblks);
    for (i = 0; i < lun->nblks; i++) {
        ssd_init_nand_blk(&lun->blk[i], spp);
    }
    lun->next_lun_avail_time = 0;
    lun->next_log_flush_lun_avail_time = 0;
    lun->this_lun_avail_time = 0;
    lun->busy = false;
}

static void ssd_init_ch(ssd_channel *ch, ssdparams *spp)
{
    int i;
    ch->nluns = spp->luns_per_ch;
    ch->lun = (nand_lun *) malloc(sizeof(nand_lun) * ch->nluns);
    memset(ch->lun, 0, sizeof(nand_lun) * ch->nluns);
    for (i = 0; i < ch->nluns; i++) {
        ssd_init_nand_lun(&ch->lun[i], spp);
    }
    ch->next_ch_avail_time = 0;
    ch->busy = 0;
}


// static void ssd_init_nand_writeline(nand_writeline *wl, ssdparams *spp) {
//     int i;


// }

static void ssd_init_sb(ssd_superblock *sb, ssdparams *spp) {
    int i;
    sb->wtl = (struct writeline *) malloc(sizeof(writeline) * spp->wls_per_sb);
    memset(sb->wtl, 0, sizeof(writeline) * spp->wls_per_sb);
    for (i = 0; i < spp->wls_per_sb; i++) {
        sb->wtl[i].npgs = spp->pgs_per_wl;
        sb->wtl[i].blk_idx = sb->blk_idx;
        sb->wtl[i].pg_idx = i;
        sb->wtl[i].wp = 0;
    }
    
    sb->nwls = spp->wls_per_sb;
    sb->ipc = 0;
    sb->vpc = 0;
    
    sb->line_wp = 0;
}

static void ssd_init_maptbl(struct ssd *ssd)
{
    int i;
    ssdparams *spp = &ssd->sp;
    //printk(KERN_ERR "%llu",sizeof(ppa) * spp->tt_pgs);
    unsigned int size = sizeof(ppa) * spp->tt_pgs;
    // ssd->maptbl = (ppa *) malloc(size);
    // memset(ssd->maptbl, 0, size);
    // //printk(KERN_ERR "ssd->maptbl = %x",ssd->maptbl);
    // for (i = 0; i < spp->tt_pgs; i++) {
    //     ssd->maptbl[i].realppa = UNMAPPED_PPA;
    // }
}

static void ssd_init_rmap(struct ssd *ssd)
{
    int i;
    ssdparams *spp = &ssd->sp;
    //printk(KERN_ERR "%llu",sizeof(ppa) * spp->tt_pgs);
    unsigned int size = sizeof(ppa) * spp->tt_pgs;
    ssd->rmap = (uint64_t *) malloc(size);
    memset(ssd->rmap, 0, size);
    // ssd->rmap = kzalloc(sizeof(uint64_t) * spp->tt_pgs,GFP_KERNEL);
    for (i = 0; i < spp->tt_pgs; i++) {
        ssd->rmap[i] = INVALID_LPN;
    }
}



static void ssd_init_queues(struct ssd *ssd)
{
    int i;
    ssdparams *spp = &ssd->sp;

    ssd->to_poller = (Ring **) calloc(spp->num_poller + 1, sizeof(void *));
    ssd->to_ftl = (Ring **) calloc(spp->num_poller + 1, sizeof(void *));

    //@TODO check i = 1 here !!!
    for (i = 1; i <= spp->num_poller; i++) {
        ssd->to_poller[i] = ring_alloc(MAX_REQ, 1);
        ssd->to_ftl[i] = ring_alloc(MAX_REQ, 1);
    }

}



void ssd_end(void) {
    // ssd *ssd = &gdev;
    // pthread_join(ssd->thread_id, NULL);
    return;

}

static inline bool valid_ppa(struct ssd *ssd, ppa *ppa)
{
    ssdparams *spp = &ssd->sp;
    int ch = ppa->g.ch;
    int lun = ppa->g.lun;
    // int pl = ppa->g.pl;
    int blk = ppa->g.blk;
    int pg = ppa->g.pg;
    // int sec = ppa->g.sec;

    if ( ch >= 0 && ch < spp->nchs
            && lun >= 0 && lun < spp->luns_per_ch
            // && pl >=   0 && pl < spp->pls_per_lun
            && blk >= 0 && blk < spp->blks_per_lun
            && pg >= 0 && pg < spp->pgs_per_blk
            // && sec >= 0 && sec < spp->secs_per_pg
       )
        return true;

    return false;
}

// maybe changed according to GC?
static inline bool valid_lpn(struct ssd *ssd, uint64_t lpn)
{
    return (lpn < ssd->sp.tt_pgs);
}

static inline bool mapped_ppa(ppa *ppa)
{
    return !(ppa->realppa == UNMAPPED_PPA);
}

static inline ssd_channel *get_ch(struct ssd *ssd, ppa *ppa)
{
    return &(ssd->ch[ppa->g.ch]);
}

static inline nand_lun *get_lun(struct ssd *ssd, ppa *ppa)
{
    ssd_channel *ch = get_ch(ssd, ppa);
    return &(ch->lun[ppa->g.lun]);
}


static inline ssd_superblock *get_sb(struct ssd *ssd, ppa *ppa)
{
    // nand_plane *pl = get_pl(ssd, ppa);
    return &(ssd->sb[ppa->g.blk]);
}


static inline nand_block *get_blk(struct ssd *ssd, ppa *ppa)
{
    // nand_plane *pl = get_pl(ssd, ppa);
    nand_lun *lun = get_lun(ssd, ppa);
    return &(lun->blk[ppa->g.blk]);
}


static inline nand_page *get_pg(struct ssd *ssd, ppa *ppa)
{
    nand_block *blk = get_blk(ssd, ppa);
    return &(blk->pg[ppa->g.pg]);
}


nand_lun *ssd_get_lun(struct ssd *ssd, uint64_t ch_idx, uint64_t lun_idx) {
    ppa ppa;
    ppa.g.ch = ch_idx;
    ppa.g.lun = lun_idx;
    return get_lun(ssd, &ppa);
}


/** key function that calculates latency */
uint64_t ssd_advance_status(struct ssd *ssd, ppa *ppa, nand_cmd *ncmd)
{
    
    int c = ncmd->cmd;
    uint64_t cmd_stime, nand_stime, nand_stime_writelog;
    ssdparams *spp = &ssd->sp;
    nand_lun *lun = get_lun(ssd, ppa);
    lun->timing_mutex.lock();
    uint64_t lat = 0;
    uint64_t cur;

    if (!is_simulator_not_emulator)
    {
        cur = the_clock_pt->get_time_sim(); 

        cmd_stime = (ncmd->stime == 0) ? cur : ncmd->stime;

        if (ncmd->stime - cur > 1)
        {
            cmd_stime = cur;
        }
    }
    else
    {
        cmd_stime = ncmd->stime;
    }
    
    
    nand_stime = max(lun->next_lun_avail_time, cmd_stime);
    nand_stime_writelog = max(lun->next_log_flush_lun_avail_time, cmd_stime);

    uint8_t op;
    switch (c) {
    case NAND_READ:
        /* read: perform NAND cmd first */
        lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;
        

        lat = lun->next_lun_avail_time - cmd_stime;



        if (lun->next_log_flush_lun_avail_time > cmd_stime) //Write log compaction not finished
        {
            lun->this_lun_avail_time = lun->next_lun_avail_time;
            lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;
            uint64_t rest_flush_ltc = (lun->next_log_flush_lun_avail_time - cmd_stime) % (spp->pg_wr_lat);
            lun->next_log_flush_lun_avail_time = lun->next_log_flush_lun_avail_time + spp->pg_rd_lat;
            lat = lun->next_lun_avail_time - cmd_stime + rest_flush_ltc; 

        }else  //Normal read
        {
            lun->this_lun_avail_time = lun->next_lun_avail_time;
            lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;
            lat = lun->next_lun_avail_time - cmd_stime;
        }


        if (ncmd->type == USER_IO)                  SSD_STAT_ATOMIC_INC(nand_read_user)
        else if (ncmd->type == GC_IO)               SSD_STAT_ATOMIC_INC(nand_read_gc)
        else if (ncmd->type == INTERNAL_TRANSFER)   SSD_STAT_ATOMIC_INC(nand_read_internal)

        lun->nrd++;
        op = 'R';

        break;

    case NAND_WRITE:
        
        if (ncmd->type != INTERNAL_TRANSFER)
        {
            if (lun->next_log_flush_lun_avail_time > cmd_stime) //Write log compaction not finished
            {
                lun->this_lun_avail_time = lun->next_lun_avail_time;
                lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
                uint64_t rest_flush_ltc = (lun->next_log_flush_lun_avail_time - cmd_stime) % (spp->pg_wr_lat);
                lun->next_log_flush_lun_avail_time = lun->next_log_flush_lun_avail_time + spp->pg_wr_lat;
                lat = lun->next_lun_avail_time - cmd_stime + rest_flush_ltc; 

            }else  //Normal write
            {
                lun->this_lun_avail_time = lun->next_lun_avail_time;
                lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
                lat = lun->next_lun_avail_time - cmd_stime;
            }
        }
        

        if (ncmd->type == USER_IO){                  SSD_STAT_ATOMIC_INC(nand_write_user);}
        else if (ncmd->type == GC_IO){               SSD_STAT_ATOMIC_INC(nand_write_gc);}
        else if (ncmd->type == INTERNAL_TRANSFER){   
            SSD_STAT_ATOMIC_INC(nand_write_internal);
            lun->next_log_flush_lun_avail_time = nand_stime_writelog + spp->pg_wr_lat;
            lat = 0;
        }

        lun->nwr++;
        op = 'W';

        break;

    case NAND_ERASE:
        /* erase: only need to advance NAND status */
        lun->this_lun_avail_time = lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->blk_er_lat;
        lat = lun->next_lun_avail_time - cmd_stime;
        op = 'E';
        break;

    default:
        bytefs_err("Unsupported NAND command: 0x%x\n", c);
    }

    
    //std::cout<<"Blk idx / ch_idx: "<<lun->blk->blk_idx<<" / "<<lun->blk->ch_idx<<", lat= "<<lat<<", r/w/e: "<<c<<", time: "<<cmd_stime<<std::endl;
    //? why cannot set breakpoint? Because of O3
    // if(lat > 1000000000){
    //     lat = lat -1;
    //     exit(1);
    // }

    lun->timing_mutex.unlock();

    // bytefs_log("Channel %3d LUN %3d operation: %c start: %20.9f end: %20.9f lat %20.9f",
    //         ppa->g.ch, ppa->g.lun, op,
    //         nand_stime / 1e9, (nand_stime + lat) / 1e9,
    //         lat / 1e9);

    return lat;
}

/* update SSD status about one page from PG_VALID -> PG_INVALID */
void mark_page_invalid(struct ssd *ssd, ppa *ppa)
{
    // printf("PPA MARK INVALID %lX\n", ppa->realppa);
    ssdparams *spp = &ssd->sp;
    nand_block *blk = NULL;
    nand_page *pg = NULL;
    ssd_superblock *sb = NULL;


    /* update corresponding page status */
    pg = get_pg(ssd, ppa);
    bytefs_assert(pg->status == PG_VALID);
    pg->status = PG_INVALID;

    /* update corresponding block status */
    blk = get_blk(ssd, ppa);
    bytefs_assert(blk->ipc >= 0 && blk->ipc < spp->pgs_per_blk);
    blk->ipc++;
    bytefs_assert(blk->vpc > 0 && blk->vpc <= spp->pgs_per_blk);
    blk->vpc--;

    sb = get_sb(ssd, ppa);
    bytefs_assert(sb->ipc >= 0 && sb->ipc < spp->pgs_per_sb);
    sb->ipc++;
    bytefs_assert(sb->vpc > 0 && sb->vpc <= spp->pgs_per_sb);
    sb->vpc--;

    // //Added test:
    // int vpc_count = 0;
    // for (int ch_idx = 0; ch_idx < ssd->sp.nchs; ch_idx++) {
    //     for (int lun_idx = 0; lun_idx < ssd->sp.luns_per_ch; lun_idx++) {
    //         for (int pg_idx = 0; pg_idx < ssd->sp.pgs_per_blk; pg_idx++) {
    //             nand_page *page = &ssd->ch[ch_idx].lun[lun_idx].blk[sb->blk_idx].pg[pg_idx];
    //             if (page->status == PG_VALID)
    //             {
    //                 vpc_count++;
    //             }
    //         }
    //     }
    // }
    // bytefs_assert(sb->vpc == vpc_count);
}


/* mark current page as valid */
void mark_page_valid(struct ssd *ssd, ppa *ppa)
{
    // printf("PPA MARK VALID %lX\n", ppa->realppa);
    nand_block *blk = NULL;
    nand_page *pg = NULL;
    ssd_superblock *sb = NULL;
    // line *line;

    /* update page status */
    pg = get_pg(ssd, ppa);
    bytefs_assert_msg(pg->status == PG_FREE, "Real status: %s",
            pg->status == PG_VALID ? "VALID" : "INVALID");
    pg->status = PG_VALID;

    /* update corresponding block status */
    blk = get_blk(ssd, ppa);
    bytefs_assert(blk->vpc >= 0 && blk->vpc < ssd->sp.pgs_per_blk);
    blk->vpc++;

    sb = get_sb(ssd, ppa);
    bytefs_assert(sb->vpc >= 0 && sb->vpc < ssd->sp.pgs_per_sb);
    sb->vpc++;

    // //Added test:
    // int vpc_count = 0;
    // for (int ch_idx = 0; ch_idx < ssd->sp.nchs; ch_idx++) {
    //     for (int lun_idx = 0; lun_idx < ssd->sp.luns_per_ch; lun_idx++) {
    //         for (int pg_idx = 0; pg_idx < ssd->sp.pgs_per_blk; pg_idx++) {
    //             nand_page *page = &ssd->ch[ch_idx].lun[lun_idx].blk[sb->blk_idx].pg[pg_idx];
    //             if (page->status == PG_VALID)
    //             {
    //                 vpc_count++;
    //             }
    //         }
    //     }
    // }
    // bytefs_assert(sb->vpc == vpc_count);
}

void mark_block_free(struct ssd *ssd, ppa *ppa)
{
    ssdparams *spp = &ssd->sp;
    nand_block *blk = get_blk(ssd, ppa);
    nand_page *pg = NULL;
    int i;
    bytefs_assert(blk->npgs == spp->pgs_per_blk);
    for (i = 0; i < spp->pgs_per_blk; i++) {
        /* reset page status */
        pg = &blk->pg[i];
        // bytefs_assert(pg->nsecs == spp->secs_per_pg);
        pg->status = PG_FREE;
    }
    //std::cout<<"Erased blk with channel: "<<ppa->g.ch<<", and lun: "<<ppa->g.lun<<", and blk index: "<<ppa->g.blk<<", the blk pointer is: "<<blk<<std::endl;
    /* reset block status */
    blk->ipc = 0;
    blk->vpc = 0;
    blk->erase_cnt++;
    blk->wp = 0;

    blk->is_candidate = 0;
}
/* This is called after mark_block_free thus no need to clean page */
void mark_sb_free(struct ssd *ssd, ssd_superblock *sb)
{
    ssdparams *spp = &ssd->sp;
    writeline *wtl;
    int i;
    for (i = 0; i < spp->wls_per_sb; i++) {
        wtl = &sb->wtl[i];
        wtl->ipc = 0;
        wtl->vpc = 0;
        wtl->erase_cnt++;
        wtl->wp = 0;
    }
    //std::cout<<"Free sb with blk index: "<<sb->blk_idx<<std::endl;
    sb->ipc = 0;
    sb->vpc = 0;
    sb->erase_cnt++;
    sb->line_wp = 0;

    sb->is_candidate = 0;
}

int is_sb_free(struct ssd *ssd, ssd_superblock *sb)
{
    return sb->ipc == 0 && sb->vpc == 0;
}

int is_block_free(struct ssd *ssd, nand_block *blk)
{
    return blk->ipc == 0 && blk->vpc == 0;
}

// static void gc_read_page(struct ssd *ssd, ppa *ppa)
// {
//     /* advance ssd status, we don't care about how long it takes */
//     if (ssd->sp.enable_gc_delay) {
//         nand_cmd gcr;
//         gcr.type = GC_IO;
//         gcr.cmd = NAND_READ;
//         gcr.stime = 0;
//         ssd_advance_status(ssd, ppa, &gcr);
//     }
// }

// /* move valid page data (already in DRAM) from victim line to a new page */
// static uint64_t gc_write_page(struct ssd *ssd, ppa *old_ppa)
// {
//     ppa new_ppa;
//     nand_lun *new_lun;
//     uint64_t lpn = get_rmap_ent(ssd, old_ppa);

//     bytefs_assert(valid_lpn(ssd, lpn));
//     new_ppa = get_new_page(ssd);
//     /* update maptbl */
//     set_maptbl_ent(ssd, lpn, &new_ppa);
//     /* update rmap */
//     set_rmap_ent(ssd, lpn, &new_ppa);

//     mark_page_valid(ssd, &new_ppa);

//     /* need to advance the write pointer here */
//     ssd_advance_write_pointer(ssd);

//     if (ssd->sp.enable_gc_delay) {
//         nand_cmd gcw;
//         gcw.type = GC_IO;
//         gcw.cmd = NAND_WRITE;
//         gcw.stime = 0;
//         ssd_advance_status(ssd, &new_ppa, &gcw);
//     }

//     /* advance per-ch gc_endtime as well */
// #if 0
//     new_ch = get_ch(ssd, &new_ppa);
//     new_ch->gc_endtime = new_ch->next_ch_avail_time;
// #endif

//     new_lun = get_lun(ssd, &new_ppa);
//     new_lun->gc_endtime = new_lun->next_lun_avail_time;

//     return 0;
// }



/* here ppa identifies the block we want to clean */
// static void clean_one_block(struct ssd *ssd, ppa *ppa)
// {
//     ssdparams *spp = &ssd->sp;
//     nand_page *pg_iter = NULL;
//     int cnt = 0;

//     for (int pg = 0; pg < spp->pgs_per_blk; pg++) {
//         ppa->g.pg = pg;
//         pg_iter = get_pg(ssd, ppa);
//         /* there shouldn't be any free page in victim blocks */
//         bytefs_assert(pg_iter->status != PG_FREE);
//         if (pg_iter->status == PG_VALID) {
//             gc_read_page(ssd, ppa);
//             /* delay the maptbl update until "write" happens */
//             gc_write_page(ssd, ppa);
//             cnt++;
//         }
//     }

//     bytefs_assert(get_blk(ssd, ppa)->vpc == cnt);
// }



// static int do_gc(struct ssd *ssd, bool force)
// {
//     line *victim_line = NULL;
//     ssdparams *spp = &ssd->sp;
//     nand_lun *lunp;
//     ppa ppa;
//     int ch, lun;

//     victim_line = select_victim_line(ssd, force);
//     if (!victim_line) {
//         return -1;
//     }

//     ppa.g.blk = victim_line->id;
//     ftl_debug("GC-ing line:%d,ipc=%d,victim=%d,full=%d,free=%d\n", ppa.g.blk,
//               victim_line->ipc, ssd->lm.victim_line_cnt, ssd->lm.full_line_cnt,
//               ssd->lm.free_line_cnt);

//     /* copy back valid data */
//     for (ch = 0; ch < spp->nchs; ch++) {
//         for (lun = 0; lun < spp->luns_per_ch; lun++) {
//             ppa.g.ch = ch;
//             ppa.g.lun = lun;
//             ppa.g.pl = 0;
//             lunp = get_lun(ssd, &ppa);
//             clean_one_block(ssd, &ppa);
//             mark_block_free(ssd, &ppa);

//             if (spp->enable_gc_delay) {
//                 nand_cmd gce;
//                 gce.type = GC_IO;
//                 gce.cmd = NAND_ERASE;
//                 gce.stime = 0;
//                 ssd_advance_status(ssd, &ppa, &gce);
//             }

//             lunp->gc_endtime = lunp->next_lun_avail_time;
//         }
//     }

//     /* update line status */
//     mark_line_free(ssd, &ppa);

//     return 0;
// }



static bool lpa_in_imt(struct ssd *ssd, uint64_t lpa) {
    ssd->indirection_mt_lock.lock();
    map<uint64_t, uint64_t>::iterator imte = ssd->indirection_mt.find(lpa);
    if (imte == ssd->indirection_mt.end()) {
        printf("LPA: %ld\n", lpa);
        return false;
    }
    uint64_t log_pos = imte->second;
    ssd->indirection_mt_lock.unlock();
    return log_pos != INVALID_LPA;
}

// 2bssd buffer implementation  //
static void ssd_init_bytefs_buffer(struct ssd *ssd) {
    // memset the buffer region and map them
    ssd->bytefs_log_region_size = (size_t) (ssd_cache_size_byte * write_log_ratio / 4096) * 4096;
    ssd->bytefs_log_region_start = (log_entry *) cache_mapped(ssd->bd, BYTEFS_LOG_REGION_START);
    ssd->bytefs_log_region_end = (log_entry *) cache_mapped(ssd->bd, BYTEFS_LOG_REGION_START + ssd->bytefs_log_region_size);
    ssd->bytefs_log_region_end = ssd->bytefs_log_region_start + ssd->bytefs_log_region_size / sizeof(log_entry);
    ssd->log_flush_hi_threshold = ssd->bytefs_log_region_size * 50 / 100;
    ssd->log_flush_lo_threshold = ssd->bytefs_log_region_size * 0.01 / 100;
    //ssd->indirection_mt.max_load_factor(0.3);
    //ssd->indirection_mt.reserve(ssd->bytefs_log_region_size / BYTEFS_LOG_REGION_GRANDULARITY);
    ssd->log_page_buffer = malloc(PG_SIZE);
    ssd->flush_page_buffer = malloc(PG_SIZE);

    for (log_entry *entry = ssd->bytefs_log_region_start; entry < ssd->bytefs_log_region_end; entry++) {
        entry->lpa = INVALID_LPA;
        memset((void *) entry->data, 0, sizeof(entry->data));
    }
    memset(ssd->log_page_buffer, 0, PG_SIZE);
    memset(ssd->flush_page_buffer, 0, PG_SIZE);

    ssd->log_rp = ssd->bytefs_log_region_start;
    ssd->log_wp = ssd->bytefs_log_region_start;
    ssd->log_size = 0;
}

static inline uint64_t advance_log_head(struct ssd *ssd) {
    log_entry *new_rp, *next_rp;
    while (ssd->log_size.load() > ssd->log_flush_lo_threshold && ssd->log_rp->lpa == INVALID_LPA) {
        new_rp = ssd->log_rp + 1;
        next_rp = ssd->log_rp + 2;
        if (next_rp > ssd->bytefs_log_region_end) bytefs_log("RP goes around");
        ssd->log_rp = next_rp > ssd->bytefs_log_region_end ? ssd->bytefs_log_region_start : new_rp;
        ssd->log_size -= sizeof(log_entry);
    }
    return ssd->log_rp->lpa;
}

static inline void advance_log_tail_cacheline(struct ssd *ssd, uint64_t lpa, void *data) {
    uint64_t lpa_pg = lpa / PG_SIZE;
    log_entry *old_wp = nullptr, *new_wp, *next_wp;
    do {
        if (ssd->log_size.load() + sizeof(log_entry) <= ssd->bytefs_log_region_size) {
            ssd->log_wp_lock.lock();
            if (ssd->log_size.load() + sizeof(log_entry) > ssd->bytefs_log_region_size) {
                ssd->log_wp_lock.unlock();
                continue;
            }
            old_wp = ssd->log_wp;
            new_wp = old_wp + 1;
            next_wp = old_wp + 2;
            if (next_wp > ssd->bytefs_log_region_end) bytefs_log("WP goes around");
            ssd->log_wp = next_wp > ssd->bytefs_log_region_end ? ssd->bytefs_log_region_start : new_wp;
            ssd->log_size += sizeof(log_entry);
            ssd->log_wp_lock.unlock();
            break;
        }
    } while (true);

    old_wp->lpa = lpa;
    // data fill ignored
    ssd->indirection_mt_lock.lock();
    map<uint64_t, uint64_t>::iterator imte = ssd->indirection_mt.find(lpa);
    log_entry *entry = (log_entry *) imte->second;
    imte->second = (uint64_t) old_wp;
    if ((uint64_t) entry != INVALID_LPA)
        entry->lpa = INVALID_LPA;
    ssd->indirection_mt_lock.unlock();
}

static int bytefs_should_start_log_flush(struct ssd *ssd) {
    return ssd->log_size.load() >= ssd->log_flush_hi_threshold;
}

static int bytefs_should_end_log_flush(struct ssd *ssd) {
    return ssd->log_size.load() < ssd->log_flush_lo_threshold;
}

static inline void try_order_log_flush(struct ssd *ssd) {
    if (bytefs_should_start_log_flush(ssd))
        ssd->log_flush_required = 1;
}


std::pair<uint64_t, uint64_t> flush_log_region_warmup(struct ssd *ssd, uint64_t tmp_array[64]) {
    uint64_t lat = 0;
    ppa ppa;
    nand_cmd cmd;
    uint64_t wr_modified = 0;

    uint64_t read_page_num = 0;
    uint64_t write_page_num = 0;

    uint64_t flush_current_time = the_clock_pt->get_time_sim(); 
    while (!bytefs_should_end_log_flush(ssd)) {
        if (bytefs_should_start_gc(ssd)) {
            size_t current_log_size = ssd->log_size.load();
            bytefs_log("Log flush interrupted by GC w/capacity %10ld/%10ld (%6.2f%%)",
                current_log_size, ssd->bytefs_log_region_size,
                100.0 * current_log_size / ssd->bytefs_log_region_size);
            break;
        }

        // flush_current_time = get_time_ns();
        const uint64_t current_lpa = advance_log_head(ssd);
        if (current_lpa == INVALID_LPA)
            continue;
        
        const uint64_t current_lpn = current_lpa / PG_SIZE;
        uint64_t offset_debug = (current_lpn % PG_SIZE) / 64;
        bool load_page = false;

        if (dram_subsystem->the_cache.is_hit_nb(current_lpn, flush_current_time)!=0) {

            for (uint64_t bit_idx = 0; bit_idx < PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY; bit_idx++) {
                uint64_t coalescing_offset = bit_idx * BYTEFS_LOG_REGION_GRANDULARITY;
                ssd->indirection_mt_lock.lock();
                map<uint64_t, uint64_t>::iterator imte =
                        ssd->indirection_mt.find(current_lpn * PG_SIZE + coalescing_offset);
                if (imte == ssd->indirection_mt.end()) {
                    ssd->indirection_mt_lock.unlock();
                    load_page = true;
                    break;
                }
                log_entry *coalescing_entry = (log_entry *) imte->second;
                if ((uint64_t) coalescing_entry == INVALID_LPA || coalescing_entry->lpa == INVALID_LPA) {
                    ssd->indirection_mt_lock.unlock();
                    load_page = true;
                    break;
                }
                ssd->indirection_mt_lock.unlock();
            }
        }

        if (load_page) {
            flush_current_time = max(flush_current_time, (uint64_t)the_clock_pt->get_time_sim()); 
            ssd->maptbl_update_mutex.lock();
            ppa = get_maptbl_ent(ssd, current_lpn);
            if (mapped_ppa(&ppa)) {
                // found, read out first
                cmd.type = INTERNAL_TRANSFER;
                cmd.cmd = NAND_READ;
                cmd.stime = flush_current_time;
                lat = ssd_advance_status(ssd, &ppa, &cmd);
                read_page_num++;
                flush_current_time += lat;
                // backend_rw(ssd->bd, ppa.realppa, ssd->flush_page_buffer, 0);
                // update old page information
                mark_page_invalid(ssd, &ppa);
                bytefs_try_add_gc_candidate_ppa(ssd, &ppa);
                set_rmap_ent(ssd, INVALID_LPN, &ppa);
                SSD_STAT_ADD(log_coalescing_rd_page, 1);
            } else {
                memset(ssd->flush_page_buffer, 0, PG_SIZE); 
            }
            ssd->maptbl_update_mutex.unlock();
        } else {
            memset(ssd->flush_page_buffer, 0, PG_SIZE);
        }

        // bring page from nand flash to coalescing buffer
        // try to coalesce all the entries that can be found in current cmt entry
        wr_modified = 0;
        
        ssd->indirection_mt_lock.lock();
        for (uint64_t bit_idx = 0; bit_idx < PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY; bit_idx++) {
            uint64_t coalescing_offset = bit_idx * BYTEFS_LOG_REGION_GRANDULARITY;
            
            map<uint64_t, uint64_t>::iterator imte =
                    ssd->indirection_mt.find(current_lpn * PG_SIZE + coalescing_offset);
            if (imte == ssd->indirection_mt.end()) {

                //assert(bit_idx!= offset_debug);
                continue;
            }
            log_entry *coalescing_entry = (log_entry *) imte->second;
            imte->second = INVALID_LPA;
            
            if ((uint64_t) coalescing_entry == INVALID_LPA || coalescing_entry->lpa == INVALID_LPA)
                continue;
            assert(coalescing_entry >= ssd->bytefs_log_region_start && coalescing_entry < ssd->bytefs_log_region_end);
            memcpy((void *) ((uint64_t) ssd->flush_page_buffer + coalescing_offset), 
                (void *) (coalescing_entry->data), 
                BYTEFS_LOG_REGION_GRANDULARITY);
            coalescing_entry->lpa = INVALID_LPA;
            wr_modified++;
        }
        ssd->log_rp->lpa = INVALID_LPA;
        advance_log_head(ssd);
        ssd->indirection_mt_lock.unlock();

        SSD_STAT_ADD(byte_issue_nand_wr_modified_distribution[wr_modified - 1], 1);

        tmp_array[wr_modified - 1]++;

        // write the page back
        flush_current_time = max(flush_current_time, (uint64_t)the_clock_pt->get_time_sim()); 
        ssd->maptbl_update_mutex.lock();
        ppa = get_new_page(ssd);
        set_maptbl_ent(ssd, current_lpn, &ppa);
        set_rmap_ent(ssd, current_lpn, &ppa);
        mark_page_valid(ssd, &ppa);
        ssd_advance_write_pointer(ssd);

        // nand_page *pg = &ssd->ch[ppa.g.ch].lun[ppa.g.lun].blk[ppa.g.blk].pg[ppa.g.pg];
        // std::cout<<"2New page: channel: "<<ppa.g.ch<<", lun: "<<ppa.g.lun<<", blk: "<<ppa.g.blk<<", page: "<<ppa.g.pg<<", status: "<<pg->status<<std::endl;
        
        // perform data write
        cmd.type = INTERNAL_TRANSFER;
        cmd.cmd = NAND_WRITE;
        cmd.stime = flush_current_time;
        ssd_advance_status(ssd, &ppa, &cmd);
        write_page_num++;
        ssd->maptbl_update_mutex.unlock();
        backend_rw(ssd->bd, ppa.realppa, ssd->flush_page_buffer, 1);
        // operate on remaining part of the log if any
        SSD_STAT_ADD(log_wr_page, 1);
    }
    ssd->log_flush_required = 0;

    return std::make_pair(read_page_num, write_page_num);
}



static uint64_t flush_log_region(struct ssd *ssd) {
    uint64_t lat = 0;
    ppa ppa;
    nand_cmd cmd;
    uint64_t wr_modified = 0;

    uint64_t flush_current_time = the_clock_pt->get_time_sim(); 
    while (!bytefs_should_end_log_flush(ssd)) {
        if (bytefs_should_start_gc(ssd)) {
            size_t current_log_size = ssd->log_size.load();
            bytefs_log("Log flush interrupted by GC w/capacity %10ld/%10ld (%6.2f%%)",
                current_log_size, ssd->bytefs_log_region_size,
                100.0 * current_log_size / ssd->bytefs_log_region_size);
            break;
        }

        // flush_current_time = get_time_ns();
        const uint64_t current_lpa = advance_log_head(ssd);
        if (current_lpa == INVALID_LPA)
            continue;
        
        const uint64_t current_lpn = current_lpa / PG_SIZE;
        uint64_t offset_debug = (current_lpn % PG_SIZE) / 64;
        bool load_page = false;

        if (dram_subsystem->the_cache.is_hit_nb(current_lpn, flush_current_time)!=0) {

            for (uint64_t bit_idx = 0; bit_idx < PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY; bit_idx++) {
                uint64_t coalescing_offset = bit_idx * BYTEFS_LOG_REGION_GRANDULARITY;
                ssd->indirection_mt_lock.lock();
                map<uint64_t, uint64_t>::iterator imte =
                        ssd->indirection_mt.find(current_lpn * PG_SIZE + coalescing_offset);
                if (imte == ssd->indirection_mt.end()) {
                    ssd->indirection_mt_lock.unlock();
                    load_page = true;
                    break;
                }
                log_entry *coalescing_entry = (log_entry *) imte->second;
                if ((uint64_t) coalescing_entry == INVALID_LPA || coalescing_entry->lpa == INVALID_LPA) {
                    ssd->indirection_mt_lock.unlock();
                    load_page = true;
                    break;
                }
                ssd->indirection_mt_lock.unlock();
            }
        }

        if (load_page) {
            flush_current_time = max(flush_current_time, (uint64_t)the_clock_pt->get_time_sim()); 
            ssd->maptbl_update_mutex.lock();
            ppa = get_maptbl_ent(ssd, current_lpn);
            if (mapped_ppa(&ppa)) {
                // found, read out first
                cmd.type = INTERNAL_TRANSFER;
                cmd.cmd = NAND_READ;
                cmd.stime = flush_current_time;
                lat = ssd_advance_status(ssd, &ppa, &cmd);
                flush_current_time += lat;
                // backend_rw(ssd->bd, ppa.realppa, ssd->flush_page_buffer, 0);
                // update old page information
                mark_page_invalid(ssd, &ppa);
                bytefs_try_add_gc_candidate_ppa(ssd, &ppa);
                set_rmap_ent(ssd, INVALID_LPN, &ppa);
                SSD_STAT_ADD(log_coalescing_rd_page, 1);
            } else {
                memset(ssd->flush_page_buffer, 0, PG_SIZE); 
            }
            ssd->maptbl_update_mutex.unlock();
        } else {
            memset(ssd->flush_page_buffer, 0, PG_SIZE);
        }

        // bring page from nand flash to coalescing buffer
        // try to coalesce all the entries that can be found in current cmt entry
        wr_modified = 0;
        
        ssd->indirection_mt_lock.lock();
        for (uint64_t bit_idx = 0; bit_idx < PG_SIZE / BYTEFS_LOG_REGION_GRANDULARITY; bit_idx++) {
            uint64_t coalescing_offset = bit_idx * BYTEFS_LOG_REGION_GRANDULARITY;
            
            map<uint64_t, uint64_t>::iterator imte =
                    ssd->indirection_mt.find(current_lpn * PG_SIZE + coalescing_offset);
            if (imte == ssd->indirection_mt.end()) {

                //assert(bit_idx!= offset_debug);
                continue;
            }
            log_entry *coalescing_entry = (log_entry *) imte->second;
            imte->second = INVALID_LPA;
            
            if ((uint64_t) coalescing_entry == INVALID_LPA || coalescing_entry->lpa == INVALID_LPA)
                continue;
            assert(coalescing_entry >= ssd->bytefs_log_region_start && coalescing_entry < ssd->bytefs_log_region_end);
            memcpy((void *) ((uint64_t) ssd->flush_page_buffer + coalescing_offset), 
                (void *) (coalescing_entry->data), 
                BYTEFS_LOG_REGION_GRANDULARITY);
            coalescing_entry->lpa = INVALID_LPA;
            wr_modified++;
        }
        ssd->log_rp->lpa = INVALID_LPA;
        advance_log_head(ssd);
        ssd->indirection_mt_lock.unlock();

        SSD_STAT_ADD(byte_issue_nand_wr_modified_distribution[wr_modified - 1], 1);
        // write the page back
        flush_current_time = max(flush_current_time, (uint64_t)the_clock_pt->get_time_sim()); 
        ssd->maptbl_update_mutex.lock();
        ppa = get_new_page(ssd);
        set_maptbl_ent(ssd, current_lpn, &ppa);
        set_rmap_ent(ssd, current_lpn, &ppa);
        mark_page_valid(ssd, &ppa);
        ssd_advance_write_pointer(ssd);

        // nand_page *pg = &ssd->ch[ppa.g.ch].lun[ppa.g.lun].blk[ppa.g.blk].pg[ppa.g.pg];
        // std::cout<<"2New page: channel: "<<ppa.g.ch<<", lun: "<<ppa.g.lun<<", blk: "<<ppa.g.blk<<", page: "<<ppa.g.pg<<", status: "<<pg->status<<std::endl;
        
        // perform data write
        cmd.type = INTERNAL_TRANSFER;
        cmd.cmd = NAND_WRITE;
        cmd.stime = flush_current_time;
        ssd_advance_status(ssd, &ppa, &cmd);
        ssd->maptbl_update_mutex.unlock();
        backend_rw(ssd->bd, ppa.realppa, ssd->flush_page_buffer, 1);
        // operate on remaining part of the log if any
        SSD_STAT_ADD(log_wr_page, 1);
    }
    ssd->log_flush_required = 0;
    return lat;
}



void force_flush_log(void) {
    ssd *ssd = &gdev;
    // signal ftl thread to flush region
    bytefs_log("Force flush ordered");
    ssd->log_flush_required = 1;
}

static inline uint64_t read_cacheline(struct ssd *ssd, uint64_t lpa, void *data, uint64_t stime) {
    ppa ppa;
    nand_cmd cmd;
    uint64_t lat = 0;
    // find the entry in the indirection mapping table
    if (!lpa_in_imt(ssd, lpa)) {
        goto cacheline_read_fail;
    }
    return lat;

cacheline_read_fail:
    // load page and copy
    ppa = get_maptbl_ent(ssd, lpa / PG_SIZE);
    bytefs_assert(mapped_ppa(&ppa));
    cmd.type = USER_IO;
    cmd.cmd = NAND_READ;
    cmd.stime = stime;
    lat = ssd_advance_status(ssd, &ppa, &cmd);

    // aquire the ownership of the log page buffer
    log_page_buffer_mutex.lock();
    backend_rw(ssd->bd, ppa.realppa, ssd->log_page_buffer, 0);
    // actual read not performed here
    log_page_buffer_mutex.unlock();
    return lat;
}

static inline uint64_t read_page(struct ssd *ssd, uint64_t lpa, void *data, uint64_t stime) {
    ppa ppa;
    nand_cmd cmd;
    uint64_t lat = 0;
    // find the entry in the coalescing mapping table
    bytefs_assert(lpa % PG_SIZE == 0);
    for (uint64_t in_page_offset = 0; in_page_offset < PG_SIZE; in_page_offset += BYTEFS_LOG_REGION_GRANDULARITY) {
        if (!lpa_in_imt(ssd, lpa + in_page_offset))
            goto page_read_fail;
    }
    return lat;

page_read_fail:
    // load page to buffer
    ppa = get_maptbl_ent(ssd, lpa / PG_SIZE);
    bytefs_assert(mapped_ppa(&ppa));
    cmd.type = USER_IO;
    cmd.cmd = NAND_READ;
    cmd.stime = stime;
    lat = ssd_advance_status(ssd, &ppa, &cmd);
    backend_rw(ssd->bd, ppa.realppa, data, 0);
    return lat;
}

static inline uint64_t read_data(struct ssd *ssd, uint64_t lpa, uint64_t size, void *data, uint64_t stime) {
    if (size == BYTEFS_LOG_REGION_GRANDULARITY)
        return read_cacheline(ssd, lpa, data, stime);
    else if (size == PG_SIZE)
        return read_page(ssd, lpa, data, stime);
    bytefs_assert(false);
    return 0;
}

static inline uint64_t write_data(struct ssd *ssd, uint64_t lpa, uint64_t size, void *data, uint64_t stime) {
    int mutex_status;
    uint64_t flush_start_time, flush_end_time;
    uint64_t lpn = lpa / ssd->sp.pgsz;
    uint64_t lat = 0;
    uint64_t current_size = 0, current_offset = 0;
    SSD_STAT_ATOMIC_INC(log_wr_op);
    // hard limit, otherwise more complicated design would be required on bytefs_log_metadata
    bytefs_assert_msg(lpa % BYTEFS_LOG_REGION_GRANDULARITY == 0, "lpa: %ld", lpa);
    bytefs_assert(size > 0);

    if (size == BYTEFS_LOG_REGION_GRANDULARITY)
        advance_log_tail_cacheline(ssd, lpa, data);
    else if (size == PG_SIZE)
        bytefs_assert_msg(false, "LPA: %lx size: %ld", lpa, size);
    else
        bytefs_assert_msg(false, "LPA: %lx size: %ld", lpa, size);

    try_order_log_flush(ssd);
    return lat;
}

// /** Block read
//  *
//  */
// static uint64_t block_read(struct ssd *ssd, cntrl_event *ctl)
// {
//     NvmeRwCmd* req = (NvmeRwCmd*)(&(ctl->cmd));
//     uint64_t lpa = req->slba * ssd->sp.pgsz;
//     uint64_t size = req->nlb * ssd->sp.pgsz;

//     // uint64_t stime = get_time_ns();
//     uint64_t maxlat = read_data(ssd, lpa, size, (void *) req->prp1, get_time_ns());
//     SSD_STAT_ATOMIC_INC(block_rissue_count);
//     SSD_STAT_ATOMIC_ADD(block_rissue_traffic, size);

//     return maxlat;
// }

static inline uint64_t single_block_write(struct ssd *ssd, uint64_t lpn, void *buf, uint64_t stime) {
    uint64_t curlat = 0, maxlat = 0;
    ppa ppa;
    nand_cmd swr;

    ssd->maptbl_update_mutex.lock();
    ppa = get_maptbl_ent(ssd, lpn);
    if (mapped_ppa(&ppa)) {
        /* update old page information first */
        mark_page_invalid(ssd, &ppa);
        set_rmap_ent(ssd, INVALID_LPN, &ppa);
    }

    /* new write */
    ppa = get_new_page(ssd);

    // bytefs_log("write request: lpn=%lu, ppa=%lu ", lpn, ppa.realppa);
    /* update maptbl */
    set_maptbl_ent(ssd, lpn, &ppa);
    /* update rmap */
    set_rmap_ent(ssd, lpn, &ppa);

    // don't need to mark valid here?
    ppa2pgidx(ssd, &ppa);
    mark_page_valid(ssd, &ppa);

    /* need to advance the write pointer here */
    ssd_advance_write_pointer(ssd);

    // nand_page *pg = &ssd->ch[ppa.g.ch].lun[ppa.g.lun].blk[ppa.g.blk].pg[ppa.g.pg];
    // std::cout<<"1New page: channel: "<<ppa.g.ch<<", lun: "<<ppa.g.lun<<", blk: "<<ppa.g.blk<<", page: "<<ppa.g.pg<<", status: "<<pg->status<<std::endl;


    ssd->maptbl_update_mutex.unlock();

    swr.type = USER_IO;
    swr.cmd = NAND_WRITE;
    swr.stime = stime;
    /* get latency statistics */
    curlat = ssd_advance_status(ssd, &ppa, &swr);
    // actually write page into addr @TODO check it this is safe
    backend_rw(ssd->bd, ppa.realppa, buf, 1);

    return 0;
}


// static uint64_t block_write(struct ssd *ssd, cntrl_event *ctl)
// {
//     NvmeRwCmd* req = (NvmeRwCmd*)(&(ctl->cmd));
//     uint64_t lba = req->slba;
//     ssdparams *spp = &ssd->sp;
//     int nlb = req->nlb;
//     uint64_t start_lpn = lba;
//     uint64_t end_lpn = lba + nlb;
//     ppa ppa;
//     uint64_t lpn;
//     uint64_t curlat = 0, maxlat = 0;
//     nand_cmd swr;

//     for (lpn = start_lpn; lpn < end_lpn; lpn++) {

//         ssd->maptbl_update_mutex.lock();
//         ppa = get_maptbl_ent(ssd, lpn);
//         if (mapped_ppa(&ppa)) {
//             /* update old page information first */
//             mark_page_invalid(ssd, &ppa);
//             set_rmap_ent(ssd, INVALID_LPN, &ppa);
//         }

//         /* new write */
//         ppa = get_new_page(ssd);

//         // bytefs_log("write request: lpn=%lu, ppa=%lu ", lpn, ppa.realppa);
//         /* update maptbl */
//         set_maptbl_ent(ssd, lpn, &ppa);
//         /* update rmap */
//         set_rmap_ent(ssd, lpn, &ppa);

//         // don't need to mark valid here?
//         ppa2pgidx(ssd, &ppa);
//         mark_page_valid(ssd, &ppa);

//         /* need to advance the write pointer here */
//         ssd_advance_write_pointer(ssd);
//         ssd->maptbl_update_mutex.unlock();

//         swr.type = USER_IO;
//         swr.cmd = NAND_WRITE;
//         swr.stime = ctl->s_time;
//         /* get latency statistics */
//         curlat = ssd_advance_status(ssd, &ppa, &swr);
//         maxlat = (curlat > maxlat) ? curlat : maxlat;
//         //actually write page into addr @TODO check it this is safe
//         backend_rw(ssd->bd, ppa.realppa, (void *) req->prp1, 1);
//     }
//     SSD_STAT_ATOMIC_INC(block_wissue_count);
//     SSD_STAT_ATOMIC_ADD(block_wissue_traffic, nlb * ssd->sp.pgsz);

//     return maxlat;
// }

void *ftl_thread(void* arg) {
    ftl_thread_info *info = (ftl_thread_info*) arg;
    ssd *ssd = &gdev;
    cntrl_event *ctl_event = NULL;
    NvmeRwCmd *req = NULL;
    uint64_t lat = 0;
    volatile uint64_t flush_start_time, flush_end_time;
    volatile uint64_t gc_start_time, gc_end_time;
    int rc;
    int i;
    // int debug_rpush = 0;

    // while (!*(ssd->dataplane_started_ptr)) {
    //     usleep(100000);
    // }

    //init time here
    /* measure monotonic time for nanosecond precision */
    // clock_gettime(CLOCK_MONOTONIC, &start);
    bytefs_log("ftl thread: start time %lu", the_clock_pt->get_time_sim()); 

    ssd->to_ftl = info->to_ftl;
    ssd->to_poller = info->to_poller;

    // main loop
    // while (!kthread_should_stop()) {
    volatile uint8_t *const terminate_flag = &ssd->terminate_flag;
    uint64_t num_log_flush = 0;
    uint64_t num_gc = 0;
    the_clock_pt->wait_without_events(ThreadType::Ftl_thread, 0);

    while (*terminate_flag == 0) {
        // if flush is ordered, do the flush immediately
        if (ssd->log_flush_required) {
            num_log_flush++;
            flush_start_time = the_clock_pt->get_time_sim();  
            uint64_t current_log_size = ssd->log_size.load();
            bytefs_log("Log flush (#%3ld) starts w/capacity = %10ld/%10ld (%6.2f%%)",
                num_log_flush, current_log_size, ssd->bytefs_log_region_size,
                100.0 * current_log_size / ssd->bytefs_log_region_size);
            flush_log_region(ssd);
            flush_end_time = the_clock_pt->get_time_sim();  
            current_log_size = ssd->log_size.load();
            bytefs_log("Log flush (#%3ld) ends   w/capacity = %10ld/%10ld (%6.2f%%) (duration: %.6f ms)", 
                num_log_flush, current_log_size, ssd->bytefs_log_region_size,
                100.0 * current_log_size / ssd->bytefs_log_region_size,
                (flush_end_time - flush_start_time) / 1e6);
            size_t rp_loc = (size_t) ssd->log_rp - (size_t) ssd->bytefs_log_region_start;
            size_t wp_loc = (size_t) ssd->log_wp - (size_t) ssd->bytefs_log_region_start;
            size_t total_size = (size_t) ((ssd_cache_size_byte * write_log_ratio / 4096) * 4096);
            bytefs_log("Log RP @ %14ld/%14ld (%8.4f%%), WP @ %14ld/%14ld (%8.4f%%)", 
                rp_loc, total_size, 100.0 * rp_loc / total_size,
                wp_loc, total_size, 100.0 * wp_loc / total_size);
        }
        if (bytefs_should_start_gc(ssd)) {
            num_gc++;
            gc_start_time = the_clock_pt->get_time_sim();  
#if ALLOCATION_SECHEM_LINE
            bytefs_log("GC (#%3ld) starts w/capacity = %10d/%10d (%6.2f%%)",
                num_gc, ssd->total_free_sbs, ssd->sp.sb_per_ssd,
                100.0 * ssd->total_free_sbs / ssd->sp.sb_per_ssd);
#else
            bytefs_log("GC (#%3ld) starts w/capacity = %10d/%10d (%6.2f%%)",
                num_gc, ssd->total_free_blks, ssd->sp.blks_per_ch * ssd->sp.nchs,
                100.0 * ssd->total_free_blks / ssd->sp.blks_per_ch * ssd->sp.nchs);
#endif
            bytefs_gc(ssd);
            gc_end_time = the_clock_pt->get_time_sim();  
#if ALLOCATION_SECHEM_LINE
            bytefs_log("GC (#%3ld) ends   w/capacity = %10d/%10d (%6.2f%%) (duration: %.6f ms)",
                num_gc, ssd->total_free_sbs, ssd->sp.sb_per_ssd,
                100.0 * ssd->total_free_sbs / ssd->sp.sb_per_ssd,
                ((gc_end_time - gc_start_time) / 1e6));
#else
            bytefs_log("GC (#%3ld) ends   w/capacity = %10d/%10d (%6.2f%%) (duration: %.6f ms)",
                num_gc, ssd->total_free_blks, ssd->sp.blks_per_ch * ssd->sp.nchs,
                100.0 * ssd->total_free_blks / ssd->sp.blks_per_ch * ssd->sp.nchs,
                (gc_end_time - gc_start_time / 1e6));
#endif
        }
        // sched_yield();
    }

    return 0;
}

/**
 * Poll completed events for all threads.
*/
void *request_poller_thread(void* arg) {
    ftl_thread_info *info = (ftl_thread_info*) arg;
    ssd *ssd = &gdev;
    cntrl_event* evt = NULL; // haor2 : no idea why this is allocated. To fix.
    bytefs_heap event_queue;
    uint64_t cur_time;
    int i;

    bytefs_log("request poller thread: start time %lu", get_time_ns()); 

    heap_create(&event_queue, MAX_EVENT_HEAP);
    // while (!kthread_should_stop()) {
    volatile uint8_t *const terminate_flag = &ssd->terminate_flag;
    while (*terminate_flag == 0) {
        // for (i = 1; i <= info->num_poller; i++) {
        //     if (!ssd->to_poller[i] || ring_is_empty(ssd->to_poller[i]))
        //         continue;
        //     evt = (cntrl_event*) ring_get(ssd->to_poller[i]);
        //     if (!evt) {
        //         bytefs_expect("FTL to_poller dequeue failed");
        //         continue;
        //     }
        //     if (!evt->if_block) {
        //         heap_insert(&event_queue, evt->expire_time, evt);
        //         // printk(KERN_NOTICE "hahah event enqueue expire: %llu \n", evt->expire_time);
        //     } else {
        //         evt->completed = 1;
        //     }
        // }

        do {
            evt = (cntrl_event *) heap_get_min(&event_queue);
            if (evt != NULL) {
                // cur_time = get_time_ns();
                // printk_ratelimited(KERN_NOTICE "hahah event dequeueeueuueueu cur: %llu expire: %llu \n",cur_time,evt->expire_time);
                if (get_time_ns() >= evt->expire_time) { 
                    // printk(KERN_NOTICE "hahah event expired expire: %llu \n", evt->expire_time);
                    // if (evt->bio != NULL) {
                    //     if ((*evt->if_end_bio) > 1) {
                    //         (*evt->if_end_bio)--;
                    //     } else {
                    //         free(evt->if_end_bio);
                    //     }
                    // }
                    heap_pop_min(&event_queue);
                    evt->completed = 1;
                    // kfree(evt);
                }

            }
            sched_yield();
            // cpu_relax();
        } while (heap_is_full(&event_queue, info->num_poller));
    }
    return 0;
}


// /**
//  * print out the memory layout for emulated 2B-SSD
//  * outputsize source is ftl_mapping.h
// */
// static void ftl_debug_DRAM_MEM_LAYOUT(void) {
// #if (TEST_FTL_DEBUG==TEST_FTL_SHOW_SIZE)
//     /**
//      * CACHE
//      * ------------------------------------------------------------------------------------------------------
//      * | LOG           |  INDIRECTION_MT    | C..._MT |FARR  | PGW | PGR |NANDBUF|MIG_BUF|MIGDESTBUF|C...BUF|
//      * |(2000KB : ~2MB)| (3187200B : ~3MB)  | (800KB) |(~6KB)|(4KB)|(4KB)| (4KB) | (4KB) |  (4KB)   | (4KB) |
//      * ------------------------------------------------------------------------------------------------------
//     */
//     printk("LOGSIZE : %lu\n",          (long unsigned int)LOG_SIZE);
//     printk("IMTSIZE : %lu\n",          (long unsigned int)INDIRECTION_MT_SIZE);
//     printk("CMTSIZE : %lu\n",          (long unsigned int)COALESCING_MT_SIZE);
//     printk("FARRSIZE : %lu\n",         (long unsigned int)FLUSHED_ARRAY_SIZE);
//     printk("PGWRBUFSIZE : %lu\n",      (long unsigned int)PG_WR_FLUSH_BUF_SIZE);
//     printk("PGRDBUFSIZE : %lu\n",      (long unsigned int)PG_RD_FLUSH_BUF_SIZE);
//     printk("PGRDNANDBUFSIZE : %lu\n",  (long unsigned int)PG_RD_NAND_BUF_SIZE);
//     printk("MIGLOGBUF : %lu\n",        (long unsigned int)MIGRATION_LOG_BUF_SIZE);
//     printk("MIGDESTBUFSIZE : %lu\n",   (long unsigned int)MIGRATION_DEST_BUF_SIZE);
//     printk("CBUF_SIZE : %lu\n",        (long unsigned int)COALESCING_BUF_SIZE);
//     printk("TOTAL_SIZE : %lu\n",       (long unsigned int)DRAM_END);
// #endif
// }

/**
* Initialize ssd parameters for emulated 2B-SSD, initialize related functionality of the emulated hardware.
* SIDE-EFFECT : one kthread created.
* >6GB host's memory will be used, check backend.c to see details.
*/
int ssd_init() {
    int error = 0;
    ssd *ssd = &gdev;
    ssdparams *spp = &ssd->sp;
    int i, ret;

    if (inited_flag == 1)
        return 0;
    else
        inited_flag = 1;

    bytefs_assert(ssd);
    memset(ssd, 0, sizeof(ssd));

    ssd_init_params(spp);
    bytefs_log("Init para");
    
    /* initialize ssd internal layout architecture */
    ssd->ch = (ssd_channel *) malloc(sizeof(ssd_channel) * spp->nchs);
    memset(ssd->ch, 0, sizeof(ssd_channel) * spp->nchs);
    /* 512 KB to emulate SSD storage hierarchy will be allocated besides the 6GB emulation */
    for (i = 0; i < spp->nchs; i++) {
        ssd_init_ch(&ssd->ch[i], spp);
    }

    /* writeline-level allocation scheme */
    ssd->sb = (ssd_superblock *) malloc(sizeof(ssd_superblock) * spp->sb_per_ssd);
    memset(ssd->sb, 0, sizeof(ssd_superblock) * spp->sb_per_ssd);
    for (i = 0; i < spp->sb_per_ssd; i++) {
        ssd_init_sb(&ssd->sb[i], spp);
    }

    bytefs_log("ByteFS init maptbl");
    /* initialize maptbl */
    ssd_init_maptbl(ssd);
    bytefs_log("ByteFS init remap");
    /* initialize rmap */
    ssd_init_rmap(ssd);

    /* initialize GC related structures */
    bytefs_log("ByteFS init GC facility");
    bytefs_gc_init(ssd);
    /* initialize write pointer, this is how we allocate new pages for writes */
    bytefs_log("ByteFS init wp");
    ssd_init_write_pointer(ssd);

    /* initialize all queues */
    bytefs_log("ByteFS init all queues");
    ssd_init_queues(ssd);
    /* initialize bgackend */
    bytefs_log("ByteFS init backend");
    ret = init_dram_backend(&(ssd->bd), TOTAL_SIZE, 0);

    /* initialize buffer */
    bytefs_log("ByteFS init in device log buffer");
    ssd_init_bytefs_buffer(ssd);

    dummy_buffer = malloc(PG_SIZE);
    ssd->ftl_thread_id = nullptr;
    ssd->polling_thread_id = nullptr;
    ssd->log_writer_thread_id = nullptr;
    ssd->promotion_thread_id = nullptr;
    ssd->log_flush_required = 0;

    /*initialize DRAM subsystem*/
    long cache_size = write_log_enable ? (long)(ssd_cache_size_byte*(1-write_log_ratio)) : ssd_cache_size_byte;
    int64_t host_way = astriflash_enable ? 8 : (host_dram_size_byte / 4096) / 8;
    dram_subsystem = new cache_controller(cache_size, ssd_cache_way, 7, 10000, host_dram_size_byte, host_way);

    ssd->terminate_flag = 0;
    error = bytefs_start_threads();

    return error;
}



int ssd_reset_skybyte(){
    int error = 0;
    ssd *ssd = &gdev;
    ssdparams *spp = &ssd->sp;
    int i, ret;


    bytefs_assert(ssd);
    free(ssd->ch);
    free(ssd->sb);
    free(dummy_buffer);
    delete dram_subsystem;

    memset(ssd, 0, sizeof(ssd));

    ssd_init_params(spp);
    bytefs_log("Init para");
    
    /* initialize ssd internal layout architecture */
    ssd->ch = (ssd_channel *) malloc(sizeof(ssd_channel) * spp->nchs);
    memset(ssd->ch, 0, sizeof(ssd_channel) * spp->nchs);
    /* 512 KB to emulate SSD storage hierarchy will be allocated besides the 6GB emulation */
    for (i = 0; i < spp->nchs; i++) {
        ssd_init_ch(&ssd->ch[i], spp);
    }

    /* writeline-level allocation scheme */
    ssd->sb = (ssd_superblock *) malloc(sizeof(ssd_superblock) * spp->sb_per_ssd);
    memset(ssd->sb, 0, sizeof(ssd_superblock) * spp->sb_per_ssd);
    for (i = 0; i < spp->sb_per_ssd; i++) {
        ssd_init_sb(&ssd->sb[i], spp);
    }

    bytefs_log("ByteFS init maptbl");
    /* initialize maptbl */
    ssd_init_maptbl(ssd);
    bytefs_log("ByteFS init remap");
    /* initialize rmap */
    ssd_init_rmap(ssd);

    /* initialize GC related structures */
    bytefs_log("ByteFS init GC facility");
    bytefs_gc_init(ssd);
    /* initialize write pointer, this is how we allocate new pages for writes */
    bytefs_log("ByteFS init wp");
    ssd_init_write_pointer(ssd);

    /* initialize all queues */
    bytefs_log("ByteFS init all queues");
    ssd_init_queues(ssd);
    /* initialize bgackend */
    bytefs_log("ByteFS init backend");
    ret = init_dram_backend(&(ssd->bd), TOTAL_SIZE, 0);

    /* initialize buffer */
    bytefs_log("ByteFS init in device log buffer");
    ssd_init_bytefs_buffer(ssd);

    dummy_buffer = malloc(PG_SIZE);
    ssd->ftl_thread_id = nullptr;
    ssd->polling_thread_id = nullptr;
    ssd->log_writer_thread_id = nullptr;
    ssd->promotion_thread_id = nullptr;
    ssd->log_flush_required = 0;

    /*initialize DRAM subsystem*/
    long cache_size = write_log_enable ? (long)(ssd_cache_size_byte*(1-write_log_ratio)) : ssd_cache_size_byte;
    int64_t host_way = astriflash_enable ? 8 : (host_dram_size_byte / 4096) / 8;
    dram_subsystem = new cache_controller(cache_size, ssd_cache_way, 7, 10000, host_dram_size_byte, host_way);

    ssd->terminate_flag = 0;
    error = bytefs_start_threads();

    return error;
}


int ssd_reset(void) {
    int i, j, k, l;
    ssd *ssd = &gdev;
    ssdparams *spp = &ssd->sp;
    int error;

    // kill the thread first
    error = bytefs_stop_threads();

    // clear all information for pages and blocks
    bytefs_log("ByteFS reset clear buffer info");
    for (i = 0; i < spp->nchs; i++) {
        for (j = 0; j < spp->luns_per_ch; j++) {
                for (k = 0; k < spp->blks_per_lun; k++) {
                    for (l = 0; l < spp->pgs_per_blk; l++) {
                        ssd->ch[i].lun[j].blk[k].pg[l].pg_num = i;
                        ssd->ch[i].lun[j].blk[k].pg[l].status = PG_FREE;
                    }
                    ssd->ch[i].lun[j].blk[k].ipc = 0;
                    // vpc here should be inited to 0
                    // ssd->ch[i].lun[j].blk[k].vpc = spp->pgs_per_blk;
                    ssd->ch[i].lun[j].blk[k].vpc = 0;
                    ssd->ch[i].lun[j].blk[k].erase_cnt = 0;
                    ssd->ch[i].lun[j].blk[k].wp = 0;
                }
                ssd->ch[i].lun[j].next_lun_avail_time = 0;
                ssd->ch[i].lun[j].next_log_flush_lun_avail_time = 0;
                ssd->ch[i].lun[j].this_lun_avail_time = 0;
                ssd->ch[i].lun[j].busy = false;
        }
        ssd->ch[i].next_ch_avail_time = 0;
        ssd->ch[i].busy = 0;
    }

    // mapping
    // for (i = 0; i < spp->tt_pgs; i++) {
    //     ssd->maptbl[i].realppa = UNMAPPED_PPA;
    // }
    for (i = 0; i < spp->tt_pgs; i++) {
        ssd->rmap[i] = INVALID_LPN;
    }

    // reset GC facilities
    bytefs_log("ByteFS reset GC facilities");
    bytefs_gc_reset(ssd);
    // reset write pointer
    bytefs_log("ByteFS reset write pointer");
    ssd_init_write_pointer(ssd);
    // reset bytefs log related structure
    bytefs_log("ByteFS reset associated log structures");
    ssd_init_bytefs_buffer(ssd);

    // drain ring buffer
    bytefs_log("ByteFS reset drain ring buffer");
    for (i = 0; i < ssd->thread_args->num_poller; i++) {
        while (ssd->to_ftl[i] && !ring_is_empty(ssd->to_ftl[i]))
            ring_get(ssd->to_ftl[i]);
        while (ssd->to_ftl[i] && !ring_is_empty(ssd->to_poller[i]))
            ring_get(ssd->to_poller[i]);
    }

    // TODO reset buffer here
    error = bytefs_start_threads();

    return 0;
}



void *simulator_timer_thread(void *thread_args){
    ssd *ssd = &gdev;
    sim_clock the_clock(0, param.logical_core_num+1);
    the_clock_pt = &the_clock;

    while (ssd->terminate_flag == 0)
    {
        the_clock.check_pop_and_incre_time();
    }
    return nullptr;
}



void *log_writer_thread(void *thread_args) {
    ssd *ssd = &gdev;
    while (ssd->terminate_flag == 0) {
        log_write lw;
        if (!dram_subsystem->WritelogQueue.pop(lw))
            continue;
        write_data(&gdev, lw.lpa, lw.size, dummy_buffer, lw.stime);
        SSD_STAT_ATOMIC_INC(byte_wissue_count);
        SSD_STAT_ATOMIC_ADD(byte_wissue_traffic, lw.size);
        //sleepns(2000);
    }
    return nullptr;
}


void *promotion_thread(void *thread_args) {
    ssd *ssd = &gdev;
    bool thread_waiting = false;
        // while (1) {}
    while (ssd->terminate_flag == 0) {
        // while (ssd->run_flag == 0) {}
        page_promotion_migration mig; //= dram_subsystem->PromotionQueue.dequeue();
        if (!dram_subsystem->PromotionQueue.pop(mig)) { 
            if (!thread_waiting)
            {
                thread_waiting = true;
                the_clock_pt->wait_without_events(ThreadType::Page_promotion_thread, 0);
            }
            continue; 
        }
        if (thread_waiting)
        {
            thread_waiting = false;
            the_clock_pt->release_without_events(ThreadType::Page_promotion_thread, 0);
        }
        
        //printf("@@@Try to Promote page addr: %lx to host DRAM!\n", mig.pm_index * 4096);

        // int64_t curr_time = get_time_ns();
        // if (curr_time > mig.stime + 32000)
        // {
        //     continue;
        // }
        eviction host_demotion;
        host_demotion.condition = 0;
        host_demotion.index = 0;
        uint64_t s_time = the_clock_pt->get_time_sim(); 
        if (dram_subsystem->the_cache.is_hit_nb(mig.pm_index, s_time) > 0)
        {
            int64_t delta = dram_subsystem->the_cache.is_hit_nb(mig.pm_index, s_time);

            assert(delta > 0);
            the_clock_pt->enqueue_future_time(s_time + delta, ThreadType::Page_promotion_thread, 0);
            the_clock_pt->wait_for_futuretime(s_time + delta, 0);

            // while (true)
            // {
            //     uint64_t curr_time = get_time_ns();  
            //     if (curr_time > s_time + delta)
            //     {
            //         break;
            //     }  
            // }
        }
        
        uint64_t c_time = the_clock_pt->get_time_sim(); 

        dram_subsystem->the_cache.hold_keep_lock(mig.pm_index);
        dram_subsystem->host_dram.hold_keep_lock(mig.pm_index);
        if (dram_subsystem->the_cache.is_hit_nb(mig.pm_index, c_time) == 0) {
            eviction host_demotion = dram_subsystem->host_dram.miss_evict(mig.pm_index);
            //dram_subsystem->host_dram.free_keep_lock();

            if (host_demotion.condition==2)
            {
                if (!write_log_enable)
                {
                    //uint64_t start_time = get_time_ns();
                    single_block_write(ssd, mig.pm_index, dummy_buffer, the_clock_pt->get_time_sim());  
                    //int64_t diff_time = get_time_ns() - start_time;
                    //printf("%ld\n", diff_time);
                }

                if (tpp_enable)
                {
                    tpp_LRU_mutex.lock();
                    if (LRU_inactive_list.find(host_demotion.index) == LRU_inactive_list.end())
                    {
                        LRU_inactive_list.insert(host_demotion.index);
                    }
                    tpp_LRU_mutex.unlock();
                    NUMA_scan_mutex.lock();
                    NUMA_scan_set.erase(host_demotion.index);
                    NUMA_scan_mutex.unlock();
                }
            }
            
            dram_subsystem->host_dram.insert(mig.pm_index);
            //printf("@@@Promoted page addr: %lx to host DRAM!\n", mig.pm_index * 4096);
            //dram_subsystem->host_dram.cache_evict_mutex.unlock();

            dram_subsystem->the_cache.do_promotion_evict(mig.pm_index);
        }
        dram_subsystem->host_dram.free_keep_lock(mig.pm_index);
        dram_subsystem->the_cache.free_keep_lock(mig.pm_index);

        if (host_demotion.condition==2)
        {
            if (write_log_enable)
            {
                for (int i = 0; i < 64; i++)
                {
                    write_data(&gdev, host_demotion.index*PG_SIZE+i*64, 64, dummy_buffer, mig.stime);
                }
                // single_block_write(ssd, mig.pm_index, dummy_buffer, get_time_ns());
                //SSD_STAT_ATOMIC_INC(byte_wissue_count);
                //SSD_STAT_ATOMIC_ADD(byte_wissue_traffic, size);
                // endtime = get_time_ns();
                //SSD_STAT_ATOMIC_ADD(total_w_lat, endtime - stime);
            }
        }

    }
    return nullptr;
}


// /** user block interface to init an nvme cmd to the ssd device */

// /** Legacy calling interface
// * Testing block write to NVMe SSD time. Without use of BIO.
// * INPUT :
// *   is_write - write or not
// *   lba      - logical block address : used for block I/O reference
// *   len      - r/w length
// *   buf      - in read case : source of input; in write case : destination of output.
// * RETURN :
// *   issued length :
// *   -1 on error
// * SIDE-EFFECT :
// * currently blocking
// */
// int nvme_issue(int is_write, uint64_t lba, uint64_t len, issue_response *resp) {
//     // if((lba + len)* PG_SIZE >= (32ull << 30)) {
//     //     printk(KERN_ERR "bad nvme addr at %llx size = %llx", lba * PG_SIZE, len * PG_SIZE);
//     //     return -1;
//     // }
//     // return 0;


//     ssd *ssd = &gdev;
//     int ret;
//     cntrl_event *e = (cntrl_event *) malloc(sizeof(cntrl_event));
//     uint64_t sec, actrual_t;

//     if (e == NULL) return -1;
//     bytefs_init_nvme(&(e->cmd), is_write ? NVME_CMD_WRITE : NVME_CMD_READ, lba, len, dummy_buffer);

//     e->s_time = 0;
//     e->expire_time = 0;
//     e->reqlat = 0;
//     e->completed = 0;
//     if (is_write)
//         e->if_block = 1;
//     else
//         e->if_block = 0;
//     e->bio = NULL;
//     e->e_time = 0;

//     ret = ring_put(ssd->to_ftl[1], (void *)e);
//     if (ret == -1) {
//         bytefs_log("Ring buffer full");
//         return -EAGAIN;
//     }

//     // read should be blocking
//     while (!e->completed) {
//         sched_yield();
//     }
//     free(e);

//     resp->latency = 0;
//     resp->flag = NORMAL;
//     return 0;
// }

/**
* Testing function for 2B-SSD r/w
* INPUT :
*   is_write - write or not
*   lpa      - byte addressable reference
*   size      - r/w length
*   buf      - in read case : source of input; in write case : destination of output.
* RETURN :
*   0 - on succes
*   always 0
*/
int byte_issue(int is_write, uint64_t lpa, uint64_t size, issue_response *resp) {
    ssd *ssd = &gdev;
    volatile uint64_t stime, endtime;
    long latency = 0;
    long flash_latency = 0;
    
    bool print_flag = false;


    if (size == 0)
        return -1;

    
    stime = the_clock_pt->get_time_sim(); 

    int64_t page_index = lpa / PG_SIZE;
    int cl_offs = (lpa % PG_SIZE) / 64;
    SSD_STAT_ATOMIC_INC(total_access_num);
    bool host_dram_hit = false;


    //TPP: check whether we need a NUMA sampling here
    if (tpp_enable)
    {
        int64_t rest = the_clock_pt->get_time_sim() / NUMA_scan_threshold_ns;
        NUMA_scan_mutex.lock();
        if (rest > NUMA_scan_count){
            NUMA_scan_count++;
            for (int64_t i = 0; i < 64*1024; i++)
            {
                NUMA_scan_set.insert(ordered_memory_space[(i+NUMA_scan_pointer)%(ordered_memory_space.size())]);
            }
            NUMA_scan_pointer = (NUMA_scan_pointer + 64*1024) % ordered_memory_space.size();
        }
        NUMA_scan_mutex.unlock();
    }

    
    //stime = get_time_ns();
    //endtime = stime;
    if (promotion_enable || tpp_enable)
    {
        dram_subsystem->host_dram.hold_keep_lock(page_index);
    }
    
    if ((promotion_enable || tpp_enable) && dram_subsystem->host_dram.is_hit(page_index))
    {
        // In Host DRAM
        // printf("Addr: %x hit in host DRAM!\n", lpa);
        SSD_STAT_ATOMIC_INC(host_dram_hit_num);
        SSD_STAT_ATOMIC_INC(hostandssdDram_hit_num);
        host_dram_hit = true;
        
        
        if (is_write)
        {
            dram_subsystem->host_dram.writehitCL(page_index, cl_offs);
        } else
        {
            dram_subsystem->host_dram.readhitCL(page_index, cl_offs);
        }
        
        dram_subsystem->host_dram.free_keep_lock(page_index);

        //endtime = get_time_ns();  
        
        if (is_simulator_not_emulator)
        {
            latency = 46;
        }
        else
        {
            latency = write_log_enable ? (endtime - stime) : (endtime - stime) + 36; //NOTE: Adjusting the latency value to form a correct model and eliminate the effect of lock contentions.
            latency = latency < 46 ? 46 : latency; //NOTE: Adjust the Min value;
        }
        
        resp->flag = HOST_DRAM_HIT;
        // print_flag = true;
    }
    else
    {
        if (promotion_enable || tpp_enable)
        {
            dram_subsystem->host_dram.free_keep_lock(page_index);
        }
        
        if (write_log_enable && is_write)
        {

            SSD_STAT_ATOMIC_INC(hostandssdDram_hit_num);
            log_write lw;
            lw.lpa = lpa;
            lw.size = size;
            lw.stime = stime;
            while (!dram_subsystem->WritelogQueue.push(lw)) {};

            //endtime = get_time_ns();  
            if (is_simulator_not_emulator)
            {
                latency = 46 + 40; //DRAM + CXL
            }else
            {
                latency = (endtime - stime); 
                //NOTE: Adjusting the latency value to form a correct CXL model and eliminate the effect of lock contentions.
                latency += 30;

                latency = latency < 146 ? 146 : latency; //NOTE: Adjust the Min value;
            }
            resp->flag = WRITE_LOG_W;
            //SSD_STAT_ATOMIC_ADD(total_w_lat, endtime - stime);
        }else
        {
            //Access SSD cache
            //std::cout<<"2"<<std::endl;
            dram_subsystem->the_cache.hold_keep_lock(page_index);
            if (dram_subsystem->the_cache.is_hit_nb(page_index, stime)==0)
            { 
                
                SSD_STAT_ATOMIC_INC(hostandssdDram_hit_num);
                
                //printf("Addr: %x hit in SSD DRAM cache!\n", lpa);
                if (!is_write)
                {
                    dram_subsystem->the_cache.readhitCL(page_index, cl_offs);
                }
                else
                {
                    ///assert(0);
                    dram_subsystem->the_cache.writehitCL(page_index, cl_offs);
                }
                // if (write_log_enable)
                // {  
                //     lpa_in_imt(ssd, lpa);
                //     //imt_get(ssd, lpa); 
                // }

                //Promotion
                if (promotion_enable)
                {
                    int64_t pm_index = dram_subsystem->the_cache.update_and_choose_promotion(page_index);
                    dram_subsystem->the_cache.free_keep_lock(page_index);
                    if (pm_index!=-1)
                    {
                        page_promotion_migration mig;
                        mig.pm_index = pm_index;
                        mig.stime = stime;
                        dram_subsystem->PromotionQueue.bounded_push(mig);
                        //dram_subsystem->PromotionQueue.enqueue(mig);
                    }
                }
                else if (tpp_enable)
                {
                    dram_subsystem->the_cache.free_keep_lock(page_index);

                    NUMA_scan_mutex.lock();

                    if (NUMA_scan_set.find(page_index) != NUMA_scan_set.end()) //sampled
                    {
                        NUMA_scan_mutex.unlock();
                        tpp_LRU_mutex.lock();
                        if (LRU_inactive_list.find(page_index) != LRU_inactive_list.end()) //inactive
                        {
                            LRU_inactive_list.erase(page_index);
                            LRU_active_list.push_front(page_index);
                            if (LRU_active_list.size() > 524288)
                            {
                                LRU_inactive_list.insert(LRU_active_list.back());
                                LRU_active_list.pop_back();
                            }
                            tpp_LRU_mutex.unlock();
                            

                        }else{ //active, then promote
                            tpp_LRU_mutex.unlock();

                            page_promotion_migration mig;
                            mig.pm_index = page_index;
                            mig.stime = stime;
                            dram_subsystem->PromotionQueue.bounded_push(mig);
                        }   
                    } else {
                        NUMA_scan_mutex.unlock();
                    }
                }
                else
                {
                    dram_subsystem->the_cache.free_keep_lock(page_index);
                }
                //endtime = stime;
                //endtime = get_time_ns();

                if (is_simulator_not_emulator)
                {
                    latency = 46 + 40 + 72; // DRAM + CXL + Indexing
                }
                else
                {
                    //NOTE: Adjusting the latency value to form a correct CXL model and eliminate the effect of lock contentions.
                    if (promotion_enable && write_log_enable)
                    {
                        latency = (endtime - stime);
                    }
                    else if (promotion_enable)
                    {
                        latency = (endtime - stime) + 40;
                    }
                    else
                    {
                        latency = (endtime - stime) + 125;
                    }
                    latency = latency < 106 ? 106 : latency; //NOTE: Adjust the Min value;
                }
                
                resp->flag = SSD_CACHE_HIT;
                //latency = 2*SSD_DRAM_WRITE_CACHELINE_LATENCY; 
            }
            else if (write_log_enable && lpa_in_imt(ssd, lpa)) 
            {
                SSD_STAT_ATOMIC_INC(hostandssdDram_hit_num);
                dram_subsystem->the_cache.free_keep_lock(page_index);
                //SSD_STAT_ATOMIC_INC(hostandssdDram_hit_num);
                //endtime = stime;
                //latency = 2*SSD_DRAM_WRITE_CACHELINE_LATENCY; 
                
                //endtime = get_time_ns(); 
                if (is_simulator_not_emulator)
                {
                    latency = 46 + 40 + 72; // DRAM + CXL + Indexing
                }
                else
                {
                    latency = (endtime - stime); //NOTE: Adjusting the latency value to form a correct CXL model and eliminate the effect of lock contentions.
                    latency = latency < 106 ? 106 : latency; //NOTE: Adjust the Min value;
                }

                resp->flag = WRITE_LOG_R;

            }
            else if (dram_subsystem->the_cache.is_hit_nb(page_index, stime)>0)
            {
                //Hit in the MSHR
                bool context_siwtch = false;

                flash_latency = dram_subsystem->the_cache.is_hit_nb(page_index, stime);


                //std::cout<<"Byte_Issue Time: "<<flash_latency<<std::endl;
                resp->flag = SSD_CACHE_MISS;
                if (device_triggered_ctx_swt && flash_latency >= cs_threshold)
                {
                    resp->flag = ONGOING_DELAY;
                    resp->estimated_latency = flash_latency;

                    context_siwtch = true;
                }
                
                if (!is_write)
                {
                    dram_subsystem->the_cache.readhitCL(page_index, cl_offs);
                }
                else
                {
                    ///assert(0);
                    dram_subsystem->the_cache.writehitCL(page_index, cl_offs);
                }

                dram_subsystem->the_cache.free_keep_lock(page_index);

                //endtime = get_time_ns(); 
                if (is_simulator_not_emulator)
                {
                    latency = context_siwtch ? 46 + 40 + 72 : flash_latency + 46 + 40 + 72;
                }
                else
                {
                    latency = context_siwtch ? (endtime - stime) : (endtime - stime) + flash_latency;
                }
                //print_flag = true;
                

            }
            else //SSD DRAM miss
            {
                //endtime = stime;
                //printf("Addr: %x missed in SSD DRAM cache!\n", lpa);
                bool context_siwtch = false;
                //print_flag = true;

                eviction evi = dram_subsystem->the_cache.miss_evict(page_index);

                // if (print_page_locality && (evi.condition!=0))  //baseline only
                // {
                //     m_screen.lock();
                //     std::cout<<"LBA: "<<page_index<<", Accessed: "<<evi.accessed_cl_num<<", Dirty: "<<evi.dirty_cl_num<<std::endl;
                //     m_screen.unlock();
                // }

                flash_latency = read_data(ssd, lpa, size, dummy_buffer, stime);

                if (!write_log_enable && (evi.condition!=0))
                {
                   //uint64_t start_time = get_time_ns();
                   single_block_write(ssd, evi.index, dummy_buffer, stime);
                   //uint64_t diff_time = get_time_ns() - start_time;
                   //printf("%ld\n", diff_time);
                }
                //Read-only cache don't need to flush
                
                SSD_STAT_ATOMIC_INC(byte_rissue_count);
                SSD_STAT_ATOMIC_ADD(byte_rissue_traffic, size);
                SSD_STAT_ATOMIC_INC(total_flash_miss_num);
                SSD_STAT_ATOMIC_ADD(total_miss_latency, (uint64_t)flash_latency);

                //std::cout<<"Byte_Issue Time: "<<flash_latency<<std::endl;
                resp->flag = SSD_CACHE_MISS;
                if (device_triggered_ctx_swt && flash_latency >= cs_threshold)
                {
                    resp->flag = ONGOING_DELAY;
                    resp->estimated_latency = flash_latency;

                    context_siwtch = true;
                }
                
                dram_subsystem->the_cache.insert_nb(page_index, stime + flash_latency, context_siwtch);
                if (!is_write)
                {
                    dram_subsystem->the_cache.readhitCL(page_index, cl_offs);
                }
                else
                {
                    ///assert(0);
                    dram_subsystem->the_cache.writehitCL(page_index, cl_offs);
                }

                //Promotion
                // if (promotion_enable)
                // {
                //     int64_t pm_index = dram_subsystem->the_cache.update_and_choose_promotion(page_index);
                //     dram_subsystem->the_cache.free_keep_lock(page_index);
                //     if (pm_index!=-1)
                //     {
                //         page_promotion_migration mig;
                //         mig.pm_index = pm_index;
                //         mig.stime = stime;
                //         dram_subsystem->PromotionQueue.bounded_push(mig);
                //         //dram_subsystem->PromotionQueue.enqueue(mig);
                //     }
                // }
                // if (promotion_enable)
                // {
                //    int64_t pm_index = dram_subsystem->the_cache.update_and_choose_promotion(page_index);
                //     if (pm_index!=-1)
                //     {
                //         //printf("@@@Promote page addr: %x to host DRAM!\n", pm_index * 4096);
                //         //dram_subsystem->host_dram.cache_evict_mutex.lock();
                //         dram_subsystem->host_dram.hold_keep_lock(pm_index);
                //         eviction host_demotion = dram_subsystem->host_dram.miss_evict(pm_index);
                //         //dram_subsystem->host_dram.free_keep_lock();
                //         if (host_demotion.condition==2)
                //         {
                //             write_data(ssd, host_demotion.index*PG_SIZE, PG_SIZE, dummy_buffer, stime);
                //             SSD_STAT_ATOMIC_INC(byte_wissue_count);
                //             SSD_STAT_ATOMIC_ADD(byte_wissue_traffic, size);
                //             // endtime = get_time_ns();
                //             // SSD_STAT_ATOMIC_ADD(total_w_lat, endtime - stime);
                //         }

                //         dram_subsystem->host_dram.insert(pm_index);
                //         //dram_subsystem->host_dram.cache_evict_mutex.unlock();
                //         dram_subsystem->host_dram.free_keep_lock(pm_index);
                //         // promoted_set.insert(pm_index);
                //         dram_subsystem->the_cache.do_promotion_evict(pm_index);
                //     }
                // }
                dram_subsystem->the_cache.free_keep_lock(page_index);


                if (tpp_enable)
                {
                    NUMA_scan_mutex.lock();
                    if (NUMA_scan_set.find(page_index) != NUMA_scan_set.end()) //sampled
                    {
                        NUMA_scan_mutex.unlock();
                        if (LRU_inactive_list.find(page_index) != LRU_inactive_list.end()) //inactive
                        {
                            LRU_inactive_list.erase(page_index);
                            LRU_active_list.push_front(page_index);
                            if (LRU_active_list.size() > 524288)
                            {
                                LRU_inactive_list.insert(LRU_active_list.back());
                                LRU_active_list.pop_back();
                            }
                        }  
                    } else
                    {
                        NUMA_scan_mutex.unlock();
                    }
                    
                }

                //endtime = get_time_ns(); 
                if (is_simulator_not_emulator)
                {
                    latency = context_siwtch ? 46 + 40 + 72 : flash_latency + 46 + 40 + 72;
                }
                else
                {
                    latency = context_siwtch ? (endtime - stime) : (endtime - stime) + flash_latency;
                }
                //print_flag = true;

            }
        }
    }

    // m_screen.lock();
    // if(endtime - stime!=0)
    if (print_flag)
    {
        std::cout<<"Byte_Issue Time: "<<latency<<std::endl;
    }

    
    // m_screen.unlock();
    
    // latency -= (endtime - stime);
    resp->latency = latency;
    return 0;
}

void bytefs_fill_data(uint64_t addr) {
    ssd *ssd = &gdev;
    ppa ppa;
    uint64_t current_lpn = addr / PG_SIZE;

    for (uint64_t offset = 0; offset < PG_SIZE; offset += BYTEFS_LOG_REGION_GRANDULARITY) {
        ssd->indirection_mt[current_lpn * PG_SIZE + offset] = INVALID_LPA;
    }

    if (tpp_enable)
    {
        ordered_memory_space.push_back(current_lpn);
        LRU_inactive_list.insert(current_lpn);
    }

    if (promotion_enable || tpp_enable)
    {
        dram_subsystem->host_dram.fill(current_lpn);
    }
    
    dram_subsystem->the_cache.fill(current_lpn);

    ppa = get_maptbl_ent(ssd, current_lpn);
    if (mapped_ppa(&ppa))
        return;
    ppa = get_new_page(ssd);
    set_maptbl_ent(ssd, current_lpn, &ppa);
    set_rmap_ent(ssd, current_lpn, &ppa);
    mark_page_valid(ssd, &ppa);
    ssd_advance_write_pointer(ssd);

    // nand_page *pg = &ssd->ch[ppa.g.ch].lun[ppa.g.lun].blk[ppa.g.blk].pg[ppa.g.pg];
    // std::cout<<"3New page: channel: "<<ppa.g.ch<<", lun: "<<ppa.g.lun<<", blk: "<<ppa.g.blk<<", page: "<<ppa.g.pg<<", status: "<<pg->status<<std::endl;
}

bool backend_prefill_data(uint64_t addr) {
    ssd *ssd = &gdev;
    uint64_t current_lpn = addr / PG_SIZE;

    if (ssd->total_free_sbs - 1 <= ssd->free_blk_lo_threshold / ssd->sp.tt_luns)
    {
        return true;
    }
    else
    {
        void *dummy_buffer;
        dummy_buffer = malloc(PG_SIZE);
        single_block_write(ssd, current_lpn, dummy_buffer, 0);
        free(dummy_buffer);
        return false;
    }

}


int64_t get_thecache_dirty_page_num(){
    return dram_subsystem->the_cache.give_dirty_num();
}


int64_t get_hostdram_dirty_page_num(){
    return dram_subsystem->host_dram.give_dirty_num();
}

int64_t get_thecache_accessed_page_num(){
    return dram_subsystem->the_cache.give_accessed_num();
}

int64_t get_hostdram_accessed_page_num(){
    return dram_subsystem->host_dram.give_accessed_num();
}




int64_t get_thecache_dirty_marked_page_num(){
    return dram_subsystem->the_cache.give_marked_dirty_num();
}


int64_t get_hostdram_dirty_marked_page_num(){
    return dram_subsystem->host_dram.give_marked_dirty_num();
}

int64_t get_thecache_accessed_marked_page_num(){
    return dram_subsystem->the_cache.give_marked_accessed_num();
}

int64_t get_hostdram_accessed_marked_page_num(){
    return dram_subsystem->host_dram.give_marked_accessed_num();
}


void copy_dram_system(FILE* output_file){
    dram_subsystem->snapshot(output_file);
}

void replay_dram_system(FILE* input_file){
    dram_subsystem->replay_snapshot(input_file);
}


void copy_tpp_system(FILE* output_file){
    fprintf(output_file, "%ld\n", LRU_inactive_list.size());
    for (const auto &element : LRU_inactive_list) {
        fprintf(output_file, "%ld\n", static_cast<long>(element));  // %llu for printing uint64_t
    }
    int64_t active_size = LRU_active_list.size();
    fprintf(output_file, "%ld\n", active_size);
    for (int64_t i = 0; i < active_size; i++)
    {
        fprintf(output_file, "%ld\n", LRU_active_list.back());
        LRU_active_list.pop_back();
    }
    fprintf(output_file, "%ld\n", NUMA_scan_set.size());
    for (const auto &element : NUMA_scan_set) {
        fprintf(output_file, "%ld\n", static_cast<long>(element));  // %llu for printing uint64_t
    }
}

void replay_tpp_system(FILE* input_file){
    LRU_active_list.clear();
    LRU_inactive_list.clear();
    NUMA_scan_set.clear();
    int64_t inactive_size;
    assert(fscanf(input_file, "%ld\n", &inactive_size));
    for (int64_t i = 0; i < inactive_size; i++)
    {
        int64_t element;
        assert(fscanf(input_file, "%ld\n", &element));
        LRU_inactive_list.insert(element);
    }
    int64_t active_size;
    assert(fscanf(input_file, "%ld\n", &active_size));
    for (int64_t i = 0; i < active_size; i++)
    {
        int64_t element;
        assert(fscanf(input_file, "%ld\n", &element));
        LRU_active_list.push_front(element);
    }
    int64_t numa_size;
    assert(fscanf(input_file, "%ld\n", &numa_size));
    for (int64_t i = 0; i < numa_size; i++)
    {
        int64_t element;
        assert(fscanf(input_file, "%ld\n", &element));
        NUMA_scan_set.insert(element);
    }
    NUMA_scan_threshold_ns = 2000000000;
}


void the_cache_mark_workup(){
    dram_subsystem->the_cache.mark_warmup();
}

void host_dram_mark_workup(){
    dram_subsystem->host_dram.mark_warmup();
}





void warmup_ssd_dram(double warmup_dirty_ratio_cache, double warmup_dirty_ratio_dram, uint64_t read_pgnum, uint64_t write_pgnum,
double cache_overall_cover_rate, double host_overall_cover_rate, double cache_uncovered_dirty_rate, double host_uncovered_dirty_rate){
    std::unordered_set<uint64_t> warmup_pages;
    ssd* ssd = &gdev;
    uint32_t random32bit; //Page index as a 32-bit value

    std::cout<<"Start to Warm up the SSD cache!"<<std::endl;

    while (warmup_pages.size() < (int64_t)((ssd_cache_size_byte / 4096)))
    {
        random32bit = rand() | (rand() << 16);
        warmup_pages.insert(random32bit*PG_SIZE);
    }

    std::vector<uint64_t> warmup_lpns;
    for (auto page : warmup_pages) {
        warmup_lpns.push_back(page/PG_SIZE);
    }
    
    //Prefill the ssd backend
    uint64_t ii = 0;

    for (auto page : warmup_pages) {
        bytefs_fill_data(page);

        uint64_t page_index = page / PG_SIZE;

    
        if (!write_log_enable)
        {
            if (ii%100 < 100*cache_uncovered_dirty_rate)
            {
                // //dirty (write)
                // if (write_log_enable && dirty_cl_per_page > 0)
                // {
                //     for (size_t i = 0; i < dirty_cl_per_page; i++)
                //     {
                //         log_write lw;
                //         lw.lpa = page_index * PG_SIZE + i * 64;
                //         lw.size = 64;
                //         lw.stime = 0;
                //         write_data(&gdev, lw.lpa, lw.size, nullptr, 0);
                //     }
                // }
                if (dram_subsystem->the_cache.is_hit(page_index))
                {
                    dram_subsystem->the_cache.writehitCL(page_index, 0);
                }
                else
                {
                    dram_subsystem->the_cache.miss_evict(page_index);
                    dram_subsystem->the_cache.insert(page_index);
                    dram_subsystem->the_cache.writehitCL(page_index, 0);
                }
            }
            else
            {
                //accessed
                if (dram_subsystem->the_cache.is_hit(page_index))
                {
                    dram_subsystem->the_cache.readhitCL(page_index, 0);
                }
                else
                {
                    dram_subsystem->the_cache.miss_evict(page_index);
                    dram_subsystem->the_cache.insert(page_index);
                    dram_subsystem->the_cache.readhitCL(page_index, 0);
                }
            }
        }
        else //read-only cache
        {
            //accessed
            if (dram_subsystem->the_cache.is_hit(page_index))
            {
                dram_subsystem->the_cache.readhitCL(page_index, 0);
            }
            else
            {
                dram_subsystem->the_cache.miss_evict(page_index);
                dram_subsystem->the_cache.insert(page_index);
                dram_subsystem->the_cache.readhitCL(page_index, 0);
            }
        }
        ii++;
    }


    //Warmup the host DRAM
    if (promotion_enable || tpp_enable)
    {
        std::cout<<"Start to Warm up the Host DRAM!"<<std::endl;
        std::unordered_set<uint64_t> warmup_pages_host;
        uint32_t random32bit; //Page index as a 32-bit value
        uint32_t xor_ = 16462368;

        while (warmup_pages_host.size() < host_dram_size_byte / 4096)
        {
            random32bit = rand() | (rand() << 16);
            xor_ = xor_ * 7 + 23;
            random32bit = random32bit ^ xor_;
            xor_ = xor_ + random32bit;
            warmup_pages_host.insert((uint64_t)(random32bit)*PG_SIZE);
        }

        //Prefill the ssd backend and the host DRAM
        ii = 0;

        for (auto page : warmup_pages_host) {
            bytefs_fill_data(page);

            uint64_t page_index = page / PG_SIZE;

        
            if (ii%100 < 100*host_uncovered_dirty_rate)
            {

                if (dram_subsystem->host_dram.is_hit(page_index))
                {
                    dram_subsystem->host_dram.writehitCL(page_index, 0);
                }
                else
                {
                    dram_subsystem->host_dram.miss_evict(page_index);
                    dram_subsystem->host_dram.insert(page_index);
                    dram_subsystem->host_dram.writehitCL(page_index, 0);
                }
            }
            else
            {
                //accessed
                if (dram_subsystem->host_dram.is_hit(page_index))
                {
                    dram_subsystem->host_dram.readhitCL(page_index, 0);
                }
                else
                {
                    dram_subsystem->host_dram.miss_evict(page_index);
                    dram_subsystem->host_dram.insert(page_index);
                    dram_subsystem->host_dram.readhitCL(page_index, 0);
                }
            }

            ii++;
        }
    }

    //TODO:GC
    ssd_backend_reset_timestamp();

    if (write_log_enable)
    {
        std::cout<<"Start to Warm up the Write Log!"<<std::endl;
        for (size_t i = 0; i < read_pgnum; i++)
        {
            assert(read_pgnum < ssd_cache_size_byte / 4096);
            nand_cmd cmd;
            ppa ppa;
            ssd->maptbl_update_mutex.lock();
            ppa = get_maptbl_ent(ssd, warmup_lpns[i]);
            assert(mapped_ppa(&ppa));
            cmd.type = INTERNAL_TRANSFER;
            cmd.cmd = NAND_READ;
            cmd.stime = 0;
            ssd_advance_status(ssd, &ppa, &cmd);
            ssd->maptbl_update_mutex.unlock();
            SSD_STAT_ADD(log_coalescing_rd_page, 1);
        }
        for (size_t i = 0; i < write_pgnum; i++)
        {
            nand_cmd cmd;
            ppa ppa;
            ssd->maptbl_update_mutex.lock();
            ppa = get_new_page(ssd);
            set_maptbl_ent(ssd, warmup_lpns[i], &ppa);
            set_rmap_ent(ssd, warmup_lpns[i], &ppa);
            mark_page_valid(ssd, &ppa);
            ssd_advance_write_pointer(ssd);

            // perform data write
            cmd.type = INTERNAL_TRANSFER;
            cmd.cmd = NAND_WRITE;
            cmd.stime = 0;
            ssd_advance_status(ssd, &ppa, &cmd);
            ssd->maptbl_update_mutex.unlock();
            SSD_STAT_ADD(log_wr_page, 1);
        }
        
    }

}


void warmup_write_log(uint64_t read_pgnum, uint64_t write_pgnum){
    ssd_backend_reset_timestamp();

    std::unordered_set<uint64_t> warmup_pages;
    ssd* ssd = &gdev;
    uint32_t random32bit; //Page index as a 32-bit value

    std::cout<<"Start to Warm up the SSD write log!"<<std::endl;

    while (warmup_pages.size() < (int64_t)((ssd_cache_size_byte / 4096)))
    {
        random32bit = rand() | (rand() << 16);
        warmup_pages.insert(random32bit*PG_SIZE);
    }
    std::vector<uint64_t> warmup_lpns;
    for (auto page : warmup_pages) {
        warmup_lpns.push_back(page/PG_SIZE);
    }

    if (write_log_enable)
    {
        std::cout<<"Start to Warm up the Write Log!"<<std::endl;
        for (size_t i = 0; i < read_pgnum; i++)
        {
            bytefs_fill_data(warmup_lpns[i]*PG_SIZE);
            assert(read_pgnum < ssd_cache_size_byte / 4096);
            nand_cmd cmd;
            ppa ppa;
            ssd->maptbl_update_mutex.lock();
            ppa = get_maptbl_ent(ssd, warmup_lpns[i]);
            assert(mapped_ppa(&ppa));
            cmd.type = INTERNAL_TRANSFER;
            cmd.cmd = NAND_READ;
            cmd.stime = 0;
            ssd_advance_status(ssd, &ppa, &cmd);
            ssd->maptbl_update_mutex.unlock();
            SSD_STAT_ADD(log_coalescing_rd_page, 1);
        }
        for (size_t i = 0; i < write_pgnum; i++)
        {
            bytefs_fill_data(warmup_lpns[i]*PG_SIZE);
            nand_cmd cmd;
            ppa ppa;
            ssd->maptbl_update_mutex.lock();
            ppa = get_new_page(ssd);
            set_maptbl_ent(ssd, warmup_lpns[i], &ppa);
            set_rmap_ent(ssd, warmup_lpns[i], &ppa);
            mark_page_valid(ssd, &ppa);
            ssd_advance_write_pointer(ssd);

            // perform data write
            cmd.type = INTERNAL_TRANSFER;
            cmd.cmd = NAND_WRITE;
            cmd.stime = 0;
            ssd_advance_status(ssd, &ppa, &cmd);
            ssd->maptbl_update_mutex.unlock();
            SSD_STAT_ADD(log_wr_page, 1);
        }
        
    }
}




void ssd_backend_reset_timestamp(){
    int i, j, k, l;
    ssd *ssd = &gdev;
    ssdparams *spp = &ssd->sp;

    for (i = 0; i < spp->nchs; i++) {
        for (j = 0; j < spp->luns_per_ch; j++) {
                ssd->ch[i].lun[j].next_lun_avail_time = 0;
                ssd->ch[i].lun[j].next_log_flush_lun_avail_time = 0;
                ssd->ch[i].lun[j].this_lun_avail_time = 0;
                ssd->ch[i].lun[j].busy = false;
        }
        ssd->ch[i].next_ch_avail_time = 0;
        ssd->ch[i].busy = 0;
    }
}


void ssd_parameter_dump(void) {
    ssd *ssd = &gdev;
    bytefs_log("Log region");
    bytefs_log("  Region start : %lX", (uint64_t) ssd->bytefs_log_region_start);
    bytefs_log("  Region end   : %lX", (uint64_t) ssd->bytefs_log_region_end);
    bytefs_log("  Read ptr     : %lX", (uint64_t) ssd->log_rp);
    bytefs_log("  Write ptr    : %lX", (uint64_t) ssd->log_wp);
    bytefs_log("  Log size     : %lX", (uint64_t) ssd->log_size);
    bytefs_log("  Pending Flush: %s", ssd->log_flush_required ? "o" : "x");
    bytefs_log("Mapping table");
    bytefs_log("  IMT size     : %ld", (uint64_t) ssd->indirection_mt.size());
    //bytefs_log("  IMT ld factor: %f", ssd->indirection_mt.load_factor());
}