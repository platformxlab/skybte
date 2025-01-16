#ifndef __BYTEFS_GC_H__
#define __BYTEFS_GC_H__

#include "ftl.h"
#include "ftl_mapping.h"

// // TODO: REMOVE THESE DUPLICATED DEFINITION
#define ALLOCATION_SECHEM_LINE (1)
// struct ssd_superblock;

struct gc_free_list {
#if ALLOCATION_SECHEM_LINE
  int num_sbs;
  struct ssd_superblock *sbs_start;
  struct ssd_superblock *sbs_end;
#else
  int num_blocks;
  nand_block *blocks_start;
  nand_block *blocks_end;
#endif
};

struct gc_candidate_list {
#if ALLOCATION_SECHEM_LINE
  int num_sbs;
  struct ssd_superblock *sbs_start;
  struct ssd_superblock *sbs_end;
#else
  int num_blocks;
  nand_block *blocks_start;
  nand_block *blocks_end;
#endif
};

// typedef gc_free_list gc_candidate_list;

int bytefs_gc_init(struct ssd *ssd);
void bytefs_gc_reset(struct ssd *ssd);
void bytefs_gc_free(struct ssd *ssd);

void bytefs_try_add_gc_candidate_ppa(struct ssd *ssd, struct ppa *ppa);
#if ALLOCATION_SECHEM_LINE
void bytefs_try_add_gc_candidate_sb(struct ssd *ssd, struct ssd_superblock *sb);
#else
void bytefs_try_add_gc_candidate_blk(struct ssd *ssd, nand_block *blk);
#endif

#if ALLOCATION_SECHEM_LINE
struct ssd_superblock *bytefs_get_next_free_sb(struct ssd *ssd);
#else
struct nand_block *bytefs_get_next_free_blk(struct ssd *ssd, int *start_idx);
#endif
int bytefs_should_start_gc(struct ssd *ssd);
int bytefs_should_stop_gc(struct ssd *ssd);
void bytefs_gc(struct ssd *ssd);

#endif
