#ifndef _NGX_SHMEM_H_INCLUDED_
#define _NGX_SHMEM_H_INCLUDED_
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

typedef uintptr_t       ngx_uint_t;
typedef intptr_t        ngx_int_t;
typedef struct {
    size_t      len;
    u_char     *data;
} ngx_str_t;

typedef struct {
    u_char      *addr;
    size_t       size;
    ngx_str_t    name;
    ngx_uint_t   exists;   /* unsigned  exists:1;  */
} ngx_shm_t;

ngx_int_t ngx_shm_alloc(ngx_shm_t *shm);
void ngx_shm_free(ngx_shm_t *shm);


#endif /* _NGX_SHMEM_H_INCLUDED_ */
