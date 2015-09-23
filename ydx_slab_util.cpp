#include "ydx_slab_util.h"
#include "logging.h"
namespace ydx
{
	static const double factor  =   1.25;			//ÿ������Ĵ洢��Ԫ��������0.25��
}

using namespace ydx;


#define POWER_SMALLEST 0	//slabclass_t�������С����
#define POWER_LARGEST  33 
#define CHUNK_ALIGN_BYTES 8 //�ڴ�����8�ֽڶ���

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
	mem_alloc_->mem_base = malloc(block_memory);//һ�η���32M�ڴ�
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
	unsigned int size = sizeof(mem_node) + SMALLEST_MALLOC; //һ��size����ʵ���� = ͷ��(2��ptr)+�洢����	

	//��ʼ�������size��node_cnt
	while ( index < POWER_LARGEST - 1 && size < per_block_limit / factor)
	{
		//8�ֽڶ������
		if(size % CHUNK_ALIGN_BYTES)
			size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);

		slabclass_[index].size = size;
		slabclass_[index].node_cnt = per_block_limit / size;
		size *= factor;
		++index; 
	}
	////�˴�����һ��������������ʹ�õ���������ĸ�Ԫ�أ������Ժ������չ�ĵ��ԣ�
	//�������������С�ڴ浥λ�ϴ�x * 1.25 ^ n ���ܻ�ܿ쳬��1M,
	//��˲�������ȫ���������λ
	int power_max = index; 
	//��ɽ�mem_base_ָ����ڴ��и��ŵ������Ա�еĹ���
	slabs_preallocate(power_max);
	return true;
}

void Slab::slabs_preallocate(const unsigned int maxslabs)
{
	unsigned int i;
	for(i = POWER_SMALLEST; i < maxslabs; i++)
	{
		//����ÿһ�������Ա���ڴ�
		if(do_slabs_newslab(i) == 0)
		{
			LOG_SYSFATAL << "do_slabs_newslab failed";
		}
	}
}

int Slab::do_slabs_newslab(const unsigned int id)
{
	slabclass_t *p = &slabclass_[id];
	unsigned int size = p->size * p->node_cnt; //���һ�������Ա���ڴ����Ҫ���ܳ���
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
	//���ڴ治���������֣���Ҫ�ٷ���һ���ڴ�
	//�����ڴ��������������
	//�˴���memcached���߱��Ĺ���
	else
	{
		
		mem_alloc_node *mem_node = (mem_alloc_node*)malloc(sizeof(*mem_alloc_));
		mem_node->mem_base = malloc(block_memory);//һ�η���32M�ڴ�
		if(NULL == mem_alloc_->mem_base)
		{
			LOG_ERROR << "Slab init failed";
			return NULL;
		}

		//��������
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
		//�������ڴ��������
		do_slabs_free(ptr, id);
		ptr += p->size;
	}
}



//�������ڴ�����Ƿ��ڴ�ʱ����Ҫ���ݽ����ڴ��С�������Ӧslabclass�����Ա
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

//�ⲿ���ýӿڣ�������ڵ���ָ������size���ڴ�
mem_node* Slab::slab_alloc(size_t size)
{
	mem_node* ret;
	//�����ڲ�alloc�������˴�ֱ�ӵ��ã������Ҫ�޸�Ϊ���̰߳�ȫ���˴����ͬ������
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
	//���н��ֵ=0ʱ����do_slabs_newslab�������˴�����||�����ԣ���p->free_cnt!=0ʱ���������������	
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
	//�����ڲ�free�������˴�ֱ�ӵ��ã������Ҫ�޸�Ϊ���̰߳�ȫ���˴����ͬ������
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
	//slabclass_���龭��memset��0��p->nodes��ʼΪNULL
	node->next = (mem_node *)p->nodes;
	if(node->next) node->next->prev = node;
	p->nodes = node;
	p->free_cnt++;
	return;
	
}