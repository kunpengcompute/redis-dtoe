/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
* redis dtoe is licensed under the Mulan PSL v2.
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
#include "kbdtoe_mempool_mr.h"
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include "kbdtoe_base.h"
#include "securec.h"

static const size_t SLAB_OBJ_SIZES[] = {8, 16, 128, 512, 1024};
#define POOL_SIZE (1ULL * 1024 * 1024 * 1024)
#define MIN_BLOCK_SIZE  32
#define MAX_LEVEL   32
#define SLAB_SIZE   16384
#define NUM_SLAB_CACHES (sizeof(SLAB_OBJ_SIZES) / sizeof(SLAB_OBJ_SIZES[0]))
#define MAGIC_SLAB 0x534C4142u  // "SLAB"
#define MAGIC_BUDDY 0x42444459u  // "BDDY"
#define SUCCESS 0
#define FAIL  -1
#define DTOE_PAGE_SIZE          (4096)

typedef struct BuddyBlock {
    struct BuddyBlock* next;
} BuddyBlock;

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
static SlabCache g_slab_caches[NUM_SLAB_CACHES];
static pthread_mutex_t g_buddy_lock;
static size_t g_buddy_total_alloc = 0;
static size_t g_buddy_peak_alloc = 0;
static flexda_dtoe_mr_s *g_dmr;

static int size_to_level(size_t size)
{
    int level = 0;
    while ((size > (MIN_BLOCK_SIZE << level)) && (level < (MAX_LEVEL - 1))) {
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
    g_memory_pool = mmap(NULL, POOL_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_memory_pool == MAP_FAILED) {
        KBDTOE_ERR("mmap failed!\n");
        g_memory_pool = NULL;
        return FAIL;
    }
    if (madvise(g_memory_pool, POOL_SIZE, MADV_DONTFORK) !=0) {
        KBDTOE_ERR("madvise failed!\n");
        munmap(g_memory_pool, POOL_SIZE);
        g_memory_pool = NULL;
        return FAIL;
    }

    (void)memset_s(g_memory_pool, POOL_SIZE, 0, POOL_SIZE);
    g_dmr = (flexda_dtoe_mr_s*)malloc(sizeof(flexda_dtoe_mr_s));
    if (g_dmr == NULL) {
        munmap(g_memory_pool, POOL_SIZE);
        g_memory_pool = NULL;
        return FAIL;
    }
    int reg_ret = flexda_dtoe_reg_mr(get_dtoe_dev_sn(), g_memory_pool, POOL_SIZE, g_dmr);
    if (reg_ret != 0) {
        munmap(g_memory_pool, POOL_SIZE);
        g_memory_pool = NULL;
        free(g_dmr);
        g_dmr = NULL;
        return FAIL;
    }
    ret = pthread_mutex_init(&g_buddy_lock, NULL);
    if (ret != 0) {
        flexda_dtoe_unreg_mr(get_dtoe_dev_sn(), g_dmr);
        munmap(g_memory_pool, POOL_SIZE);
        g_memory_pool = NULL;
        free(g_dmr);
        g_dmr = NULL;
        return FAIL;
    }
    for (int i = 0; i < MAX_LEVEL; i++) {
        g_free_lists[i] = NULL;
    }
    int max_level = size_to_level(POOL_SIZE);
    g_free_lists[max_level] = (BuddyBlock*)g_memory_pool;
    g_free_lists[max_level]->next = NULL;
    g_buddy_total_alloc = 0;
    g_buddy_peak_alloc = 0;
    return SUCCESS;
}

static void* buddy_alloc(size_t size)
{
    pthread_mutex_lock(&g_buddy_lock);
    size_t need = size + sizeof(size_t);
    int level = size_to_level(need);
    int curr = level;
    while ((curr < MAX_LEVEL) && (g_free_lists[curr] == NULL)) {
        curr++;
    }
    if (curr >= MAX_LEVEL) {
        pthread_mutex_unlock(&g_buddy_lock);
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
    g_buddy_total_alloc += block_size;
    if (g_buddy_total_alloc > g_buddy_peak_alloc) {
        g_buddy_peak_alloc = g_buddy_total_alloc;
    }
    pthread_mutex_unlock(&g_buddy_lock);
    return (char*)block + sizeof(size_t);
}

static void buddy_free(void* ptr) 
{
    if (!ptr) {
        return;
    }
    pthread_mutex_lock(&g_buddy_lock);
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
            if ((void*)curr == buddy) {
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
    g_buddy_total_alloc -= size ;
    pthread_mutex_unlock(&g_buddy_lock);
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
    size_t bitmap_bytes = (slab->total_count + 7) / 8; // 加7除8是向上取整的经典写法
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

static void* slab_alloc_from_cache(SlabCache* cache)
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

    for (unsigned int i = 0; i < slab->total_count; i++) {
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

static void slab_free_obj(void* ptr)
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
        if (g_slab_caches[i].obj_size == slab->obj_size) {
            cache = &g_slab_caches[i];
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

int  kbdtoe_mempool_init()
{
    if (buddy_init() != 0) {
        return FAIL;
    }
    for (int i = 0; i < NUM_SLAB_CACHES; ++i) {
        g_slab_caches[i].obj_size = SLAB_OBJ_SIZES[i];
        g_slab_caches[i].slabs = NULL;
        pthread_mutex_init(&g_slab_caches[i].lock, NULL);
    }
    return SUCCESS;
}

flexda_dtoe_mr_s *get_dtoe_mr_s()
{
    return g_dmr;
}

void* kbdtoe_mempool_alloc(size_t size)
{
    for (int i = 0; i < NUM_SLAB_CACHES; ++i) {
        if (size <= g_slab_caches[i].obj_size) {
            void *p = slab_alloc_from_cache(&g_slab_caches[i]);
            if (p) {
                return p;
            }
            break;
        }
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

void  kbdtoe_mempool_free(int sockfd, uint64_t w_id)
{
    void *ptr = (void*)w_id;
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

void kbdtoe_mempool_stats()
{
    KBDTOE_INFO("=== Buddy stats ===\n");
    KBDTOE_INFO("Buddy total allocated:%zu bytes\n", g_buddy_total_alloc);
    KBDTOE_INFO("Buddy peak allocated:%zu bytes\n", g_buddy_peak_alloc);
    KBDTOE_INFO("=== SLAB stats ===\n");
    for (int i = 0; i < NUM_SLAB_CACHES; ++i) {
        SlabCache* sc = &g_slab_caches[i];
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

void kbdtoe_mempool_destroy()
{
    if (g_memory_pool == NULL) {
        return;
    }
    for (int i = 0; i < NUM_SLAB_CACHES; ++i) {
        SlabCache* sc = &g_slab_caches[i];
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
    pthread_mutex_destroy(&g_buddy_lock);
    munmap(g_memory_pool, POOL_SIZE);
    g_memory_pool = NULL;
    if (g_dmr != NULL) {
        flexda_dtoe_unreg_mr(get_dtoe_dev_sn(), g_dmr);
        free(g_dmr);
        g_dmr = NULL;
    }
}

