#ifndef __RTE_SLUB_H__
#define __RTE_SLUB_H__
#include <assert.h>
#include <stdio.h>
#include "types.h"
#include "list.h"
#include "spinlock.h"

struct mem_cache_cpu{
	void **freelist; // 指向本地Local slab的空闲Obj链表
	struct rte_page *page;
};

struct mem_cache_node{
	rte_spinlock_t list_lock;
	unsigned long nr_partial;
	struct list_head partial;
};

#define RTE_SLAB_BASE_SIZE 64
#define RTE_SHM_CACHE_NUM 14
#define RTE_OO_SHIFT 16
#define RTE_OO_MASK ((1UL<<RTE_OO_SHIFT)-1)

#define RTE_MAX_CPU_NUM 8

/* 每种规格的slab都对应一个 struct rte_mem_caches 结构体 */
struct rte_mem_cache{
	struct mem_cache_cpu cpu_slab[RTE_MAX_CPU_NUM]; // 每个Core对应一个
	int32_t size; // 本mem_cache中slab的规格
	int32_t offset; // 页中的空闲slab组成一个链表，在slab中便宜量为offset的地方中存放下一个slab的地址
	uint64_t oo; // oo = order<<OO_SHIFT |slab_num（存在slab占用多个页的情况）
	struct mem_cache_node local_node;
	uint64_t min_partial;
};

static inline void RTE_SLUB_BUG(const char *name, int line)
{
	printf("SLUB_BUG On file %s line %d.\n", name, line);	
	assert(0);
}

/* Only for x86_64, from arch/x86/include/asm/bitops.h */
static inline unsigned int rte_fls(unsigned int x)
{
	asm("bsr %1,%0"
	    : "=r" (x)
	    : "rm" (x));
	return x;
}


int rte_slub_system_init(struct rte_mem_cache *array, int cache_num);
void * __rte_slub_alloc(uint32_t size);
void __rte_slub_free(void *ptr);

#endif
