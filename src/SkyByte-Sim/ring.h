/*
 * Ring Buffer
 *
 * Multiple producers and single consumer are supported with lock free.
 *
 * Copyright (c) 2018 Tencent Inc
 *
 * Authors:
 *  Xiao Guangrong <xiaoguangrong@tencent.com>
 * 
 * modified:
 *  Shaobo Li <pollux875745322@gmail.com> 2022-12 
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef __RING__
#define __RING__

#include <cstdlib>
#include <atomic>

#define CACHE_LINE  64
#define cache_aligned __attribute__((__aligned__(CACHE_LINE)))

#define RING_MULTI_PRODUCER 0x1


// /* atomic read/set with acquire/release barrier */
// #define atomic_read_acquire(ptr)                      
//     ({                                                
//     QEMU_BUILD_BUG_ON(sizeof(*ptr) > sizeof(void *)); 
//     typeof(*ptr) _val;                                
//     __atomic_load(ptr, &_val, __ATOMIC_ACQUIRE);      
//     _val;                                             
//     })

// #define atomic_set_release(ptr, i)  do {              
//     QEMU_BUILD_BUG_ON(sizeof(*ptr) > sizeof(void *)); 
//     typeof(*ptr) _val = (i);                          
//     __atomic_store(ptr, &_val, __ATOMIC_RELEASE);     
// } while(0)


// /* atomic read/set with acquire/release barrier */
// #define atomic_read_acquire(ptr)    ({            
//     typeof(*ptr) _val = atomic_read(ptr);         
//     smp_mb();                                     
//     _val;                                         
// })

// #define atomic_set_release(ptr, i)  do {          
//     smp_mb();                                     
//     atomic_set(ptr, i);                           
// } while (0)

using std::atomic_uint64_t;

struct Ring {
    unsigned int flags;
    unsigned int size;
    unsigned int mask;

    atomic_uint64_t in cache_aligned;
    atomic_uint64_t out cache_aligned;
    atomic_uint64_t *data[0] cache_aligned;
};

/*
 * allocate and initialize the ring
 *
 * @size: the number of element, it should be power of 2
 * @flags: set to RING_MULTI_PRODUCER if the ring has multiple producer,
 *         otherwise set it to 0, i,e. single producer and single consumer.
 *
 * return the ring.
 */
static inline Ring *ring_alloc(unsigned int size, unsigned int flags)
{
    Ring *ring;

    // assert(is_power_of_2(size));

    ring = (Ring *) malloc(sizeof(*ring) + size * sizeof(void *));
    ring->size = size;
    ring->mask = ring->size - 1;
    ring->flags = flags;
    return ring;
}

static inline void ring_free(Ring *ring)
{
    free(ring);
}

static inline bool __ring_is_empty(unsigned int in, unsigned int out)
{
    return in == out;
}

static inline bool ring_is_empty(Ring *ring)
{
    return ring->in == ring->out;
}

static inline unsigned int ring_len(unsigned int in, unsigned int out)
{
    return in - out;
}

static inline bool
__ring_is_full(Ring *ring, unsigned int in, unsigned int out)
{
    return ring_len(in, out) > ring->mask;
}

static inline bool ring_is_full(Ring *ring)
{
    return __ring_is_full(ring, ring->in, ring->out);
}

static inline unsigned int ring_index(Ring *ring, unsigned int pos)
{
    return pos & ring->mask;
}

static inline int ring_mp_put(Ring *ring, void *data)
{
    uint64_t index, in, in_next, out;

    do {
        in = atomic_load((atomic_uint64_t*) &ring->in);
        out = atomic_load((atomic_uint64_t*) &ring->out);

        if (__ring_is_full(ring, in, out)) {
            if (atomic_load((atomic_uint64_t*) &ring->in) == in &&
                atomic_load((atomic_uint64_t*) &ring->out) == out) {
                return -1;
            }

            /* a entry has been fetched out, retry. */
            continue;
        }

        in_next = in + 1;
    } while (atomic_compare_exchange_strong((atomic_uint64_t*) &ring->in, (uint64_t*) &in, in_next) != in);

    index = ring_index(ring, in);
    // printk(KERN_ERR "ring_mp_put: index=%d, data=%x", index, data);

    /*
     * smp_rmb() paired with the memory barrier of (A) in ring_mp_get()
     * is implied in atomic_cmpxchg() as we should read ring->out first
     * before fetching the entry, otherwise this assert will fail.
     */
    // assert(!atomic_read(&ring->data[index]));

    /*
     * smp_mb() paired with the memory barrier of (B) in ring_mp_get() is
     * implied in atomic_cmpxchg(), that is needed here as  we should read
     * ring->out before updating the entry, it is the same as we did in
     * __ring_put().
     *
     * smp_wmb() paired with the memory barrier of (C) in ring_mp_get()
     * is implied in atomic_cmpxchg(), that is needed as we should increase
     * ring->in before updating the entry.
     */
    atomic_store((atomic_uint64_t *) &ring->data[index], (int64_t) data);

    // printk(KERN_ERR "ring_mp_put: index=%d, data=%x", index, ring->data[index]);

    return 0;
}

static inline void *ring_mp_get(Ring *ring)
{
    unsigned int index, in;
    void *data;

    do {
        in = atomic_load((atomic_uint64_t *) &ring->in);

        /*
         * (C) should read ring->in first to make sure the entry pointed by this
         * index is available @ TODO
         */
        // @ TODO
        // smp_rmb();

        if (!__ring_is_empty(in, ring->out)) {
            break;
        }

        if (atomic_load((atomic_uint64_t *) &ring->in) == in) {
            return nullptr;
        }
        /* new entry has been added in, retry. */
    } while (1);

    index = ring_index(ring, ring->out);

    do {
        data = (void *) atomic_load((atomic_uint64_t *) &ring->data[index]);
        if (data) {
            break;
        }
        /* the producer is updating the entry, retry */
        // @ TODO
        // cpu_relax();
    } while (1);

    atomic_store((atomic_uint64_t *) &ring->data[index], (int64_t) NULL);

    /*
     * (B) smp_mb() is needed as we should read the entry out before
     * updating ring->out as we did in __ring_get().
     *
     * (A) smp_wmb() is needed as we should make the entry be NULL before
     * updating ring->out (which will make the entry be visible and usable).
     */

    // @TODO can we use atomic inc here?
    // atomic64_set_release((atomic_uint64_t *) &ring->out, ring->out + 1);
    atomic_store((atomic_uint64_t *) &ring->out, ring->out + 1);
    // atomic64_inc((atomic64_t*)&ring->out);

    // printk(KERN_ERR "ring_mp_put: index=%d, data=%x, out=%d", index, ring->data[index],ring->out);
    return data;
}

static inline int ring_put(Ring *ring, void *data)
{
    // if (ring->flags & RING_MULTI_PRODUCER) {
    return ring_mp_put(ring, data);
    // }
    // return __ring_put(ring, data);
}

static inline void *ring_get(Ring *ring)
{
    // if (ring->flags & RING_MULTI_PRODUCER) {
    return ring_mp_get(ring);
    // }
    // return __ring_get(ring);
}

#endif
