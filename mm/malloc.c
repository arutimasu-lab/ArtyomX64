// malloc.c — malloc/realloc/free + статистика (print_kmalloc_stats)
#include "malloc.h"
#include <stdint.h>
#include <stddef.h>
//#include "../syscall/syscall.h"

/* Конфигурация */
#define ALIGN 8
#define MAGIC 0xB16B00B5U

/* Заголовок блока (payload идёт сразу после заголовка) */
typedef struct block_header
{
    uint32_t magic;
    size_t size; /* payload size в байтах */
    int free;    /* 1 если свободен, 0 если занят */
    struct block_header *prev;
    struct block_header *next;
} block_header_t;

#define MIN_SPLIT_SIZE (sizeof(block_header_t) + ALIGN)

static block_header_t *heap_head = NULL;
static block_header_t *heap_tail = NULL;
static unsigned char *managed_heap_start = NULL;
static unsigned char *managed_heap_end = NULL;
static int heap_initialized = 0;

/* Внешние функции (реализованы в других файлах вашего ядра) */
extern void *memcpy(void *dst, const void *src, size_t n);

static inline size_t align_up(size_t n)
{
    if (n > SIZE_MAX - (ALIGN - 1))
        return SIZE_MAX;
    return (n + (ALIGN - 1)) & ~(ALIGN - 1);
}

static inline void *header_to_payload(block_header_t *h)
{
    return (void *)((char *)h + sizeof(block_header_t));
}

static inline block_header_t *payload_to_header(void *p)
{
    return (block_header_t *)((char *)p - sizeof(block_header_t));
}

void malloc_init(void *heap_start, size_t heap_size)
{
    uintptr_t raw_start;
    uintptr_t aligned_start;
    size_t adjustment;

    heap_head = NULL;
    heap_tail = NULL;
    managed_heap_start = NULL;
    managed_heap_end = NULL;
    heap_initialized = 0;

    if (!heap_start || heap_size < sizeof(block_header_t) + ALIGN)
        return;

    raw_start = (uintptr_t)heap_start;
    aligned_start = (raw_start + (ALIGN - 1u)) & ~(uintptr_t)(ALIGN - 1u);
    adjustment = (size_t)(aligned_start - raw_start);
    if (adjustment > heap_size || heap_size - adjustment < sizeof(block_header_t) + ALIGN)
        return;

    heap_size -= adjustment;
    heap_head = (block_header_t *)aligned_start;
    heap_head->magic = MAGIC;
    heap_head->size = heap_size - sizeof(block_header_t);
    heap_head->free = 1;
    heap_head->prev = NULL;
    heap_head->next = NULL;
    heap_tail = heap_head;
    managed_heap_start = (unsigned char *)aligned_start;
    managed_heap_end = managed_heap_start + heap_size;
    heap_initialized = 1;
}

/* find first-fit */
static block_header_t *find_fit(size_t size)
{
    block_header_t *cur = heap_head;
    while (cur)
    {
        if (cur->free && cur->size >= size)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

/* split блока */
static void split_block(block_header_t *h, size_t req_size)
{
    if (!h)
        return;
    if (h->size < req_size + MIN_SPLIT_SIZE)
        return;

    char *new_hdr_addr = (char *)header_to_payload(h) + req_size;
    block_header_t *newh = (block_header_t *)new_hdr_addr;
    newh->magic = MAGIC;
    newh->free = 1;
    newh->size = h->size - req_size - sizeof(block_header_t);
    newh->prev = h;
    newh->next = h->next;
    if (newh->next)
        newh->next->prev = newh;
    h->next = newh;
    h->size = req_size;
    if (heap_tail == h)
        heap_tail = newh;
}

static block_header_t *coalesce(block_header_t *h)
{
    block_header_t *next;

    if (!h)
        return NULL;

    if (h->prev && h->prev->free) {
        h = h->prev;
        next = h->next;
        h->size += sizeof(block_header_t) + next->size;
        h->next = next->next;
        if (h->next)
            h->next->prev = h;
        else
            heap_tail = h;
    }

    while (h->next && h->next->free) {
        next = h->next;
        h->size += sizeof(block_header_t) + next->size;
        h->next = next->next;
        if (h->next)
            h->next->prev = h;
        else
            heap_tail = h;
    }

    return h;
}

static int heap_contains_payload(const void *ptr, block_header_t **out_header)
{
    block_header_t *cur;

    if (!heap_initialized || !ptr || !managed_heap_start || !managed_heap_end)
        return 0;
    if ((const unsigned char *)ptr < managed_heap_start + sizeof(block_header_t) ||
        (const unsigned char *)ptr >= managed_heap_end)
        return 0;

    cur = heap_head;
    while (cur) {
        if (cur->magic != MAGIC)
            return 0;
        if (header_to_payload(cur) == ptr) {
            if (out_header)
                *out_header = cur;
            return 1;
        }
        cur = cur->next;
    }

    return 0;
}

void *malloc(size_t size)
{
    block_header_t *fit;

    if (!heap_initialized || size == 0)
        return NULL;
    size = align_up(size);
    if (size == SIZE_MAX)
        return NULL;

    fit = find_fit(size);
    if (!fit)
        return NULL;
    split_block(fit, size);
    fit->free = 0;
    return header_to_payload(fit);
}

void free(void *ptr)
{
    block_header_t *h;

    if (!heap_contains_payload(ptr, &h) || h->free)
        return;

    h->free = 1;
    coalesce(h);
}

void *realloc(void *ptr, size_t new_size)
{
    block_header_t *h;
    void *newp;
    size_t copy;

    if (!ptr)
        return malloc(new_size);
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    if (!heap_contains_payload(ptr, &h) || h->free)
        return NULL;

    new_size = align_up(new_size);
    if (new_size == SIZE_MAX)
        return NULL;
    if (new_size <= h->size) {
        size_t old_size = h->size;
        split_block(h, new_size);
        if (h->size != old_size && h->next)
            coalesce(h->next);
        return ptr;
    }

    if (h->next && h->next->free) {
        size_t available = h->size;
        block_header_t *cur = h->next;
        block_header_t *last_free = h;

        while (cur && cur->free && available < new_size) {
            available += sizeof(block_header_t) + cur->size;
            last_free = cur;
            cur = cur->next;
        }

        if (available >= new_size) {
            h->size = available;
            h->next = cur;
            if (cur)
                cur->prev = h;
            else
                heap_tail = h;
            split_block(h, new_size);
            if (h->next)
                coalesce(h->next);
            return ptr;
        }
    }

    newp = malloc(new_size);
    if (!newp)
        return NULL;
    copy = h->size < new_size ? h->size : new_size;
    memcpy(newp, ptr, copy);
    free(ptr);
    return newp;
}

/* ---- stats for kernel malloc ---- */

/* Обойти список блоков и собрать статистику.
   heap_head и block_header_t — доступны в этом файле */
void get_kmalloc_stats(kmalloc_stats_t *st)
{
    if (!st)
        return;
    st->total_managed = 0;
    st->used_payload = 0;
    st->free_payload = 0;
    st->largest_free = 0;
    st->num_blocks = st->num_used = st->num_free = 0;

    block_header_t *cur = heap_head;
    while (cur)
    {
        size_t block_size;
        if (cur->magic != MAGIC)
            break;
        st->num_blocks++;
        block_size = sizeof(block_header_t) + cur->size;
        if (st->total_managed > SIZE_MAX - block_size)
            st->total_managed = SIZE_MAX;
        else
            st->total_managed += block_size;
        if (cur->free)
        {
            st->num_free++;
            st->free_payload += cur->size;
            if (cur->size > st->largest_free)
                st->largest_free = cur->size;
        }
        else
        {
            st->num_used++;
            st->used_payload += cur->size;
        }
        cur = cur->next;
    }
}