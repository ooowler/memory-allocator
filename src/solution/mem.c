#include <stdarg.h>

#define _DEFAULT_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>

#include "../include/mem_internals.h"
#include "../include/mem.h"
#include "../include/util.h"

void debug_block(struct block_header *b, const char *fmt, ...);

void debug(const char *fmt, ...);

extern inline block_size size_from_capacity(block_capacity cap);

extern inline block_capacity capacity_from_size(block_size sz);

static bool block_is_big_enough(size_t query, struct block_header *block) { return block->capacity.bytes >= query; }

static size_t pages_count(size_t mem) { return mem / getpagesize() + ((mem % getpagesize()) > 0); }

static size_t round_pages(size_t mem) { return getpagesize() * pages_count(mem); }

static void block_init(void *restrict addr, block_size block_sz, void *restrict next) {
    *((struct block_header *) addr) = (struct block_header) {
            .next = next,
            .capacity = capacity_from_size(block_sz),
            .is_free = true
    };
}

static size_t region_actual_size(size_t query) { return size_max(round_pages(query), REGION_MIN_SIZE); }

extern inline bool region_is_invalid(const struct region *r);


static void *map_pages(void const *addr, size_t length, int additional_flags) {
    return mmap((void *) addr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | additional_flags, 0, 0);
}


struct block_search_result {
    enum {
        BSR_FOUND_GOOD_BLOCK, BSR_REACHED_END_NOT_FOUND, BSR_CORRUPTED
    } type;
    struct block_header *block;
};


#define BLOCK_MIN_CAPACITY 24

/*  аллоцировать регион памяти и инициализировать его блоком */
static struct region alloc_region(void const *addr, size_t query) {
    query = size_max(query, BLOCK_MIN_CAPACITY);
    size_t region_size = region_actual_size(size_from_capacity((block_capacity) {.bytes = query}).bytes);
    void *region_addr = map_pages(addr, region_size, MAP_FIXED);

    if (region_addr == MAP_FAILED) {
        region_addr = map_pages(addr, region_size, 0); // flag 0
        if (region_addr == MAP_FAILED) return REGION_INVALID;
    }

    block_init(region_addr, (block_size) {.bytes = region_size}, NULL); // NULL like a linked list
    struct region new_region_init = (struct region) {.addr = region_addr, .size = region_size};

    return new_region_init;
}

static void *block_after(struct block_header const *block);

void *heap_init(size_t initial) {
    const struct region region = alloc_region(HEAP_START, initial);
    if (region_is_invalid(&region)) return NULL;

    return region.addr;
}


/*  --- Разделение блоков (если найденный свободный блок слишком большой )--- */

static bool block_splittable(struct block_header *restrict block, size_t query) {
    query = size_max(query, BLOCK_MIN_CAPACITY);
    return block->is_free &&
           query + offsetof(struct block_header, contents) + BLOCK_MIN_CAPACITY <= block->capacity.bytes;
}

static bool split_if_too_big(struct block_header *block, size_t query) {
    if (!block || !block_splittable(block, query)) return false;
    query = size_max(query, BLOCK_MIN_CAPACITY);

    block->capacity.bytes = query;
    struct block_header *next_block = (struct block_header *) block_after(block);

    uint8_t content = block->capacity.bytes - query;
    block_init(next_block, (block_size) {.bytes = content}, block->next);
    block->next = next_block;

    return true;
}


/*  --- Слияние соседних свободных блоков --- */

static void *block_after(struct block_header const *block) {
    return (void *) (block->contents + block->capacity.bytes);
}

static bool blocks_continuous(
        struct block_header const *fst,
        struct block_header const *snd) {
    return (void *) snd == block_after(fst);
}

static bool mergeable(struct block_header const *restrict fst, struct block_header const *restrict snd) {
    return fst->is_free && snd->is_free && blocks_continuous(fst, snd);
}

static bool try_merge_with_next(struct block_header *block) {
    if (block == NULL || block-> next == NULL || !mergeable(block, block->next)) return false;
    struct block_header *next_block = block->next;

    if (!mergeable(next_block, block)) return false;

    block->capacity.bytes += size_from_capacity(next_block->capacity).bytes;
    block->next = next_block->next;

    return true;
}


/*  --- ... ecли размера кучи хватает --- */

static struct block_search_result find_good_or_last(struct block_header *restrict block, size_t block_size) {
    if (block == NULL || block->next == NULL) return (struct block_search_result) {.type = BSR_CORRUPTED};
    struct block_search_result search_result = {.block = block};

    while (block && try_merge_with_next(block)) {
        if (block->is_free && block_is_big_enough(block_size, block)) {
            search_result.type = BSR_FOUND_GOOD_BLOCK;
            search_result.block = block;
            break;
        }

        block = block->next;
    }

    if (!block) search_result.type = BSR_REACHED_END_NOT_FOUND;

    return search_result;
}

/*  Попробовать выделить память в куче начиная с блока `block` не пытаясь расширить кучу
 Можно переиспользовать как только кучу расширили. */
static struct block_search_result try_memalloc_existing(size_t query, struct block_header *block) {
    if (!block) return (struct block_search_result) {.type = BSR_CORRUPTED, .block = block};

    query = size_max(query, BLOCK_MIN_CAPACITY);
    struct block_search_result search_result = find_good_or_last(block, query);
    if (search_result.type == BSR_FOUND_GOOD_BLOCK) {
        split_if_too_big(search_result.block, query);
        search_result.block->is_free = false;
    } else return (struct block_search_result) {.type = BSR_CORRUPTED, .block = block};

    return search_result;
}


static struct block_header *grow_heap(struct block_header *restrict last, size_t query) {
    if (!last) return NULL;

    query = size_max(query, BLOCK_MIN_CAPACITY);
    struct region new_alloc_region = alloc_region(block_after(last), query);

    if (region_is_invalid(&new_alloc_region) || !last->is_free) {
        last->next = new_alloc_region.addr;
        return new_alloc_region.addr;
    }

    last->next = new_alloc_region.addr;
    last->capacity.bytes += new_alloc_region.size;

    if (try_merge_with_next(last)) return last;
    return new_alloc_region.addr;

}

/*  Реализует основную логику malloc и возвращает заголовок выделенного блока */
static struct block_header *memalloc(size_t query, struct block_header *heap_start) {
    if (heap_start) return NULL;

    query = size_max(BLOCK_MIN_CAPACITY, query);
    struct block_search_result block_alloc_result = try_memalloc_existing(query, heap_start);
    struct block_header *result = NULL;

    if (!grow_heap(block_alloc_result.block, query)) return NULL;
    while (block_alloc_result.type == BSR_REACHED_END_NOT_FOUND) {
        if (!grow_heap(block_alloc_result.block, query)) return NULL;
        result = try_memalloc_existing(query, grow_heap(block_alloc_result.block, query)).block;
    }

    if (block_alloc_result.type == BSR_FOUND_GOOD_BLOCK) result = block_alloc_result.block;

    if (block_alloc_result.type == BSR_CORRUPTED) return NULL;


    return result;
}

void *_malloc(size_t query) {
    query = size_max(query, BLOCK_MIN_CAPACITY);
    struct block_header *const addr = memalloc(query, (struct block_header *) HEAP_START);
    if (addr) return addr->contents;
    else return NULL;
}

static struct block_header *block_get_header(void *contents) {
    return (struct block_header *) (((uint8_t *) contents) - offsetof(struct block_header, contents));
}

void _free(void *mem) {
    if (!mem) return;
    struct block_header *header = block_get_header(mem);
    header->is_free = true;
    while (try_merge_with_next(header));
}
