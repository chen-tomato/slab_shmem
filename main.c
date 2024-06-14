

#include "ngx_slab.h"
#include "ngx_shmem.h"


int main(){
    ngx_shm_t                 shm;
    shm.size = (size_t) 1 << 13; //大小最低为 页内存大小+sizeof(ngx_slab_page_t),小了无法分配
    if(ngx_shm_alloc(&shm)!=0){
        printf("ngx_shm_alloc is faild\n");
        return -1;
    }
    ngx_slab_pool_t  *sp = (ngx_slab_pool_t *)shm.addr;
    sp->ngx_pagesize = getpagesize();
    sp->end = shm.addr + shm.size;
    ngx_slab_init(sp);
    int *temp =NULL;
    temp = ngx_slab_calloc(sp,sizeof(int));
    if(temp ==NULL){
        printf("ngx_slab_alloc is faild\n");
        return -1;
    }
    printf("temp addr = %p\n",temp);
    *temp=10;
    if(temp)
        ngx_slab_free(sp,temp);

    ngx_shm_free(&shm);
}

