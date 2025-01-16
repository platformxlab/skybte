#ifndef __QUEUE_SORT_H__
#define __QUEUE_SORT_H__

#include "ftl.h"
#include "bytefs_utils.h"

struct ssd;

struct bytefs_heap {
    int count;
    int capacity;
    int64_t *key;
    void **storage;
};

void heap_create(bytefs_heap *heap, int capacity);
void heap_clear(bytefs_heap *heap);
int heap_is_empty(bytefs_heap *heap);
int heap_is_full(bytefs_heap *heap, int reserved);
void heap_insert(bytefs_heap *heap, int64_t key, void *item);
void heapify_bottom_top(bytefs_heap *heap);
void heapify_top_bottom(bytefs_heap *heap);
void *heap_get_min(bytefs_heap *heap);
void *heap_get_min_key(bytefs_heap *heap, int64_t *key_ret);
int heap_pop_min(bytefs_heap *heap);

#endif 
