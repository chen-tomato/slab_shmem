
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "shmtx.h"

#ifndef _SLAB_H_INCLUDED_
#define _SLAB_H_INCLUDED_
#define SLAB_SHARM_MIN_SHIFT 3 
#define SLAB_PAGE_SIZE 4
typedef unsigned char   u_char;
typedef intptr_t        int_t;
typedef uintptr_t       uint_t;
typedef uint64_t                    atomic_uint_t;
typedef volatile atomic_uint_t  atomic_t;

typedef struct slab_page_s  slab_page_t;

struct slab_page_s {
    uintptr_t         slab;
    slab_page_t  *next;
    uintptr_t         prev;
};


typedef struct {
    uint_t        total;
    uint_t        used;

    uint_t        reqs;
    uint_t        fails;
} slab_stat_t;


typedef struct {
    shmtx_sh_t    lock;
    uint_t  pagesize;
    size_t            min_size;
    size_t            min_shift;

    slab_page_t  *pages;
    slab_page_t  *last;
    slab_page_t   free;

    slab_stat_t  *stats;
    uint_t        pfree;

    u_char           *start;
    u_char           *end;

    shmtx_t       mutex;

    u_char           *log_ctx;
    u_char            zero;

    unsigned          log_nomem:1;

    void             *data;
    void             *addr;
} slab_pool_t;


void slab_sizes_init(void);
void slab_init(slab_pool_t *pool,size_t pool_size);
void *slab_alloc(slab_pool_t *pool, size_t size);
void *slab_alloc_locked(slab_pool_t *pool, size_t size);
void *slab_calloc(slab_pool_t *pool, size_t size);
void *slab_calloc_locked(slab_pool_t *pool, size_t size);
void slab_free(slab_pool_t *pool, void *p);
void slab_free_locked(slab_pool_t *pool, void *p);


#endif /* _SLAB_H_INCLUDED_ */
