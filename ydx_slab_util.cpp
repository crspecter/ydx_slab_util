#include "ydx_slab_util.h"
#include "logging.h"
namespace ydx
{
	static const double factor  =   1.25;			//每个数组的存储单元依次增长0.25倍
}

using namespace ydx;


#define POWER_SMALLEST 0	//slabclass_t数组的最小索引
#define POWER_LARGEST  33 
#define CHUNK_ALIGN_BYTES 8 //内存申请8字节对齐

#define SMALLEST_MALLOC 48
#define MAX_NUMBER_OF_SLAB_CLASSES (31 + 1)

Slab::Slab()
	:mem_alloc_(NULL),
	 mem_alloc_size_(0),
	 mem_base_(NULL),
	 mem_current_(NULL),
	 mem_avail_(0),
	 mem_malloced_(0)
{

}

Slab::~Slab()
{

}

bool Slab::init()
{
	
	mem_alloc_ = (mem_alloc_node*)malloc(sizeof(*mem_alloc_));
	mem_alloc_->next = NULL;
	mem_alloc_->prev = NULL;
	mem_alloc_->mem_base = malloc(block_memory);//一次分配32M内存
	if(NULL == mem_alloc_->mem_base)
	{
		LOG_ERROR << "Slab init failed";
		return false;
	}
	++mem_alloc_size_;
	

	mem_base_ = mem_alloc_->mem_base;
	mem_current_ = mem_base_;
	mem_avail_	= block_memory;

	memset(slabclass_, 0, sizeof(slabclass_));
	int index = POWER_SMALLEST;
	unsigned int size = sizeof(mem_node) + SMALLEST_MALLOC; //一个size的真实长度 = 头部(2个ptr)+存储长度	

	//初始化数组的size和node_cnt
	while ( index < POWER_LARGEST - 1 && size < per_block_limit / factor)
	{
		//8字节对齐调整
		if(size % CHUNK_ALIGN_BYTES)
			size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);

		slabclass_[index].size = size;
		slabclass_[index].node_cnt = per_block_limit / size;
		size *= factor;
		++index; 
	}
	////此处定义一个变量，存放最大使用到了数组的哪个元素，增加以后程序扩展的弹性，
	//如果可以配置最小内存单位较大，x * 1.25 ^ n 可能会很快超过1M,
	//因此不会用完全部的数组空位
	int power_max = index; 
	//完成将mem_base_指向的内存切割存放到数组成员中的过程
	slabs_preallocate(power_max);
	return true;
}

void Slab::slabs_preallocate(const unsigned int maxslabs)
{
	unsigned int i;
	for(i = POWER_SMALLEST; i < maxslabs; i++)
	{
		//分配每一个数组成员的内存
		if(do_slabs_newslab(i) == 0)
		{
			LOG_SYSFATAL << "do_slabs_newslab failed";
		}
	}
}

int Slab::do_slabs_newslab(const unsigned int id)
{
	slabclass_t *p = &slabclass_[id];
	unsigned int size = p->size * p->node_cnt; //求出一个数组成员中内存块需要的总长度
	char *ptr;

	
	if((ptr = (char*)memory_allocate((size_t)size)) == 0)
	{
		return 0;
	}

	//memset(ptr, 0, (size_t)size);		
	split_slab_page_into_freelist(ptr, id);
	mem_malloced_ += size;
	return 1;
}

void *Slab::memory_allocate(size_t size)
{
	void *ret;
	ret = mem_current_;

    if (size % CHUNK_ALIGN_BYTES) 
	{
        size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);
    }	
		
	mem_current_ = ((char*)mem_current_) + size;
	if(size < mem_avail_)
	{
		mem_avail_ -= size;
	}
	//当内存不足的情况出现，需要再分配一块内存
	//并将内存加入对象的链表中
	//此处是memcached不具备的功能
	else
	{
		
		mem_alloc_node *mem_node = (mem_alloc_node*)malloc(sizeof(*mem_alloc_));
		mem_node->mem_base = malloc(block_memory);//一次分配32M内存
		if(NULL == mem_alloc_->mem_base)
		{
			LOG_ERROR << "Slab init failed";
			return NULL;
		}

		//加入链表
		if(mem_alloc_ != NULL)
		{
			mem_alloc_->prev = mem_node;
			mem_node->next = mem_alloc_;
			mem_node->prev = NULL;
			
		}
		
		mem_alloc_ = mem_node;
		++mem_alloc_size_;
	
		mem_base_ = mem_alloc_->mem_base;
		mem_current_ = mem_base_;
		mem_avail_	= block_memory;	
		ret = mem_current_;
		mem_current_ = ((char*)mem_current_) + size;
		mem_avail_ -= size;  //reset avail
		mem_malloced_ = size;//reset malloced_
	}
	return ret;
}


void Slab::split_slab_page_into_freelist(char *ptr, const unsigned int id)
{
	slabclass_t *p = &slabclass_[id];
	for(unsigned int x = 0; x < p->node_cnt; x++)
	{
		//将空闲内存放入链表
		do_slabs_free(ptr, id);
		ptr += p->size;
	}
}



//当申请内存或者是否内存时，需要根据结点的内存大小，放入对应slabclass数组成员
int Slab::slabs_index(const size_t size)
{
	int ret = POWER_SMALLEST;
	if(size == 0)
		return 0;
	while(size > slabclass_[ret].size)
	{
		if(ret++ == POWER_LARGEST)
			return 0;
	}
	return ret;
}

//外部调用接口，分配大于等于指定长度size的内存
mem_node* Slab::slab_alloc(size_t size)
{
	mem_node* ret;
	//调用内部alloc函数，此处直接调用，如果需要修改为多线程安全，此处添加同步操作
	ret = (mem_node*)do_slabs_alloc(size);
	
	return ret;
}

void* Slab::do_slabs_alloc(const size_t size)
{
	int id = slabs_index(size);
	void* ret = NULL;
	slabclass_t *p;
	mem_node *node;
	p = &slabclass_[id];
   /* fail unless we have space at the end of a recently allocated page,
       we have something on our freelist, or we could allocate a new page */
	//空闲结点值=0时，做do_slabs_newslab操作，此处利用||的特性，当p->free_cnt!=0时，不会做分配操作	
	if(!(p->free_cnt != 0 || do_slabs_newslab(id) != 0))
	{
		ret = NULL;
	}
	else if(p->free_cnt != 0)
	{
		node = (mem_node*)p->nodes;
		node->id = id;
		p->nodes = node->next;
		if(node->next) node->next->prev = 0;
		--p->free_cnt;
		ret = (void*)node;
	}
	return ret;
}

void Slab::slab_free(mem_node* node)
{
	//调用内部free函数，此处直接调用，如果需要修改为多线程安全，此处添加同步操作
	do_slabs_free(node, node->id);
}

void Slab::do_slabs_free(void *ptr, const unsigned int id)
{
	slabclass_t *p;
	mem_node *node;
    if (id < POWER_SMALLEST || id > POWER_LARGEST)
    {
		LOG_SYSFATAL << "do_slabs_free failed id: " << id ;
	}
	p = &slabclass_[id];
	node = (mem_node*)ptr;
	node->prev = 0;
	//slabclass_数组经过memset清0，p->nodes初始为NULL
	node->next = (mem_node *)p->nodes;
	if(node->next) node->next->prev = node;
	p->nodes = node;
	p->free_cnt++;
	return;
	
}