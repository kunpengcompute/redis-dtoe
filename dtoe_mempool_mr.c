/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
* kraio is licensed under the Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*     http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
* PURPOSE.
* See the Mulan PSL v2 for more details.
*
* Encapsulate dtoe interface
*/
#include "dtoe_mempool_mr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <malloc.h>
#include "knet_dtoe_api.h"
#include "kbdtoe_base.h"

static const size_t SLAB_OBJ_SIZES[] = {8, 64, 128, 256};
#define POOL_SIZE   (4ULL * 1024 * 1024 * 1024)
#define MIN_BLOCK_SIZE  32
#define MAX_LEVEL   32
#define SLAB_SIZE   4096
#define NUM_SLAB_CACHES (sizeof(SLAB_OBJ_SIZES) / sizeof(SLAB_OBJ_SIZES[0]))
#define MAGIC_SLAB 0x534C4142u  // "SLAB"
#define MAGIC_BUDDY 0x42444459u  // "BDDY"
#define SUCCESS 0
#define FAIL  -1
#define DTOE_PAGE_SIZE          (4096)

typedef struct BuddyBlock {
    struct BuddyBlock* next;
}BuddyBlock;

typedef struct Slab {
    struct Slab* next;
    unsigned int free_count;
    unsigned int total_count;
    unsigned char* bitmap;
    void* mem;
    size_t obj_size;
    size_t slot_size;
} Slab;

typedef struct SlabCache {
    size_t obj_size;
    Slab* slabs;
    pthread_mutex_t lock;
} SlabCache;

typedef struct MpHeader {
    uint32_t magic;
    void* owner;
} MpHeader;

static unsigned char* g_memory_pool = NULL;
static BuddyBlock* g_free_lists[MAX_LEVEL];
static SlabCache slab_caches[NUM_SLAB_CACHES];
static pthread_mutex_t buddy_lock;
static size_t buddy_total_alloc = 0;
static size_t buddy_peak_alloc = 0;
struct knet_mr *g_dmr;

static int size_to_level(size_t size)
{
    size_t block_size = MIN_BLOCK_SIZE;
    int level = 0;
    while ((block_size < size) && (level < (MAX_LEVEL -1))) {
        block_size <<= 1;
        level++;
    }
    return level;
}

static void* get_buddy(void* addr, size_t size) 
{
    uintptr_t base = (uintptr_t)g_memory_pool;
    uintptr_t offset = (uintptr_t)addr - base;
    uintptr_t buddy_offset = offset ^ size;
    return (void*)(base + buddy_offset);
}

static int buddy_init()
{
    int ret;
    size_t mempool_size = POOL_SIZE;
    g_memory_pool = aligned_alloc(DTOE_PAGE_SIZE, mempool_size);
    if (g_memory_pool == NULL) {
        KBDTOE_ERR("malloc mempool failed!\n");
        return FAIL;
    }
    memset(g_memory_pool, 0, mempool_size);
    g_dmr = knet_reg_mr(g_memory_pool,mempool_size);
    if (g_dmr == NULL) {
        free(g_memory_pool);
        return FAIL;
    }
    ret = pthread_mutex_init(&buddy_lock, NULL);
    if (ret != 0) {
        return FAIL;
    }
    for (int i = 0; i < MAX_LEVEL; i++) {
        g_free_lists[i] = NULL;
    }
    int max_level = size_to_level(POOL_SIZE);
    g_free_lists[max_level] = (BuddyBlock*)g_memory_pool;
    g_free_lists[max_level]->next = NULL;
    buddy_total_alloc = 0;
    buddy_peak_alloc = 0;
    return SUCCESS;
}

static void* buddy_alloc(size_t size)
{
    pthread_mutex_lock(&buddy_lock);
    size_t need = size + sizeof(size_t);
    int level = size_to_level(need);
    int curr = level;
    while ((curr < MAX_LEVEL) && (g_free_lists[curr] == NULL)) {
        curr++;
    }
    if (curr >= MAX_LEVEL) {
        KBDTOE_ERR("buddy alloc failed\n");
        return NULL;
    }

    while (curr > level) {
        BuddyBlock* block = g_free_lists[curr];
        g_free_lists[curr] = block->next;
        size_t block_size = (size_t)MIN_BLOCK_SIZE << curr;
        size_t half = block_size >> 1;
        BuddyBlock* b1 = block;
        BuddyBlock* b2 = (BuddyBlock*) ((char*)block + half);
        int next_level = curr - 1;
        b1->next = b2;
        b2->next = g_free_lists[next_level];
        g_free_lists[next_level] = b1;
        curr--;
    }
    BuddyBlock* block = g_free_lists[level];
    g_free_lists[level] = block->next;
    size_t block_size = (size_t)MIN_BLOCK_SIZE << level;
    *(size_t*)block = block_size;
    buddy_total_alloc += block_size;
    if (buddy_total_alloc > buddy_peak_alloc) {
        buddy_peak_alloc = buddy_total_alloc;
    }
    pthread_mutex_unlock(&buddy_lock);
    return (char*)block + sizeof(size_t);
}

static void buddy_free(void* ptr) 
{
    if (!ptr) {
        return;
    }
    pthread_mutex_lock(&buddy_lock);
    BuddyBlock* block = (BuddyBlock*) ((char*)ptr - sizeof(size_t));
    size_t size = *(size_t*)block;
    int level = size_to_level(size);
    void* addr = block;
    while (level < MAX_LEVEL) {
        void* buddy = get_buddy(addr, size);
        BuddyBlock** list = &g_free_lists[level];
        BuddyBlock* prev = NULL;
        BuddyBlock* curr = *list;
        int merged = 0;
        while (curr) {
            if ((void*)curr ==buddy) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    *list = curr->next;
                }
                if (buddy < addr) {
                    addr = buddy;
                }
                size <<= 1;
                level++;
                merged = 1;
                break;
            }
            prev = curr;
            curr = curr->next;
        }
        if (!merged) {
            break;
        }
    }
    BuddyBlock* bb = (BuddyBlock*)addr;
    bb->next = g_free_lists[level];
    g_free_lists[level] = bb;
    buddy_total_alloc -= size ;
    pthread_mutex_unlock(&buddy_lock);
}

static Slab* slab_create(SlabCache* cache)
{
    void* mem = buddy_alloc(SLAB_SIZE);
    if (!mem) {
        return NULL;
    }
    Slab* slab = (Slab*)malloc(sizeof(Slab));
    if (!slab) {
        buddy_free(mem);
        return NULL;
    }

    slab->obj_size = cache->obj_size;
    slab->slot_size = cache->obj_size + sizeof(MpHeader);
    slab->total_count = SLAB_SIZE / slab->slot_size;
    if (slab->total_count == 0) {
        buddy_free(mem);
        free(slab);
        return NULL;
    }
    slab->free_count = slab->total_count;
    slab->mem = mem;
    size_t bitmap_bytes = (slab->total_count + 7) / 8;
    slab->bitmap = (unsigned char*) calloc(bitmap_bytes, 1);
    if (!slab->bitmap) {
        buddy_free(mem);
        free(slab);
        return NULL;
    }
    slab->next = cache->slabs;
    cache->slabs = slab;
    return slab;
}

static void*  slab_alloc_from_cache(SlabCache* cache)
{
    pthread_mutex_lock(&cache->lock);
    Slab* slab = cache->slabs;
    while(slab && slab->free_count == 0) {
        slab = slab->next;
    }
    if (!slab) {
        slab = slab_create(cache);
        if (!slab) {
            pthread_mutex_unlock(&cache->lock);
            return NULL;
        }
    }

    unsigned int n = slab->total_count;
    for (unsigned int i = 0; i < n; i++) {
        unsigned int byte_index = i / 8;
        unsigned int bit_index = i % 8;
        unsigned char mask = (unsigned char)(1u << bit_index);
        if ((slab->bitmap[byte_index] & mask) == 0) {
            slab->bitmap[byte_index] |= mask;
            slab->free_count--;
            char* base = (char*) slab->mem + (i * slab->slot_size);
            MpHeader* hdr = (MpHeader*)base;
            hdr->magic = MAGIC_SLAB;
            hdr->owner = slab;
            pthread_mutex_unlock(&cache->lock);
            return (void*)(base + sizeof(MpHeader));
        }
    }

    pthread_mutex_unlock(&cache->lock);
    return NULL;
}

static void slab_free_obj (void* ptr)
{
    if (!ptr) {
        return;
    }
    MpHeader* hdr = (MpHeader*) ((char*)ptr - sizeof(MpHeader));
    if (hdr->magic != MAGIC_SLAB) {
        buddy_free(hdr);
        return;
    }
    Slab* slab = (Slab*)hdr->owner;
    if (!slab) {
        return;
    }

    SlabCache* cache = NULL;
    for (int i = 0; i < NUM_SLAB_CACHES; ++i) {
        if (slab_caches[i].obj_size == slab->obj_size) {
            cache = &slab_caches[i];
            break;
        }
    }
    if (!cache) {
        return;
    }
    pthread_mutex_lock(&cache->lock);
    uintptr_t base = (uintptr_t)slab->mem;
    uintptr_t header = (uintptr_t)hdr;
    if (header < base || (header >= (base + SLAB_SIZE))) {
        pthread_mutex_unlock(&cache->lock);
        return;
    }
    uintptr_t offset = header - base;
    unsigned int index = (unsigned int) (offset / slab->slot_size);
    if (index >= slab->total_count) {
        pthread_mutex_unlock(&cache->lock);
        return;
    }
    unsigned int byte_index = index / 8;
    unsigned int bit_index = index % 8;
    unsigned char mask = (unsigned char) (1u << bit_index);
    if (slab->bitmap[byte_index] & mask) {
        slab->bitmap[byte_index] &= (unsigned char)~mask;
        slab->free_count++;
    }
    if (slab->free_count == slab->total_count) {
        Slab** ps = &cache->slabs;
        while (*ps && *ps != slab) {
            ps = &((*ps)->next);
        }
        if (*ps == slab) {
            *ps = slab->next;
        }
        buddy_free(slab->mem);
        free(slab->bitmap);
        free(slab);
    }
    pthread_mutex_unlock(&cache->lock);
}

int  dtoe_mempool_init()
{
    if (buddy_init() != 0) {
        return FAIL;
    }
    for (int i = 0; i < NUM_SLAB_CACHES; ++i) {
        slab_caches[i].obj_size = SLAB_OBJ_SIZES[i];
        slab_caches[i].slabs = NULL;
        pthread_mutex_init(&slab_caches[i].lock, NULL);
    }
    return SUCCESS;
}

struct knet_mr *get_dtoe_mr_s()
{
    return g_dmr;
}

void* dtoe_mempool_alloc(size_t size)
{
    for (int i = 0; i < NUM_SLAB_CACHES; ++i) {
        if (size <= slab_caches[i].obj_size) {
            return slab_alloc_from_cache(&slab_caches[i]);
        }
        void* p = buddy_alloc(size + sizeof(MpHeader));
        if (!p) {
            return NULL;
        }
        MpHeader* hdr = (MpHeader*)p;
        hdr->magic = MAGIC_BUDDY;
        hdr->owner = NULL;
        return (char*)p + sizeof(MpHeader);
    }
    return NULL;
}

void  dtoe_mempool_free(void* ptr)
{
    if (!ptr) {
        return;
    }
    MpHeader* hdr = (MpHeader*)((char*)ptr - sizeof(MpHeader));
    if (hdr->magic == MAGIC_SLAB) {
        slab_free_obj(ptr);
    } else if (hdr->magic == MAGIC_BUDDY) {
        buddy_free(hdr);
    } else {
        KBDTOE_ERR("unknown mempool type free \n");
    }
}

void dtoe_mempool_stats()
{
    KBDTOE_INFO("=== Buddy stats ===\n");
    KBDTOE_INFO("Buddy total allocated:%zu bytes\n", buddy_total_alloc);
    KBDTOE_INFO("Buddy peak allocated:%zu bytes\n", buddy_peak_alloc);
    KBDTOE_INFO("=== SLAB stats ===\n");
    for (int i = 0; i < NUM_SLAB_CACHES; ++i) {
        SlabCache* sc = &slab_caches[i];
        unsigned int slabs = 0;
        unsigned int objs = 0;
        unsigned int free_objs = 0; 
        pthread_mutex_lock(&sc->lock);
        for (Slab* slab = sc->slabs; slab; slab = slab->next) {
            slabs++;
            objs += slab->total_count;
            free_objs += slab->free_count;
        }
        pthread_mutex_unlock(&sc->lock);
        if (slabs > 0) {
            KBDTOE_INFO("Cache %zuB: slabs=%u,obj=%u, free=%u\n", sc->obj_size, 
            slabs, objs, free_objs);
        }
    }
}

void dtoe_mempool_destroy()
{
    for (int i = 0; i < NUM_SLAB_CACHES; ++i) {
        SlabCache* sc = &slab_caches[i];
        pthread_mutex_lock(&sc->lock);
        Slab* slab = sc->slabs;
        while (slab) {
            Slab* next = slab->next;
            buddy_free(slab->mem);
            free(slab->bitmap);
            free(slab);
            slab = next;
        }
        sc->slabs = NULL;
        pthread_mutex_unlock (&sc->lock);
        pthread_mutex_destroy(&sc->lock);
    }
    pthread_mutex_destroy(&buddy_lock);
    free(g_memory_pool);
    g_memory_pool = NULL;
    knet_unreg_mr(g_dmr);
}

































