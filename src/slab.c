
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include "../include/slab.h"


uint_t  pagesize_shift;


#define SLAB_PAGE_MASK   3
#define SLAB_PAGE        0
#define SLAB_BIG         1
#define SLAB_EXACT       2
#define SLAB_SMALL       3

#if (PTR_SIZE == 4)

#define SLAB_PAGE_FREE   0
#define SLAB_PAGE_BUSY   0xffffffff
#define SLAB_PAGE_START  0x80000000

#define SLAB_SHIFT_MASK  0x0000000f
#define SLAB_MAP_MASK    0xffff0000
#define SLAB_MAP_SHIFT   16

#define SLAB_BUSY        0xffffffff

#else /* (PTR_SIZE == 8) */

#define SLAB_PAGE_FREE   0
#define SLAB_PAGE_BUSY   0xffffffffffffffff
#define SLAB_PAGE_START  0x8000000000000000

#define SLAB_SHIFT_MASK  0x000000000000000f
#define SLAB_MAP_MASK    0xffffffff00000000
#define SLAB_MAP_SHIFT   32

#define SLAB_BUSY        0xffffffffffffffff

#endif


#define slab_slots(pool)                                                  \
    (slab_page_t *) ((u_char *) (pool) + sizeof(slab_pool_t))

#define slab_page_type(page)   ((page)->prev & SLAB_PAGE_MASK)

#define slab_page_prev(page)                                              \
    (slab_page_t *) ((page)->prev & ~SLAB_PAGE_MASK)

#define slab_page_addr(pool, page)                                        \
    ((((page) - (pool)->pages) << pagesize_shift)                         \
     + (uintptr_t) (pool)->start)


#if (DEBUG_MALLOC)

#define slab_junk(p, size)     memset(p, 0xA5, size)

#elif (HAVE_DEBUG_MALLOC)

#define slab_junk(p, size)                                                \
    if (debug_malloc)          memset(p, 0xA5, size)

#else

#define slab_junk(p, size)

#endif

#define align_ptr(p, a)                                                   \
    (u_char *) (((uintptr_t) (p) + ((uintptr_t) a - 1)) & ~((uintptr_t) a - 1))

static slab_page_t *slab_alloc_pages(slab_pool_t *pool,
    uint_t pages);
static void slab_free_pages(slab_pool_t *pool, slab_page_t *page,
    uint_t pages);
static void slab_error(slab_pool_t *pool, uint_t level,
    char *text);


static uint_t  slab_max_size;
static uint_t  slab_exact_size;
static uint_t  slab_exact_shift;

void
slab_init(slab_pool_t *pool,size_t pool_size)
{
    u_char           *p;
    size_t            size;
    int_t         m;
    uint_t        i, n, pages;
    slab_page_t  *slots, *page;
    pool->end = (u_char*)pool + pool_size;
    pool->min_shift = SLAB_SHARM_MIN_SHIFT;
    pool->pagesize= getpagesize();
    printf("pool->pagesize = %ld\n",pool->pagesize);
    for (uint_t num = pool->pagesize; num >>= 1; pagesize_shift++) { /* void */ }
    printf("pagesize_shift = %ld\n",pagesize_shift);
    pool->min_size = (size_t) 1 << pool->min_shift;

    slots = slab_slots(pool);
    p = (u_char *) slots;
    size = pool->end - p;
    printf("pool size = %ld\n",size);
    slab_junk(p, size);

    n = pagesize_shift - pool->min_shift;
    printf("n = %ld\n",n);
    for (i = 0; i < n; i++) {
        /* only "next" is used in list head */
        slots[i].slab = 0;
        slots[i].next = &slots[i];
        slots[i].prev = 0;
    }

    p += n * sizeof(slab_page_t);

    pool->stats = (slab_stat_t *) p;
    memzero(pool->stats, n * sizeof(slab_stat_t));
    p += n * sizeof(slab_stat_t);
    size -= n * (sizeof(slab_page_t) + sizeof(slab_stat_t));
    printf("size = %ld\n",size);
    printf("pool->pagesize = %ld\n",pool->pagesize);
    printf("sizeof(slab_page_t) = %ld\n",sizeof(slab_page_t));
    pages = (uint_t) (size / (pool->pagesize + sizeof(slab_page_t)));

    printf("pages = %ld\n",pages);
    pool->pages = (slab_page_t *) p;
    printf("pool->pages = %p\n",pool->pages);
    printf("pages * sizeof(slab_page_t) = %ld\n",pages * sizeof(slab_page_t));
    memset(pool->pages, 0, pages * sizeof(slab_page_t));
    page = pool->pages;
    
    /* only "next" is used in list head */
    pool->free.slab = 0;
    pool->free.next = page;
    pool->free.prev = 0;

    page->slab = pages;
    page->next = &pool->free;
    page->prev = (uintptr_t) &pool->free;
    printf("page->prev = %ld\n",page->prev);
    pool->start = align_ptr(p + pages * sizeof(slab_page_t),
                                pool->pagesize);
    printf("pool->start = %p\n",pool->start);
    m = pages - (pool->end - pool->start) / pool->pagesize;
    if (m > 0) {
        pages -= m;
        page->slab = pages;
    }

    pool->last = pool->pages + pages;
    pool->pfree = pages;

    pool->log_nomem = 1;
    pool->log_ctx = &pool->zero;
    pool->zero = '\0';
}


void *
slab_alloc(slab_pool_t *pool, size_t size)
{
    void  *p;

    shmtx_lock(&pool->mutex);

    p = slab_alloc_locked(pool, size);

    shmtx_unlock(&pool->mutex);

    return p;
}


void *
slab_alloc_locked(slab_pool_t *pool, size_t size)
{
    size_t            s;
    uintptr_t         p, m, mask, *bitmap;
    uint_t        i, n, slot, shift, map;
    slab_page_t  *page, *prev, *slots;

    if (size > slab_max_size) {

        printf("slab alloc: %lu\n", size);
        printf("size >> pagesize_shift: %lu\n", size >> pagesize_shift);
        printf("size %% pool->pagesize: %lu\n", size % pool->pagesize);
        printf("pool->pagesize: %lu\n", pool->pagesize);
        page = slab_alloc_pages(pool, (size >> pagesize_shift)
                                          + ((size % pool->pagesize) ? 1 : 0));
        if (page) {
            p = slab_page_addr(pool, page);

        } else {
            p = 0;
        }

        goto done;
    }

    if (size > pool->min_size) {
        shift = 1;
        for (s = size - 1; s >>= 1; shift++) { /* void */ }
        slot = shift - pool->min_shift;

    } else {
        shift = pool->min_shift;
        slot = 0;
    }

    pool->stats[slot].reqs++;

    printf("slab alloc: %lu slot: %lu\n", size, slot);

    slots = slab_slots(pool);
    page = slots[slot].next;

    if (page->next != page) {

        if (shift < slab_exact_shift) {

            bitmap = (uintptr_t *) slab_page_addr(pool, page);

            map = (pool->pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (n = 0; n < map; n++) {

                if (bitmap[n] != SLAB_BUSY) {

                    for (m = 1, i = 0; m; m <<= 1, i++) {
                        if (bitmap[n] & m) {
                            continue;
                        }

                        bitmap[n] |= m;

                        i = (n * 8 * sizeof(uintptr_t) + i) << shift;

                        p = (uintptr_t) bitmap + i;

                        pool->stats[slot].used++;

                        if (bitmap[n] == SLAB_BUSY) {
                            for (n = n + 1; n < map; n++) {
                                if (bitmap[n] != SLAB_BUSY) {
                                    goto done;
                                }
                            }

                            prev = slab_page_prev(page);
                            prev->next = page->next;
                            page->next->prev = page->prev;

                            page->next = NULL;
                            page->prev = SLAB_SMALL;
                        }

                        goto done;
                    }
                }
            }

        } else if (shift == slab_exact_shift) {

            for (m = 1, i = 0; m; m <<= 1, i++) {
                if (page->slab & m) {
                    continue;
                }

                page->slab |= m;

                if (page->slab == SLAB_BUSY) {
                    prev = slab_page_prev(page);
                    prev->next = page->next;
                    page->next->prev = page->prev;

                    page->next = NULL;
                    page->prev = SLAB_EXACT;
                }

                p = slab_page_addr(pool, page) + (i << shift);

                pool->stats[slot].used++;

                goto done;
            }

        } else { /* shift > slab_exact_shift */

            mask = ((uintptr_t) 1 << (pool->pagesize >> shift)) - 1;
            mask <<= SLAB_MAP_SHIFT;

            for (m = (uintptr_t) 1 << SLAB_MAP_SHIFT, i = 0;
                 m & mask;
                 m <<= 1, i++)
            {
                if (page->slab & m) {
                    continue;
                }

                page->slab |= m;

                if ((page->slab & SLAB_MAP_MASK) == mask) {
                    prev = slab_page_prev(page);
                    prev->next = page->next;
                    page->next->prev = page->prev;

                    page->next = NULL;
                    page->prev = SLAB_BIG;
                }

                p = slab_page_addr(pool, page) + (i << shift);

                pool->stats[slot].used++;

                goto done;
            }
        }

        printf("slab_alloc(): page is busy\n");
    }

    page = slab_alloc_pages(pool, 1);

    if (page) {
        if (shift < slab_exact_shift) {
            bitmap = (uintptr_t *) slab_page_addr(pool, page);

            n = (pool->pagesize >> shift) / ((1 << shift) * 8);

            if (n == 0) {
                n = 1;
            }

            /* "n" elements for bitmap, plus one requested */

            for (i = 0; i < (n + 1) / (8 * sizeof(uintptr_t)); i++) {
                bitmap[i] = SLAB_BUSY;
            }

            m = ((uintptr_t) 1 << ((n + 1) % (8 * sizeof(uintptr_t)))) - 1;
            bitmap[i] = m;

            map = (pool->pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (i = i + 1; i < map; i++) {
                bitmap[i] = 0;
            }

            page->slab = shift;
            page->next = &slots[slot];
            page->prev = (uintptr_t) &slots[slot] | SLAB_SMALL;

            slots[slot].next = page;

            pool->stats[slot].total += (pool->pagesize >> shift) - n;

            p = slab_page_addr(pool, page) + (n << shift);

            pool->stats[slot].used++;

            goto done;

        } else if (shift == slab_exact_shift) {

            page->slab = 1;
            page->next = &slots[slot];
            page->prev = (uintptr_t) &slots[slot] | SLAB_EXACT;

            slots[slot].next = page;

            pool->stats[slot].total += 8 * sizeof(uintptr_t);

            p = slab_page_addr(pool, page);

            pool->stats[slot].used++;

            goto done;

        } else { /* shift > slab_exact_shift */

            page->slab = ((uintptr_t) 1 << SLAB_MAP_SHIFT) | shift;
            page->next = &slots[slot];
            page->prev = (uintptr_t) &slots[slot] | SLAB_BIG;

            slots[slot].next = page;

            pool->stats[slot].total += pool->pagesize >> shift;

            p = slab_page_addr(pool, page);

            pool->stats[slot].used++;

            goto done;
        }
    }

    p = 0;

    pool->stats[slot].fails++;

done:
    return (void *) p;
}


void *
slab_calloc(slab_pool_t *pool, size_t size)
{
    void  *p;

    shmtx_lock(&pool->mutex);

    p = slab_calloc_locked(pool, size);

    shmtx_unlock(&pool->mutex);

    return p;
}


void *
slab_calloc_locked(slab_pool_t *pool, size_t size)
{
    void  *p;

    p = slab_alloc_locked(pool, size);
    if (p) {
        memzero(p, size);
    }

    return p;
}


void
slab_free(slab_pool_t *pool, void *p)
{
    shmtx_lock(&pool->mutex);

    slab_free_locked(pool, p);

    shmtx_unlock(&pool->mutex);
}


void
slab_free_locked(slab_pool_t *pool, void *p)
{
    size_t            size;
    uintptr_t         slab, m, *bitmap;
    uint_t        i, n, type, slot, shift, map;
    slab_page_t  *slots, *page;

    if ((u_char *) p < pool->start || (u_char *) p > pool->end) {
        printf("slab_free(): outside of pool\n");
        goto fail;
    }

    n = ((u_char *) p - pool->start) >> pagesize_shift;
    page = &pool->pages[n];
    slab = page->slab;
    type = slab_page_type(page);

    switch (type) {

    case SLAB_SMALL:

        shift = slab & SLAB_SHIFT_MASK;
        size = (size_t) 1 << shift;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        n = ((uintptr_t) p & (pool->pagesize - 1)) >> shift;
        m = (uintptr_t) 1 << (n % (8 * sizeof(uintptr_t)));
        n /= 8 * sizeof(uintptr_t);
        bitmap = (uintptr_t *)
                             ((uintptr_t) p & ~((uintptr_t) pool->pagesize - 1));

        if (bitmap[n] & m) {
            slot = shift - pool->min_shift;

            if (page->next == NULL) {
                slots = slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | SLAB_SMALL;
                page->next->prev = (uintptr_t) page | SLAB_SMALL;
            }

            bitmap[n] &= ~m;

            n = (pool->pagesize >> shift) / ((1 << shift) * 8);

            if (n == 0) {
                n = 1;
            }

            i = n / (8 * sizeof(uintptr_t));
            m = ((uintptr_t) 1 << (n % (8 * sizeof(uintptr_t)))) - 1;

            if (bitmap[i] & ~m) {
                goto done;
            }

            map = (pool->pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (i = i + 1; i < map; i++) {
                if (bitmap[i]) {
                    goto done;
                }
            }

            slab_free_pages(pool, page, 1);

            pool->stats[slot].total -= (pool->pagesize >> shift) - n;

            goto done;
        }

        goto chunk_already_free;

    case SLAB_EXACT:

        m = (uintptr_t) 1 <<
                (((uintptr_t) p & (pool->pagesize - 1)) >> slab_exact_shift);
        size = slab_exact_size;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        if (slab & m) {
            slot = slab_exact_shift - pool->min_shift;

            if (slab == SLAB_BUSY) {
                slots = slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | SLAB_EXACT;
                page->next->prev = (uintptr_t) page | SLAB_EXACT;
            }

            page->slab &= ~m;

            if (page->slab) {
                goto done;
            }

            slab_free_pages(pool, page, 1);

            pool->stats[slot].total -= 8 * sizeof(uintptr_t);

            goto done;
        }

        goto chunk_already_free;

    case SLAB_BIG:

        shift = slab & SLAB_SHIFT_MASK;
        size = (size_t) 1 << shift;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        m = (uintptr_t) 1 << ((((uintptr_t) p & (pool->pagesize - 1)) >> shift)
                              + SLAB_MAP_SHIFT);

        if (slab & m) {
            slot = shift - pool->min_shift;

            if (page->next == NULL) {
                slots = slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | SLAB_BIG;
                page->next->prev = (uintptr_t) page | SLAB_BIG;
            }

            page->slab &= ~m;

            if (page->slab & SLAB_MAP_MASK) {
                goto done;
            }

            slab_free_pages(pool, page, 1);

            pool->stats[slot].total -= pool->pagesize >> shift;

            goto done;
        }

        goto chunk_already_free;

    case SLAB_PAGE:

        if ((uintptr_t) p & (pool->pagesize - 1)) {
            goto wrong_chunk;
        }

        if (!(slab & SLAB_PAGE_START)) {
            printf("slab_free(): page is already free\n");
            goto fail;
        }

        if (slab == SLAB_PAGE_BUSY) {
            printf("slab_free(): pointer to wrong page\n");
            goto fail;
        }

        size = slab & ~SLAB_PAGE_START;

        slab_free_pages(pool, page, size);

        slab_junk(p, size << pagesize_shift);

        return;
    }

    /* not reached */

    return;

done:

    pool->stats[slot].used--;

    slab_junk(p, size);

    return;

wrong_chunk:

    printf("slab_free(): pointer to wrong chunk\n");

    goto fail;

chunk_already_free:

    printf("slab_free(): chunk is already free\n");

fail:

    return;
}


static slab_page_t *
slab_alloc_pages(slab_pool_t *pool, uint_t pages)
{
    slab_page_t  *page, *p;
    printf("slab_alloc_pages function\n");
    for (page = pool->free.next; page != &pool->free; page = page->next) {
        printf("slab_alloc_pages page->slab = %ld\n",page->slab);
        printf("slab_alloc_pages pages = %ld\n",pages);
        if (page->slab >= pages) {

            if (page->slab > pages) {
                page[page->slab - 1].prev = (uintptr_t) &page[pages];

                page[pages].slab = page->slab - pages;
                page[pages].next = page->next;
                page[pages].prev = page->prev;

                p = (slab_page_t *) page->prev;
                p->next = &page[pages];
                page->next->prev = (uintptr_t) &page[pages];

            } else {
                p = (slab_page_t *) page->prev;
                p->next = page->next;
                page->next->prev = page->prev;
            }

            page->slab = pages | SLAB_PAGE_START;
            page->next = NULL;
            page->prev = SLAB_PAGE;

            pool->pfree -= pages;

            if (--pages == 0) {
                return page;
            }

            for (p = page + 1; pages; pages--) {
                p->slab = SLAB_PAGE_BUSY;
                p->next = NULL;
                p->prev = SLAB_PAGE;
                p++;
            }

            return page;
        }
    }

    if (pool->log_nomem) {
        printf("slab_alloc() failed: no memory\n");
    }

    return NULL;
}


static void
slab_free_pages(slab_pool_t *pool, slab_page_t *page,
    uint_t pages)
{
    slab_page_t  *prev, *join;

    pool->pfree += pages;

    page->slab = pages--;

    if (pages) {
        memzero(&page[1], pages * sizeof(slab_page_t));
    }

    if (page->next) {
        prev = slab_page_prev(page);
        prev->next = page->next;
        page->next->prev = page->prev;
    }

    join = page + page->slab;

    if (join < pool->last) {

        if (slab_page_type(join) == SLAB_PAGE) {

            if (join->next != NULL) {
                pages += join->slab;
                page->slab += join->slab;

                prev = slab_page_prev(join);
                prev->next = join->next;
                join->next->prev = join->prev;

                join->slab = SLAB_PAGE_FREE;
                join->next = NULL;
                join->prev = SLAB_PAGE;
            }
        }
    }

    if (page > pool->pages) {
        join = page - 1;

        if (slab_page_type(join) == SLAB_PAGE) {

            if (join->slab == SLAB_PAGE_FREE) {
                join = slab_page_prev(join);
            }

            if (join->next != NULL) {
                pages += join->slab;
                join->slab += page->slab;

                prev = slab_page_prev(join);
                prev->next = join->next;
                join->next->prev = join->prev;

                page->slab = SLAB_PAGE_FREE;
                page->next = NULL;
                page->prev = SLAB_PAGE;

                page = join;
            }
        }
    }

    if (pages) {
        page[pages].prev = (uintptr_t) page;
    }

    page->prev = (uintptr_t) &pool->free;
    page->next = pool->free.next;

    page->next->prev = (uintptr_t) page;

    pool->free.next = page;
}


static void
slab_error(slab_pool_t *pool, uint_t level, char *text)
{
    printf("%s%s\n", text, pool->log_ctx);
}
