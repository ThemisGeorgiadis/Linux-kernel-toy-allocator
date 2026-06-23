#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#define OBJ_SIZE 128
#define OBJS_PER_PAGE (PAGE_SIZE/OBJ_SIZE)


struct toy_pages_list;

struct toy_page{

    void *kaddr;
    unsigned int page_mapped;
    struct page *page;
    unsigned int free_obj_cnt;
    struct toy_pages_list *head;
    //No freelist yet
    unsigned int obj_available[OBJS_PER_PAGE];
    unsigned int obj_alloc_length[OBJS_PER_PAGE];

    //Future use
    unsigned int obj_count;
    unsigned int **obj_alloc_length_bytes; //obj_alloc_length_bytes[obj_count]

};

struct toy_pages_list{

    struct toy_page *page;
    struct toy_pages_list *next;

};

struct toy_alloc_metadata{
    unsigned int toy_pages_count;
    struct toy_pages_list *head;

} t_alloc_metadata;

int toy_allocator_initialized = 0;

void toy_alloc_internal_init(void);
int check_internal_page_array(void);
void* toy_alloc_internal(size_t size);

unsigned long calc_size(size_t size, size_t max_size);

struct toy_page *allocate_new_pages(struct toy_pages_list *curr_page_l,
     unsigned long pages_needed, size_t objects_needed, struct toy_pages_list **saved_l);

struct toy_page* new_toy_page(void);

void init_toy_allocator(void);

void* toy_alloc(size_t size);

unsigned long mark_objects(struct toy_page **curr_toy_page, size_t objects_needed, unsigned long start_obj);

unsigned long check_obj_continuity(struct toy_page *curr_toy_page, size_t objects_needed);

unsigned long check_page_continuity(struct toy_pages_list *curr_page_l, size_t size);

void toy_free(void* ptr);

void print_toy_allocator_state(void);