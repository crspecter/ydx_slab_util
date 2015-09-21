#ifndef __YDX_SLAB_UTIL_H__
#define __YDX_SLAB_UTIL_H__
#include <stddef.h>

#include "stdint.h"

namespace ydx
{

//�ڴ������䵥Ԫ
struct mem_node
{
	struct mem_node *next;
	struct mem_node *prev;
	int id;
	char data[];
};
/*
�ڴ���������ÿ���ڴ��32M����һ�η����32M�ᣬ���ֵ�slabclass�����ÿ����Ա.ÿ��������1M�ڴ����и�
������һ�������г����ڴ治��ʱ���ٴη���32M,�����ڴ治���Ա����1M��ʣ���ڴ�������ʹ�á�
ÿ�η�����ڴ�װ�ص�Slab::mem_alloc_������
*/

struct mem_alloc_node
{
	struct mem_alloc_node* next;
	struct mem_alloc_node* prev;
	void *mem_base;
};

/*
	һ��32������ÿ�����δ�48�ֽ�(����ߴ磬ʵ�ʻ����mem_node�������ֶΣ��ٰ�8������)��
	ÿ�����ֵ��ǰһ����1.25�������ڴ�Ŀǰ48 x 1.25 ^ 31 = 48467byte��1M = 1048576 byte
	���ʱע�������Ĵ�СҪС��1M
*/
//slabclass_t�������ڼ�¼32�������Ա�У�ÿһ����Ա����Ϣ
typedef struct
{
	unsigned int size; //��ǰclass�У�ÿ��node���ڴ��С
	unsigned int node_cnt; //��ǰclass�п��Դ洢���ٸ�node 1M/size
	unsigned int free_cnt;	//��ǰ���н����
	void *nodes;    //����node������1M�ڴ���и�ɶ��node������������mem_node�����У�nodesָ�������ͷ��
	
}slabclass_t;

//�ڴ�ض���
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
	static const int block_memory = 32 * 1024 * 1024; //ÿ�������ڴ�Ĵ�С
	static const int per_block_limit = 1 * 1024 * 1024;
	static const int slab_class_size = 32;			//slab_class����Ĵ�С
	
private:
	mem_alloc_node *mem_alloc_;
	uint64_t 	mem_alloc_size_;
	
	void*		mem_base_;	//ָ��ǰ��32M�ڴ�����ʼ
	void*		mem_current_;//ָ��ǰ��32M�ڴ���е���ʼ
	size_t 		mem_avail_;  //��¼��ǰ32M�ڴ��У������õ��ڴ��С
	size_t 		mem_malloced_;//��¼��ǰ32M�ڴ����Ѿ�������ڴ�
	
	slabclass_t slabclass_[slab_class_size];
};

}

#endif
