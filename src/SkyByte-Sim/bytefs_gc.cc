#include "ftl.h"
#include "bytefs_gc.h"
#include "bytefs_heap.h"
#include "bytefs_utils.h"
#include "simulator_clock.h"

extern sim_clock* the_clock_pt;

#if ALLOCATION_SECHEM_LINE
static void bytefs_push_back_free_list(struct ssd *ssd, struct ssd_superblock *sb) {
  gc_free_list *free_list = ssd->free_sbs;
  bytefs_assert(sb->vpc == 0);
  bytefs_assert(sb->ipc == 0);
  bytefs_assert(!sb->is_candidate);
  
#ifdef DEBUG
  for (int ch_idx = 0; ch_idx < ssd->sp.nchs; ch_idx++) {
    for (int lun_idx = 0; lun_idx < ssd->sp.luns_per_ch; lun_idx++) {
      for (int pg_idx = 0; pg_idx < ssd->sp.pgs_per_blk; pg_idx++) {
        nand_page *page = &ssd->ch[ch_idx].lun[lun_idx].blk[sb->blk_idx].pg[pg_idx];
        // page->status = PG_FREE;
        bytefs_assert_msg(page->status == PG_FREE, "Real status is: %s", page->status == PG_VALID ? "VALID" : "INVALID");
      }
    }
  }
#endif  
  
  sb->next_sb = NULL;
  if (!free_list->sbs_end) {
    bytefs_assert(!free_list->sbs_start);
    free_list->sbs_start = sb;
  } else {
    free_list->sbs_end->next_sb = sb;
  }
  free_list->sbs_end = sb;
  free_list->num_sbs++;
  ssd->total_free_sbs++;
}
#else
static void bytefs_push_back_free_list(struct ssd *ssd, nand_block *nand_block) {
  gc_free_list *free_list = &ssd->free_blks[nand_block->ch_idx];
  bytefs_assert(nand_block->vpc == 0);
  bytefs_assert(nand_block->ipc == 0);
  nand_block->next_blk = NULL;
  if (!free_list->blocks_end) {
    bytefs_assert(!free_list->blocks_start);
    free_list->blocks_start = nand_block;
  } else {
    free_list->blocks_end->next_blk = nand_block;
  }
  free_list->blocks_end = nand_block;
  free_list->num_blocks++;
  ssd->total_free_blks++;
}
#endif

#if ALLOCATION_SECHEM_LINE
static inline struct ssd_superblock *bytefs_get_front_free_list(struct ssd *ssd) {
  return ssd->free_sbs->sbs_start;
}
#else
static inline nand_block *bytefs_get_front_free_list(struct ssd *ssd, int ch_idx) {
  return ssd->free_blks[ch_idx].blocks_start;
}
#endif


#if ALLOCATION_SECHEM_LINE
static void bytefs_rm_front_free_list(struct ssd *ssd) {
  gc_free_list *free_list = ssd->free_sbs;
  bytefs_assert(free_list->sbs_start);
  bytefs_assert(free_list->num_sbs);
  if (free_list->sbs_start == free_list->sbs_end) {
    bytefs_assert(free_list->num_sbs == 1);
    free_list->sbs_start = NULL;
    free_list->sbs_end = NULL;
  } else {
    free_list->sbs_start = free_list->sbs_start->next_sb;
  }
  free_list->num_sbs--;
  ssd->total_free_sbs--;
}
#else
static void bytefs_rm_front_free_list(struct ssd *ssd, int ch_idx) {
  gc_free_list *free_list = &ssd->free_blks[ch_idx];
  bytefs_assert(free_list->blocks_start);
  bytefs_assert(free_list->num_blocks);
  if (free_list->blocks_start == free_list->blocks_end) {
    bytefs_assert(free_list->num_blocks == 1);
    free_list->blocks_start = NULL;
    free_list->blocks_end = NULL;
  } else {
    free_list->blocks_start = free_list->blocks_start->next_blk;
  }
  free_list->num_blocks--;
  ssd->total_free_blks--;
}
#endif

#if ALLOCATION_SECHEM_LINE
static void bytefs_push_back_candidate_list(struct ssd *ssd, struct ssd_superblock *sb) {
  struct gc_candidate_list *candidate_list = ssd->gc_candidate_sbs;
  ssdparams *spp = &ssd->sp;
  bytefs_expect(sb->vpc <= spp->pgs_per_sb);
  bytefs_assert(!sb->is_candidate);
  sb->is_candidate = 1;
  sb->next_sb = NULL;
  if (!candidate_list->sbs_end) {
    bytefs_assert(!candidate_list->sbs_start);
    candidate_list->sbs_start = sb;
  } else {
    candidate_list->sbs_end->next_sb = sb;
  }
  candidate_list->sbs_end = sb;
  candidate_list->num_sbs++;
}
#else
static void bytefs_push_back_candidate_list(struct ssd *ssd, nand_block *nand_block) {
  int ch_idx = nand_block->ch_idx;
  // int way_idx = nand_block->way_idx;
  // int blk_idx = nand_block->blk_idx;
  struct gc_candidate_list *candidate_list = &ssd->gc_candidate_blks[ch_idx];
  bytefs_expect(nand_block->vpc <= PG_COUNT);
  bytefs_assert(!nand_block->is_candidate);
  nand_block->is_candidate = 1;
  nand_block->next_blk = NULL;
  if (!candidate_list->blocks_end) {
    bytefs_assert(!candidate_list->blocks_start);
    candidate_list->blocks_start = nand_block;
  } else {
    candidate_list->blocks_end->next_blk = nand_block;
  }
  candidate_list->blocks_end = nand_block;
  candidate_list->num_blocks++;
}
#endif

#if ALLOCATION_SECHEM_LINE
static inline struct ssd_superblock *bytefs_get_front_candidate_list(struct ssd *ssd) {
  return ssd->gc_candidate_sbs->sbs_start;
}
#else
static inline nand_block *bytefs_get_front_candidate_list(struct ssd *ssd, int ch_idx) {
  return ssd->gc_candidate_blks[ch_idx].blocks_start;
}
#endif

#if ALLOCATION_SECHEM_LINE
static void bytefs_rm_front_candidate_list(struct ssd *ssd) {
  struct gc_candidate_list *candidate_list = ssd->gc_candidate_sbs;
  bytefs_assert(candidate_list->sbs_start);
  bytefs_assert(candidate_list->num_sbs);
  if (candidate_list->sbs_start == candidate_list->sbs_end) {
    candidate_list->sbs_start = NULL;
    candidate_list->sbs_end = NULL;
  } else {
    candidate_list->sbs_start = candidate_list->sbs_start->next_sb;
  }
  candidate_list->num_sbs--;
}
#else
static void bytefs_rm_front_candidate_list(struct ssd *ssd, int ch_idx) {
  struct gc_candidate_list *candidate_list = &ssd->gc_candidate_blks[ch_idx];
  bytefs_assert(candidate_list->blocks_start);
  bytefs_assert(candidate_list->num_blocks);
  if (candidate_list->blocks_start == candidate_list->blocks_end) {
    candidate_list->blocks_start = NULL;
    candidate_list->blocks_end = NULL;
  } else {
    candidate_list->blocks_start = candidate_list->blocks_start->next_blk;
  }
  candidate_list->num_blocks--;
}
#endif

#if ALLOCATION_SECHEM_LINE
int bytefs_gc_init(struct ssd *ssd) {
  int ch_idx;
  ssd->free_sbs = (gc_free_list *) malloc(sizeof(gc_free_list));
  ssd->gc_candidate_sbs = (struct gc_candidate_list *) malloc(sizeof(gc_candidate_list));
  ssd->gc_heaps = (bytefs_heap *) malloc(sizeof(bytefs_heap));
  ssd->gc_buffer = malloc(ssd->sp.pgsz);
  memset(ssd->free_sbs, 0, sizeof(gc_free_list));
  memset(ssd->gc_candidate_sbs, 0, sizeof(gc_candidate_list));
  memset(ssd->gc_heaps, 0, sizeof(bytefs_heap));
  memset(ssd->gc_buffer, 0, ssd->sp.pgsz);
  bytefs_expect(ssd->free_sbs);
  bytefs_expect(ssd->gc_candidate_sbs);
  bytefs_expect(ssd->gc_heaps);
  bytefs_expect(ssd->gc_buffer);
  if (!ssd->free_sbs || !ssd->gc_candidate_sbs || !ssd->gc_heaps || !ssd->gc_buffer) {
    bytefs_err("GC init failed");
    return -1;
  }
  heap_create(ssd->gc_heaps, ssd->sp.sb_per_ssd);
  ssd->free_blk_lo_threshold = ssd->sp.blks_per_ch * ssd->sp.nchs * 20 / 100;   //TODO: change this to adjustable value in the future
  ssd->free_blk_hi_threshold = ssd->sp.blks_per_ch * ssd->sp.nchs * 35 / 100;

  bytefs_gc_reset(ssd);
  return 0;
}
#else
int bytefs_gc_init(struct ssd *ssd) {
  int ch_idx;
  ssd->free_blks = (gc_free_list *) malloc(ssd->sp.nchs * sizeof(gc_free_list));
  ssd->gc_candidate_blks = (struct gc_candidate_list *) malloc(ssd->sp.nchs * sizeof(gc_candidate_list));
  ssd->gc_heaps = (bytefs_heap *) malloc(ssd->sp.nchs * sizeof(bytefs_heap));
  ssd->gc_buffer = malloc(ssd->sp.pgsz);
  memset(ssd->free_blks, 0, ssd->sp.nchs * sizeof(gc_free_list));
  memset(ssd->gc_candidate_blks, 0, ssd->sp.nchs * sizeof(gc_candidate_list));
  memset(ssd->gc_heaps, 0, ssd->sp.nchs * sizeof(bytefs_heap));
  memset(ssd->gc_buffer, 0, ssd->sp.pgsz);
  bytefs_expect(ssd->free_blks);
  bytefs_expect(ssd->gc_candidate_blks);
  bytefs_expect(ssd->gc_heaps);
  bytefs_expect(ssd->gc_buffer);
  if (!ssd->free_blks || !ssd->gc_candidate_blks || !ssd->gc_heaps || !ssd->gc_buffer) {
    bytefs_err("GC init failed");
    return -1;
  }
  for (ch_idx = 0; ch_idx < ssd->sp.nchs; ch_idx++) {
    heap_create(&ssd->gc_heaps[ch_idx], ssd->sp.blks_per_ch);
  }
  ssd->free_blk_lo_threshold = ssd->sp.blks_per_ch * ssd->sp.nchs * 10 / 100;
  ssd->free_blk_hi_threshold = ssd->sp.blks_per_ch * ssd->sp.nchs * 25 / 100;

  bytefs_gc_reset(ssd);
  return 0;
}
#endif

//@TODO !! future pls move all init of backpointer in blk and sb struct to ftl.c (ssd_init)
#if ALLOCATION_SECHEM_LINE
void bytefs_gc_reset(struct ssd *ssd) {
  ssdparams *spp = &ssd->sp;
  int ch_idx, way_idx, blk_idx;
  int sb_idx;
  nand_block *blk;
  struct ssd_superblock *sb;
  ssd->total_free_sbs = 0;
  // The free list
  ssd->free_sbs->sbs_start = NULL;
  ssd->free_sbs->sbs_end = NULL;
  ssd->free_sbs->num_sbs = 0;
  for (ch_idx = 0; ch_idx < spp->nchs; ch_idx++) {
    for (way_idx = 0; way_idx < spp->luns_per_ch; way_idx++) {
      for (blk_idx = 0; blk_idx < spp->blks_per_lun; blk_idx++) {
        blk = &ssd->ch[ch_idx].lun[way_idx].blk[blk_idx];
        blk->ch_idx = ch_idx;
        blk->way_idx = way_idx;
        blk->blk_idx = blk_idx;
        blk->is_candidate = 0;
      }
    }
  }
  for (sb_idx = 0; sb_idx < spp->sb_per_ssd; sb_idx++) {
    sb = &ssd->sb[sb_idx];
    sb->blk_idx = sb_idx;
    sb->is_candidate = 0;
    bytefs_push_back_free_list(ssd, sb);
  }
  // The GC candidate list
  ssd->gc_candidate_sbs->sbs_start = NULL;
  ssd->gc_candidate_sbs->sbs_end = NULL;
  ssd->gc_candidate_sbs->num_sbs = 0;
  // The GC candidate heap
  heap_clear(ssd->gc_heaps);
}
#else
void bytefs_gc_reset(struct ssd *ssd) {
  ssdparams *spp = &ssd->sp;
  int ch_idx, way_idx, blk_idx;
  nand_block *blk;
  ssd->total_free_blks = 0;
  for (ch_idx = 0; ch_idx < spp->nchs; ch_idx++) {
    // Per channel free list
    ssd->free_blks[ch_idx].blocks_start = NULL;
    ssd->free_blks[ch_idx].blocks_end = NULL;
    ssd->free_blks[ch_idx].num_blocks = 0;
    for (way_idx = 0; way_idx < spp->luns_per_ch; way_idx++) {
      for (blk_idx = 0; blk_idx < spp->blks_per_lun; blk_idx++) {
        blk = &ssd->ch[ch_idx].lun[way_idx].blk[blk_idx];
        blk->ch_idx = ch_idx;
        blk->way_idx = way_idx;
        blk->blk_idx = blk_idx;
        blk->is_candidate = 0;
        bytefs_push_back_free_list(ssd, blk);
      }
    }
    // Per channel GC candidate list
    ssd->gc_candidate_blks[ch_idx].blocks_start = NULL;
    ssd->gc_candidate_blks[ch_idx].blocks_end = NULL;
    ssd->gc_candidate_blks[ch_idx].num_blocks = 0;
    // Per channel GC candidate heap
    heap_clear(&ssd->gc_heaps[ch_idx]);
  }
}
#endif

#if ALLOCATION_SECHEM_LINE
void bytefs_gc_free(struct ssd *ssd) {
  free(ssd->free_sbs);
  free(ssd->gc_heaps);
  free(ssd->gc_candidate_sbs);
  free(ssd->gc_buffer);
}
#else
void bytefs_gc_free(struct ssd *ssd) {
  free(ssd->free_blks);
  free(ssd->gc_heaps);
  free(ssd->gc_candidate_blks);
  free(ssd->gc_buffer);
}
#endif

static ppa bytefs_get_pba_from_sb(struct ssd_superblock *sb) {
  ppa ppa;
  ppa.realppa = 0;
  ppa.g.ch = 0;
  ppa.g.lun = 0;
  ppa.g.blk = sb->blk_idx;
  return ppa;
}

static ppa bytefs_get_pba_from_nand_blk(nand_block *blk) {
  ppa ppa;
  ppa.realppa = 0;
  ppa.g.ch = blk->ch_idx;
  ppa.g.lun = blk->way_idx;
  ppa.g.blk = blk->blk_idx;
  return ppa;
}

#if ALLOCATION_SECHEM_LINE
void bytefs_try_add_gc_candidate_ppa(struct ssd *ssd, struct ppa *ppa) {
  bytefs_try_add_gc_candidate_sb(ssd, &ssd->sb[ppa->g.blk]);
}
#else
void bytefs_try_add_gc_candidate_ppa(struct ssd *ssd, struct ppa *ppa) {
  bytefs_try_add_gc_candidate_blk(ssd, &ssd->ch[ppa->g.ch].lun[ppa->g.lun].blk[ppa->g.blk]);
}
#endif

#if ALLOCATION_SECHEM_LINE
void bytefs_try_add_gc_candidate_sb(struct ssd *ssd, struct ssd_superblock *sb) {
  if (sb->is_candidate || is_sb_free(ssd, sb)) return;
  bytefs_push_back_candidate_list(ssd, sb);
}
#else
void bytefs_try_add_gc_candidate_blk(struct ssd *ssd, nand_block *blk) {
  if (blk->is_candidate || is_block_free(ssd, blk)) return;
  bytefs_push_back_candidate_list(ssd, blk);
}
#endif

#if ALLOCATION_SECHEM_LINE
static void bytefs_add_everyting_to_candidate_list(struct ssd *ssd) {
  int i, j;
  struct ssd_superblock *sb;
  for (i = 0; i < ssd->sp.sb_per_ssd; i++) {
    sb = &ssd->sb[i];
    if (!sb->is_candidate && sb->vpc < ssd->sp.pgs_per_sb)
      bytefs_try_add_gc_candidate_sb(ssd, sb);
  }
}
#else
static void bytefs_add_everyting_to_candidate_list(struct ssd *ssd, int ch_idx) {
  int i, j;
  nand_block *blk;
  for (i = 0; i < ssd->sp.luns_per_ch; i++) {
    for (j = 0; j < ssd->sp.blks_per_lun; j++) {
      blk = &ssd->ch[ch_idx].lun[i].blk[j];
      if (!blk->is_candidate && blk->vpc < ssd->sp.pgs_per_blk)
        bytefs_try_add_gc_candidate_blk(ssd, blk);
    }
  }
}
#endif

#if ALLOCATION_SECHEM_LINE
int bytefs_should_start_gc(struct ssd *ssd) {
  return ssd->total_free_sbs <= ssd->free_blk_lo_threshold / ssd->sp.tt_luns;
}
#else
int bytefs_should_start_gc(struct ssd *ssd) {
  return ssd->total_free_blks <= ssd->free_blk_lo_threshold;
}
#endif

#if ALLOCATION_SECHEM_LINE
int bytefs_should_stop_gc(struct ssd *ssd) {
  return ssd->total_free_sbs >= ssd->free_blk_hi_threshold / ssd->sp.tt_luns;
}
#else
int bytefs_should_stop_gc(struct ssd *ssd) {
  return ssd->total_free_blks >= ssd->free_blk_hi_threshold;
}
#endif

#if ALLOCATION_SECHEM_LINE
void bytefs_generate_gc_heaps(struct ssd *ssd) {
  int ch_idx;
  struct ssd_superblock *sb;
  int max_heap_len = ssd->sp.tt_blks / ssd->sp.tt_luns;
  heap_clear(ssd->gc_heaps);
  bytefs_add_everyting_to_candidate_list(ssd);
  sb = bytefs_get_front_candidate_list(ssd);
  while (sb && ssd->gc_heaps->count < max_heap_len) {
    bytefs_rm_front_candidate_list(ssd);
    heap_insert(ssd->gc_heaps, sb->vpc, sb);
    sb = bytefs_get_front_candidate_list(ssd);
  }
}
#else
void bytefs_generate_gc_heaps(struct ssd *ssd) {
  int ch_idx;
  nand_block *blk;
  int max_heap_len = ssd->free_blk_hi_threshold - ssd->free_blk_lo_threshold;
  for (ch_idx = 0; ch_idx < ssd->sp.nchs; ch_idx++) {
    heap_clear(&ssd->gc_heaps[ch_idx]);
    bytefs_add_everyting_to_candidate_list(ssd, ch_idx);
    blk = bytefs_get_front_candidate_list(ssd, ch_idx);
    while (blk && ssd->gc_heaps[ch_idx].count < max_heap_len) {
      bytefs_rm_front_candidate_list(ssd, ch_idx);
      heap_insert(&ssd->gc_heaps[ch_idx], blk->vpc, blk);
      blk = bytefs_get_front_candidate_list(ssd, ch_idx);
    }
  }
}
#endif

#if ALLOCATION_SECHEM_LINE
struct ssd_superblock *bytefs_get_next_free_sb(struct ssd *ssd) {
  struct ssd_superblock *sb;
  sb = bytefs_get_front_free_list(ssd);
  bytefs_assert_msg(sb, "No free block left on device");

  // //Added test:
  // int nfp_count = 0;
  // for (int ch_idx = 0; ch_idx < ssd->sp.nchs; ch_idx++) {
  //     for (int lun_idx = 0; lun_idx < ssd->sp.luns_per_ch; lun_idx++) {
  //         for (int pg_idx = 0; pg_idx < ssd->sp.pgs_per_blk; pg_idx++) {
  //             nand_page *page = &ssd->ch[ch_idx].lun[lun_idx].blk[sb->blk_idx].pg[pg_idx];
  //             if (page->status != PG_FREE)
  //             {
  //                 nfp_count++;
  //             }
  //         }
  //     }
  // }
  // bytefs_assert(0 == nfp_count);

  bytefs_rm_front_free_list(ssd);
  return sb;
}
#else
nand_block *bytefs_get_next_free_blk(struct ssd *ssd, int *start_idx) {
  nand_block *blk;
  int ch_idx = *start_idx;
  const int end_idx = ch_idx;
  bytefs_assert(ch_idx >= 0 && ch_idx < ssd->sp.nchs);
  do {
    blk = bytefs_get_front_free_list(ssd, ch_idx);
    if (blk) {
      bytefs_rm_front_free_list(ssd, ch_idx);
      *start_idx = (ch_idx + 1) % ssd->sp.nchs;
      return blk;
    }
    ch_idx = (ch_idx + 1) % ssd->sp.nchs;
  } while (ch_idx != end_idx);
  bytefs_assert("No free block left on device");
  return NULL;
}
#endif

#if ALLOCATION_SECHEM_LINE
static struct ssd_superblock *bytefs_gc_find_next_gc_sb(struct ssd *ssd) {
  struct ssd_superblock *sb;
  if (!heap_is_empty(ssd->gc_heaps)) {
    sb = (struct ssd_superblock *) heap_get_min(ssd->gc_heaps);
    heap_pop_min(ssd->gc_heaps);
    return sb;
  }
  bytefs_assert("No free block left on device");
  return NULL;
}
#else
static nand_block *bytefs_gc_find_next_gc_nand_blk(struct ssd *ssd, int *start_idx) {
  nand_block *blk;
  int ch_idx = *start_idx;
  const int end_idx = ch_idx;
  bytefs_assert(ch_idx >= 0 && ch_idx < ssd->sp.nchs);
  do {
    if (!heap_is_empty(&ssd->gc_heaps[ch_idx])) {
      *start_idx = (ch_idx + 1) % ssd->sp.nchs;
      blk = (nand_block *) heap_get_min(&ssd->gc_heaps[ch_idx]);
      heap_pop_min(&ssd->gc_heaps[ch_idx]);
      return blk;
    }
    ch_idx = (ch_idx + 1) % ssd->sp.nchs;
  } while (ch_idx != end_idx);
  bytefs_err("No GC candidate available");
  return NULL;
}
#endif

#if ALLOCATION_SECHEM_LINE
static void put_sb_back_to_candidates(struct ssd *ssd) {
  struct ssd_superblock *sb;
  while (!heap_is_empty(ssd->gc_heaps)) {
    sb = (struct ssd_superblock *) heap_get_min(ssd->gc_heaps);
    heap_pop_min(ssd->gc_heaps);
    sb->is_candidate = 0;
    bytefs_push_back_candidate_list(ssd, sb);
  }
}
#else
static void put_nand_blk_back_to_candidates(struct ssd *ssd) {
  int ch_idx;
  nand_block *blk;
  for (ch_idx = 0; ch_idx < ssd->sp.nchs; ch_idx++) {
    while (!heap_is_empty(&ssd->gc_heaps[ch_idx])) {
      blk = (nand_block *) heap_get_min(&ssd->gc_heaps[ch_idx]);
      heap_pop_min(&ssd->gc_heaps[ch_idx]);
      blk->is_candidate = 0;
      bytefs_push_back_candidate_list(ssd, blk);
    }
  }
}
#endif

#if ALLOCATION_SECHEM_LINE
void bytefs_gc(struct ssd *ssd) {      //TODO: how do the GC time work?
  ssdparams *spp = &ssd->sp;
  struct ssd_superblock *gc_sb;
  nand_block *gc_blk;
  ppa free_blk_ppa, gc_blk_pba;
  uint64_t gc_page_lpn;
  uint64_t current_time;
  nand_cmd cmd;
  int ch_idx = 0, lun_idx = 0, pg_off;

  // iterate over GC sbs and gather valid pages
      
  bytefs_generate_gc_heaps(ssd);
  while (1) {
    current_time = the_clock_pt->get_time_sim(); //TODO: replace, done
    // get new superblock that is ready for GC
    gc_sb = bytefs_gc_find_next_gc_sb(ssd);
    if (gc_sb == NULL) {
      // GC ends because cannot find more candidates
      bytefs_warn("GC force end, no candidate available");
      return;
    }

    gc_blk_pba = bytefs_get_pba_from_sb(gc_sb);
    // if (gc_sb->vpc != 0) {
    for (ch_idx = 0; ch_idx < spp->nchs; ch_idx++) {
      for (lun_idx = 0; lun_idx < spp->luns_per_ch; lun_idx++) {
        gc_blk = &ssd->ch[ch_idx].lun[lun_idx].blk[gc_sb->blk_idx];
        gc_blk_pba.g.ch = ch_idx;
        gc_blk_pba.g.lun = lun_idx;
        gc_blk_pba.g.blk = gc_sb->blk_idx;
        gc_blk_pba.g.pg = 0;
        if (gc_blk->vpc != 0) {
          for (pg_off = 0; pg_off < ssd->sp.pgs_per_blk; pg_off++) {
            // get ppa of current GC block
            gc_blk_pba.g.pg = pg_off;

            ssd->maptbl_update_mutex.lock();

            ppa2pgidx(ssd, &gc_blk_pba);
            // data migration if this ppa is valid
            gc_page_lpn = get_rmap_ent(ssd, &gc_blk_pba);
            if (gc_page_lpn != INVALID_LPN) {
              // interact with nand flash
              cmd.type = GC_IO;
              cmd.cmd = NAND_READ;
              cmd.stime = current_time;
              // FIXME: CHECK THIS LATER
              ssd_advance_status(ssd, &gc_blk_pba, &cmd);
              backend_rw(ssd->bd, gc_blk_pba.realppa, ssd->gc_buffer, 0);
              // validate the copied entries
              free_blk_ppa = get_new_page(ssd);

              // nand_block *blk = get_blk(ssd, free_blk_ppa);
              // nand_page* pg = &(blk->pg[free_blk_ppa.g.pg]);
              nand_page *pg = &ssd->ch[free_blk_ppa.g.ch].lun[free_blk_ppa.g.lun].blk[free_blk_ppa.g.blk].pg[free_blk_ppa.g.pg];
              //std::cout<<"*New page: channel: "<<free_blk_ppa.g.ch<<", lun: "<<free_blk_ppa.g.lun<<", blk: "<<free_blk_ppa.g.blk<<", page: "<<free_blk_ppa.g.pg<<", status: "<<pg->status<<std::endl;
              bytefs_assert(pg->status == PG_FREE);

              set_maptbl_ent(ssd, gc_page_lpn, &free_blk_ppa);
              set_rmap_ent(ssd, gc_page_lpn, &free_blk_ppa);
              mark_page_valid(ssd, &free_blk_ppa);
              // interact with nand flash
              cmd.type = GC_IO;
              cmd.cmd = NAND_WRITE;
              cmd.stime = current_time;
              ssd_advance_status(ssd, &free_blk_ppa, &cmd);
              backend_rw(ssd->bd, free_blk_ppa.realppa, ssd->gc_buffer, 1);
              ssd_advance_write_pointer(ssd);
            }

            ssd->maptbl_update_mutex.unlock();
          }
        }
        // erase origional block
        cmd.type = GC_IO;
        cmd.cmd = NAND_ERASE;
        cmd.stime = current_time;
        ssd_advance_status(ssd, &gc_blk_pba, &cmd);
        // mark if as free and add to free list
        mark_block_free(ssd, &gc_blk_pba);
        bytefs_assert(gc_blk->vpc == 0);
        bytefs_assert(gc_blk->ipc == 0);
      }
    }
    // }
    
    mark_sb_free(ssd, gc_sb);
    bytefs_assert(gc_sb->vpc == 0);
    bytefs_assert(gc_sb->ipc == 0);
    bytefs_push_back_free_list(ssd, gc_sb);

    // if GC should end, try end it
    if (bytefs_should_stop_gc(ssd)) {
      // put back candidates for possible future GC
      put_sb_back_to_candidates(ssd);
      return;
    }
  }
}
#else
void bytefs_gc(struct ssd *ssd) {
  nand_block *gc_blk;
  ppa free_blk_ppa, gc_blk_pba;
  uint64_t gc_page_lpn;
  uint64_t current_time;
  nand_cmd cmd;
  int ch_idx = 0, gc_blk_offset = 0;

  // iterate over GC blocks and gather valid pages
      
  bytefs_generate_gc_heaps(ssd);
  while (1) {
    current_time = get_time_ns();
    // get new block that is ready for GC
    gc_blk = bytefs_gc_find_next_gc_nand_blk(ssd, &ch_idx);
    if (gc_blk == NULL) {
      // GC ends because cannot find more candidates
      bytefs_warn("GC force end, no candidate available");
      return;
    }
    gc_blk_pba = bytefs_get_pba_from_nand_blk(gc_blk);
    if (gc_blk->vpc != 0) {
      for (gc_blk_offset = 0; gc_blk_offset < ssd->sp.pgs_per_blk; gc_blk_offset++) {
        // get ppa of current GC block
        gc_blk_pba.g.pg = gc_blk_offset;
        // bytefs_log("GC blk ch: %5d way: %5d blk: %5d pg: %5d",
        //     gc_blk_ppa.g.ch, gc_blk_ppa.g.lun, gc_blk_ppa.g.blk, gc_blk_ppa.g.pg);
        ppa2pgidx(ssd, &gc_blk_pba);
        // data migration if this ppa is valid
        gc_page_lpn = get_rmap_ent(ssd, &gc_blk_pba);
        if (gc_page_lpn != INVALID_LPN) {
          // interact with nand flash
          cmd.type = GC_IO;
          cmd.cmd = NAND_READ;
          cmd.stime = current_time;
          ssd_advance_status(ssd, &gc_blk_pba, &cmd);
          backend_rw(ssd->bd, gc_blk_pba.realppa, ssd->gc_buffer, 0);
          // validate the copied entries
          // bytefs_log("Free blk ch: %5d way: %5d blk: %5d pg: %5d",
          //     free_blk_ppa.g.ch, free_blk_ppa.g.lun, free_blk_ppa.g.blk, free_blk_ppa.g.pg);
          free_blk_ppa = get_new_page(ssd);
          set_maptbl_ent(ssd, gc_page_lpn, &free_blk_ppa);
          set_rmap_ent(ssd, gc_page_lpn, &free_blk_ppa);
          mark_page_valid(ssd, &free_blk_ppa);
          // interact with nand flash
          cmd.type = GC_IO;
          cmd.cmd = NAND_WRITE;
          cmd.stime = current_time;
          ssd_advance_status(ssd, &free_blk_ppa, &cmd);
          backend_rw(ssd->bd, free_blk_ppa.realppa, ssd->gc_buffer, 1);
          ssd_advance_write_pointer(ssd);
        }
      }
      // erase origional block
      cmd.type = GC_IO;
      cmd.cmd = NAND_ERASE;
      cmd.stime = get_time_ns();
      ssd_advance_status(ssd, &gc_blk_pba, &cmd);
      // mark if as free and add to free list
      mark_block_free(ssd, &gc_blk_pba);
      bytefs_assert(gc_blk->vpc == 0);
      bytefs_assert(gc_blk->ipc == 0);
      bytefs_push_back_free_list(ssd, gc_blk);
    }

    // if GC should end, try end it
    if (bytefs_should_stop_gc(ssd)) {
      // put back candidates for possible future GC
      put_nand_blk_back_to_candidates(ssd);
      return;
    }
  }
}
#endif
