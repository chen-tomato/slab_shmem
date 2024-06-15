
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */
#include "../include/shmtx.h"

#define open_file(name, mode, create, access)                            \
    open((const char *) name, mode|create, access)


int lock_fd(int fd)
{
    struct flock  fl;

    memzero(&fl, sizeof(struct flock));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;

    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        return errno;
    }

    return 0;
}
int unlock_fd(int fd)
{
    struct flock  fl;

    memzero(&fl, sizeof(struct flock));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;

    if (fcntl(fd, F_SETLK, &fl) == -1) {
        return  errno;
    }

    return 0;
}
int trylock_fd(int fd)
{
    struct flock  fl;

    memzero(&fl, sizeof(struct flock));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;

    if (fcntl(fd, F_SETLK, &fl) == -1) {
        return errno;
    }

    return 0;
}
#if (HAVE_ATOMIC_OPS)


static void shmtx_wakeup(shmtx_t *mtx);


int_t
shmtx_create(shmtx_t *mtx, shmtx_sh_t *addr, u_char *name)
{
    mtx->lock = &addr->lock;

    if (mtx->spin == (uint_t) -1) {
        return OK;
    }

    mtx->spin = 2048;

#if (HAVE_POSIX_SEM)

    mtx->wait = &addr->wait;

    if (sem_init(&mtx->sem, 1, 0) == -1) {
        log_error(LOG_ALERT, cycle->log, errno,
                      "sem_init() failed");
    } else {
        mtx->semaphore = 1;
    }

#endif

    return OK;
}


void
shmtx_destroy(shmtx_t *mtx)
{
#if (HAVE_POSIX_SEM)

    if (mtx->semaphore) {
        if (sem_destroy(&mtx->sem) == -1) {
            log_error(LOG_ALERT, cycle->log, errno,
                          "sem_destroy() failed");
        }
    }

#endif
}


uint_t
shmtx_trylock(shmtx_t *mtx)
{
    return (*mtx->lock == 0 && atomic_cmp_set(mtx->lock, 0, pid));
}


void
shmtx_lock(shmtx_t *mtx)
{
    uint_t         i, n;

    log_debug0(LOG_DEBUG_CORE, cycle->log, 0, "shmtx lock");

    for ( ;; ) {

        if (*mtx->lock == 0 && atomic_cmp_set(mtx->lock, 0, pid)) {
            return;
        }

        if (ncpu > 1) {

            for (n = 1; n < mtx->spin; n <<= 1) {

                for (i = 0; i < n; i++) {
                    cpu_pause();
                }

                if (*mtx->lock == 0
                    && atomic_cmp_set(mtx->lock, 0, pid))
                {
                    return;
                }
            }
        }

#if (HAVE_POSIX_SEM)

        if (mtx->semaphore) {
            (void) atomic_fetch_add(mtx->wait, 1);

            if (*mtx->lock == 0 && atomic_cmp_set(mtx->lock, 0, pid)) {
                (void) atomic_fetch_add(mtx->wait, -1);
                return;
            }

            log_debug1(LOG_DEBUG_CORE, cycle->log, 0,
                           "shmtx wait %uA", *mtx->wait);

            while (sem_wait(&mtx->sem) == -1) {
                err_t  err;

                err = errno;

                if (err != EINTR) {
                    log_error(LOG_ALERT, cycle->log, err,
                                  "sem_wait() failed while waiting on shmtx");
                    break;
                }
            }

            log_debug0(LOG_DEBUG_CORE, cycle->log, 0,
                           "shmtx awoke");

            continue;
        }

#endif

        sched_yield();
    }
}


void
shmtx_unlock(shmtx_t *mtx)
{
    if (mtx->spin != (uint_t) -1) {
        log_debug0(LOG_DEBUG_CORE, cycle->log, 0, "shmtx unlock");
    }

    if (atomic_cmp_set(mtx->lock, pid, 0)) {
        shmtx_wakeup(mtx);
    }
}


uint_t
shmtx_force_unlock(shmtx_t *mtx, pid_t pid)
{
    log_debug0(LOG_DEBUG_CORE, cycle->log, 0,
                   "shmtx forced unlock");

    if (atomic_cmp_set(mtx->lock, pid, 0)) {
        shmtx_wakeup(mtx);
        return 1;
    }

    return 0;
}


static void
shmtx_wakeup(shmtx_t *mtx)
{
#if (HAVE_POSIX_SEM)
    atomic_uint_t  wait;

    if (!mtx->semaphore) {
        return;
    }

    for ( ;; ) {

        wait = *mtx->wait;

        if ((atomic_int_t) wait <= 0) {
            return;
        }

        if (atomic_cmp_set(mtx->wait, wait, wait - 1)) {
            break;
        }
    }

    log_debug1(LOG_DEBUG_CORE, cycle->log, 0,
                   "shmtx wake %uA", wait);

    if (sem_post(&mtx->sem) == -1) {
        log_error(LOG_ALERT, cycle->log, errno,
                      "sem_post() failed while wake shmtx");
    }

#endif
}


#else


int_t
shmtx_create(shmtx_t *mtx, shmtx_sh_t *addr, u_char *name)
{
    if (mtx->name) {

        if (strcmp(name, mtx->name) == 0) {
            mtx->name = name;
            return 0;
        }

        shmtx_destroy(mtx);
    }

    mtx->fd = open_file(name, O_RDWR, O_CREAT,0644);

    if (mtx->fd == -1) {
        printf(" \"%s\" failed\n", name);
        return -1;
    }

    if (unlink((const char *)name) == -1) {
        printf(" \"%s\" failed\n", name);
    }

    mtx->name = name;

    return 0;
}


void
shmtx_destroy(shmtx_t *mtx)
{
    if (close(mtx->fd) == -1) {
        printf(" \"%s\" failed\n", mtx->name);
    }
}


uint_t
shmtx_trylock(shmtx_t *mtx)
{
    int  err;

    err = trylock_fd(mtx->fd);

    if (err == 0) {
        return 1;
    }

    if (err == EAGAIN) {
        return 0;
    }

#if __osf__ /* Tru64 UNIX */

    if (err == EACCES) {
        return 0;
    }

#endif

    printf(" %s failed\n", mtx->name);

    return 0;
}


void
shmtx_lock(shmtx_t *mtx)
{
    int  err;

    err = lock_fd(mtx->fd);

    if (err == 0) {
        return;
    }

    printf(" %s failed\n", mtx->name);
}


void
shmtx_unlock(shmtx_t *mtx)
{
    int  err;

    err = unlock_fd(mtx->fd);

    if (err == 0) {
        return;
    }

    printf(" %s failed\n", mtx->name);
}


uint_t
shmtx_force_unlock(shmtx_t *mtx, pid_t pid)
{
    return 0;
}

#endif
