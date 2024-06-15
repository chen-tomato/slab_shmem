
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>


#ifndef _SHMTX_H_INCLUDED_
#define _SHMTX_H_INCLUDED_
#define memzero(buf, n)       (void) memset(buf, 0, n)
#define strcmp(s1, s2)  strcmp((const char *) s1, (const char *) s2)

typedef __u_char u_char;
typedef intptr_t        int_t;
typedef uintptr_t       uint_t;
typedef uint64_t                    atomic_uint_t;
typedef volatile atomic_uint_t  atomic_t;

typedef struct {
    atomic_t   lock;
#if (HAVE_POSIX_SEM)
    atomic_t   wait;
#endif
} shmtx_sh_t;

typedef struct {
#if (HAVE_ATOMIC_OPS)
    atomic_t  *lock;
#if (HAVE_POSIX_SEM)
    atomic_t  *wait;
    uint_t     semaphore;
    sem_t          sem;
#endif
#else
    int       fd;
    u_char        *name;
#endif
    uint_t     spin;
} shmtx_t;


int lock_fd(int fd);
int unlock_fd(int fd);
int trylock_fd(int fd);
int_t shmtx_create(shmtx_t *mtx, shmtx_sh_t *addr,
    u_char *name);
void shmtx_destroy(shmtx_t *mtx);
uint_t shmtx_trylock(shmtx_t *mtx);
void shmtx_lock(shmtx_t *mtx);
void shmtx_unlock(shmtx_t *mtx);
uint_t shmtx_force_unlock(shmtx_t *mtx, pid_t pid);


#endif /* _SHMTX_H_INCLUDED_ */
