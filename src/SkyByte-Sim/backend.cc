/**
 * Emulate SSD cache(DRAM in SSD) and SSD flash storage.
*/
#include <stdlib.h>

#include "ftl.h"
#include "ftl_mapping.h"
#include "bytefs_utils.h"

/**
 * Memory Backend (mbe) for emulated SSD.
 * INPUT:
 * mbe - memory backend, a chunk of DRAM to emulate SSD cache and SSD disk storage.
 * nbytes - the number of bytes of such chunk
 * phy_loc - the location related to mbe where you want to put your emulated disk. (In current case, zero)
 * RETURN:
 * 0 on success
 * other on failure.
 * SIDE-EFFECT:
 * At least 4G memory is strictly allocated. 
 * Meaning to emulate this version of 2B-SSD, you need at least 6G simply for emulation usage. 
 * WARNING:
 * In GRUB, mmap=2G!(4G+kernel_memory) is needed. Any smaller DRAM configuration will lead to failure.
*/
int init_dram_backend(SsdDramBackend **mbe, size_t nbytes, uint64_t phy_loc)
{
    void* virt_loc;
    SsdDramBackend *b = *mbe = (SsdDramBackend*) malloc(sizeof(SsdDramBackend));
    if (!b) {
        bytefs_err("MTE mapping failure");
        goto failed;
    }
    memset(b, 0, sizeof(SsdDramBackend));

    b->size = nbytes;
    b->phy_loc = (void *) phy_loc;

    virt_loc = malloc(CACHE_SIZE);
    if (!virt_loc) {
        bytefs_err("MTE mapping failure");
        goto failed;
    }

    b->virt_loc = virt_loc;
    bytefs_log("vir_loc %lu", (uint64_t) virt_loc);
    return 0;

failed:
    return -1;
}


void free_dram_backend(SsdDramBackend *b)
{
    // TODO no need for free?

    // if (b->logical_space) {
    //     munlock(b->logical_space, b->size);
    //     g_free(b->logical_space);
    // }

    // no need to use this in non-kernel version
    // iounmap(b->virt_loc);

    free(b->virt_loc);
    free(b);
    return;
}

/*
* backend page read/write one page
* INPUT:
*     b        - SSD Ram Backend structure : recording the emulated DRAM physical starting address and corresponding virtual address
*                in host's DRAM (for emulation purpose).
*     ppa      - Starting address, unit is ppa. ppa is Physical Page Address : unit is 4KB DRAM chunk.
*     data     - for read : source; for write : destination
*     is_write - r/w
* RETURN:
*     always 0
* SIDE-EFFECT  - overwrite one page starting from 
*                data/ppa
*/
int backend_rw(SsdDramBackend *b, unsigned long ppa, void* data, bool is_write)
{
    return 0;
    
    // void* page_addr = (void *) ((uint64_t) (b->virt_loc) + (ppa * PG_SIZE));
    // if ((uint64_t) ppa > TOTAL_SIZE - PG_SIZE) {
    //     bytefs_err("BE RW Invalid PPN: %lX\n", ppa);
    //     return -1;
    // }

    // if (is_write) {
    //     memcpy(page_addr, data, PG_SIZE);
    // } else {
    //     memcpy(data, page_addr, PG_SIZE);
    // }
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    // return 0;
}

/**
 * return dram translated location. Cache is in 2GB~4GB in current simulated version, and always in TOTAL_SIZE ~ TOTAL_SIZE + CACHE_SIZE.
 * INPUT : 
 * b - backend SSD storage. b->virt_loc is the locaion in memory, and it's the starting address for backend.
 * off - offset to which the reqeusted position relative to cache.
 * RETURN:
 * address in host's DRAM.
 */
void *cache_mapped(SsdDramBackend *b, unsigned long off) {
    //bytefs_assert(off <= CACHE_SIZE);
    return (void *) ((uint64_t) (b->virt_loc) + off);
}


/*
* cache read/write size bytes
* INPUT:
*     b        - SSD Ram Backend structure : recording the emulated DRAM physical starting address and corresponding virtual address
*                in host's DRAM (for emulation purpose).
*     off      - offset to the starting address
*     data     - for read : source; for write : destination
*     is_write - r/w
* RETURN:
*     always 0
* SIDE-EFFECT  - overwrite one page starting from 
*                data/ppa 
*/
int cache_rw(SsdDramBackend *b, unsigned long off, void* data, bool is_write,unsigned long size)
{
    void* page_addr = cache_mapped(b, off);
    // if (off + size > CACHE_SIZE) return -1;

    if (is_write) {
        memcpy(page_addr, data, size);
    } else {
        memcpy(data, page_addr, size);
    }
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
    return 0;
}
