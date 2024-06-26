#ifndef _SHMEM_H_INCLUDED_
#define _SHMEM_H_INCLUDED_
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

typedef uintptr_t       uint_t;
typedef intptr_t        int_t;
typedef struct {
    size_t      len;
    u_char     *data;
} str_t;

typedef struct {
    u_char      *addr;
    size_t       size;
    str_t    name;
    uint_t   exists;   /* unsigned  exists:1;  */
} shm_t;

int_t shm_alloc(shm_t *shm);
void shm_free(shm_t *shm);


#endif /* _SHMEM_H_INCLUDED_ */
