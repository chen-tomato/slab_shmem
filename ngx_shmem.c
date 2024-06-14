

#include "ngx_shmem.h"

typedef __u_char u_char;
#if (0)

ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    shm->addr = (u_char *) mmap(NULL, shm->size,
                                PROT_READ|PROT_WRITE,
                                MAP_ANON|MAP_SHARED, -1, 0);

    if (shm->addr == MAP_FAILED) {
        printf(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "mmap(MAP_ANON|MAP_SHARED, %luz) failed", shm->size);
        return NGX_ERROR;
    }

    return NGX_OK;
}


void
ngx_shm_free(ngx_shm_t *shm)
{
    if (munmap((void *) shm->addr, shm->size) == -1) {
        printf("munmap(%p, %luz) failed", shm->addr, shm->size);
    }
}

#elif (0)

ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    int  fd;

    fd = open("/dev/zero", O_RDWR);

    if (fd == -1) {
        printf("open(\"/dev/zero\") failed\n");
        return -1;
    }

    shm->addr = (u_char *) mmap(NULL, shm->size, PROT_READ|PROT_WRITE,
                                MAP_SHARED, fd, 0);

    if (shm->addr == MAP_FAILED) {
        printf("mmap(/dev/zero, MAP_SHARED, %luz) failed\n", shm->size);
    }

    if (close(fd) == -1) {
        printf("close(\"/dev/zero\") failed\n");
    }

    return (shm->addr == MAP_FAILED) ? -1 : 0;
}


void
ngx_shm_free(ngx_shm_t *shm)
{
    if (munmap((void *) shm->addr, shm->size) == -1) {
        printf("munmap(%p, %luz) failed\n", shm->addr, shm->size);
    }
}

#elif (1)
ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    int  id;

    id = shmget(IPC_PRIVATE, shm->size, (SHM_R|SHM_W|IPC_CREAT));

    if (id == -1) {
        printf("shmget(%luz) failed\n", shm->size);
        return -1;
    }

    printf("shmget id: %d\n", id);

    shm->addr = shmat(id, NULL, 0);

    if (shm->addr == (void *) -1) {
        printf("shmat() failed\n");
    }

    if (shmctl(id, IPC_RMID, NULL) == -1) {
        printf("shmctl(IPC_RMID) failed\n");
    }

    return (shm->addr == (void *) -1) ? -1 : 0;
}


void
ngx_shm_free(ngx_shm_t *shm)
{
    if (shmdt(shm->addr) == -1) {
        printf("shmdt(%p) failed\n", shm->addr);
    }
}

#endif
