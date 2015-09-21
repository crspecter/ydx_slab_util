#ifndef __YDX_SLAB_UTIL_H__
#define __YDX_SLAB_UTIL_H__
#include <stddef.h>

#include "stdint.h"

namespace ydx
{

//内存对外分配单元
struct mem_node
{
	struct mem_node *next;
	struct mem_node *prev;
	int id;
	char data[];
};
/*
内存块分配链表，每个内存块32M，第一次分配的32M会，均分到slabclass数组的每个成员.每个数组用1M内存做切割
当有哪一个数组中出现内存不足时，再次分配32M,并往内存不足成员分配1M，剩余内存做后续使用。
每次分配的内存装载到Slab::mem_alloc_链表中
*/

struct mem_alloc_node
{
	struct mem_alloc_node* next;
	struct mem_alloc_node* prev;
	void *mem_base;
};

/*
	一共32个链表，每个依次从48字节(对外尺寸，实际会加上mem_node的其他字段，再按8做对齐)，
	每个结点值比前一个大1.25。最大的内存目前48 x 1.25 ^ 31 = 48467byte，1M = 1048576 byte
	设计时注意最大结点的大小要小于1M
*/
//slabclass_t对象用于记录32个数组成员中，每一个成员的信息
typedef struct
{
	unsigned int size; //当前class中，每个node的内存大小
	unsigned int node_cnt; //当前class中可以存储多少个node 1M/size
	unsigned int free_cnt;	//当前空闲结点数
	void *nodes;    //空闲node的链表，1M内存会切割成多个node，放入链表中mem_node链表中，nodes指向链表的头部
	
}slabclass_t;

//内存池对象
class Slab
{
public:
	Slab();
	~Slab();

	bool init();
	mem_node* slab_alloc(size_t size);
	void  	slab_free(mem_node* node);
	int   	slabs_index(const size_t size);
	void    slab_destroy();

private:
	 void slabs_preallocate(const unsigned int maxslabs);
	 int  do_slabs_newslab(const unsigned int id);
	 void* memory_allocate(size_t size);
	 void* do_slabs_alloc(const size_t size);
	 void do_slabs_free(void *ptr, const unsigned int id);
	 void split_slab_page_into_freelist(char *ptr, const unsigned int id);
	 
private:	
	static const int block_memory = 32 * 1024 * 1024; //每次申请内存的大小
	static const int per_block_limit = 1 * 1024 * 1024;
	static const int slab_class_size = 32;			//slab_class数组的大小
	
private:
	mem_alloc_node *mem_alloc_;
	uint64_t 	mem_alloc_size_;
	
	void*		mem_base_;	//指向当前的32M内存块的起始
	void*		mem_current_;//指向当前的32M内存空闲的起始
	size_t 		mem_avail_;  //记录当前32M内存中，可以用的内存大小
	size_t 		mem_malloced_;//记录当前32M内存中已经分配的内存
	
	slabclass_t slabclass_[slab_class_size];
};

}

#endif
