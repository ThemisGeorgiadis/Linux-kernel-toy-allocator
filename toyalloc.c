#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <linux/types.h>

#define OBJ_SIZE 64
#define OBJS_PER_PAGE 64

//64byte objects;

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

};

struct toy_pages_list{

    struct toy_page *page;
    struct toy_pages_list *next;

};

struct toy_alloc_metadata{
    unsigned int toy_pages_count;
    struct toy_pages_list *head;

} t_alloc_metadata;

struct toy_page* new_toy_page(void);
void init_toy_allocator(void);
void* toy_alloc(size_t size);

struct toy_page* new_toy_page(){

    struct toy_page *toypage = kmalloc(sizeof(struct toy_page), GFP_KERNEL);
    if (!toypage)
        return NULL;

    toypage->page = alloc_page(GFP_KERNEL);
    toypage->page_mapped = 0;
    toypage->free_obj_cnt = OBJS_PER_PAGE;
    toypage->head = t_alloc_metadata.head;

    for(int i=0; i<OBJS_PER_PAGE; i++){
        toypage->obj_available[i] = 1;
        toypage->obj_alloc_length[i] = 0;
    }

    return toypage;

}

int toy_allocator_initialized = 0;

void init_toy_allocator(void){

    t_alloc_metadata.head = NULL;
    t_alloc_metadata.toy_pages_count = 0;

    toy_allocator_initialized = 1;

}

unsigned long mark_objects(struct toy_page **curr_toy_page, size_t objects_needed);

unsigned long mark_objects(struct toy_page **curr_toy_page, size_t objects_needed){

    size_t objects_allocated = 0, page_offset = -1;
    int flag = 1;

    for(int i=0; i<OBJS_PER_PAGE; i++){

                if((*curr_toy_page)->obj_available[i] == 1){

                    (*curr_toy_page)->obj_available[i] = 0;
                    objects_allocated++;

                    if(flag){
                            page_offset = i;
                            flag = 0;
                    }

                    if(objects_allocated == objects_needed){
                        
                        break;
                    }
                }

    }
    return page_offset;
}

int check_continuity(struct toy_page **curr_toy_page, size_t objects_needed);

int check_continuity(struct toy_page **curr_toy_page, size_t objects_needed){

    int countcont = 0, countoccupied = 0;

    for(int i=0; i<OBJS_PER_PAGE; i++){

        countcont++;

        if((*curr_toy_page)->obj_available[i] == 0)
            countcont = 0;
        else
            countoccupied++;


        if(OBJS_PER_PAGE - i  == objects_needed && !countcont)
            return 0;

        if(OBJS_PER_PAGE - i  == objects_needed && countoccupied < OBJS_PER_PAGE - (*curr_toy_page)->free_obj_cnt)
            return 0;
        
        if(OBJS_PER_PAGE - i  == objects_needed && countcont && countoccupied >= OBJS_PER_PAGE - (*curr_toy_page)->free_obj_cnt)
            return 1;
            
    }

    if(countcont >= objects_needed)
        return 1;
    else
        return 0;

}
void toy_free(void* ptr);
void toy_free(void* ptr){

    if(!t_alloc_metadata.toy_pages_count)
        return;

    struct toy_pages_list * curr_page_l = t_alloc_metadata.head;

    while(curr_page_l){
        printk("%px - %px : %px\n", curr_page_l->page->kaddr, curr_page_l->page->kaddr + PAGE_SIZE, ptr);
        if(ptr >= curr_page_l->page->kaddr && ptr < curr_page_l->page->kaddr + PAGE_SIZE){
            break;
        }
        curr_page_l = curr_page_l->next;
    }

    if(curr_page_l){
        unsigned long page_offset = (ptr - curr_page_l->page->kaddr)/OBJ_SIZE;
        int obj_freed_count = curr_page_l->page->obj_alloc_length[page_offset];

        curr_page_l->page->obj_alloc_length[page_offset] = 0;
        curr_page_l->page->free_obj_cnt += obj_freed_count;

        for(int i=0; i<obj_freed_count; i++){
            curr_page_l->page->obj_available[page_offset + i] = 1;
        }
        
    }

}

void* toy_alloc(size_t size){

    if(!size)
        return NULL;

    size_t objects_needed = size/OBJ_SIZE + 1;
    if(size%OBJ_SIZE == 0)
        objects_needed--;

    struct toy_pages_list *curr_page_l = NULL;
    struct toy_pages_list *prev_page_l = NULL;
    struct toy_page *curr_toy_page;

    unsigned long page_offset = 0;
    int allocated = 0;

    //is there a page with free objects?
    if(t_alloc_metadata.toy_pages_count){

        curr_page_l = t_alloc_metadata.head;

        while(curr_page_l){

            prev_page_l = curr_page_l;

            curr_toy_page = curr_page_l->page;

            if(!(curr_toy_page->free_obj_cnt >= objects_needed && check_continuity(&curr_toy_page,objects_needed))){

                curr_page_l = curr_page_l->next;
                continue;

            }

            page_offset = mark_objects(&curr_toy_page,objects_needed);

            if (((long)page_offset) != -1){

                curr_toy_page->obj_alloc_length[page_offset] = objects_needed;

                curr_toy_page->free_obj_cnt -= objects_needed;
                allocated = 1;

                break;
            }

            curr_page_l = curr_page_l->next;

        }

        if(!curr_page_l && !allocated){

            prev_page_l->next = kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);

            if (!prev_page_l->next)
                return NULL;

            prev_page_l->next->next = NULL;
            curr_page_l = prev_page_l->next;

            curr_page_l->page = new_toy_page();
            curr_toy_page = curr_page_l->page;

            page_offset = mark_objects(&curr_toy_page,objects_needed);

            if (((long)page_offset) != -1){

                curr_toy_page->obj_alloc_length[page_offset] = objects_needed;

                curr_toy_page->free_obj_cnt -= objects_needed;

            }

        }

        

    }else{

        t_alloc_metadata.head = kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);
        t_alloc_metadata.head->page = new_toy_page();
        
        curr_toy_page = t_alloc_metadata.head->page;
        t_alloc_metadata.head->next = NULL;
        t_alloc_metadata.toy_pages_count = 1;

        page_offset = mark_objects(&curr_toy_page,objects_needed);

        if (((long)page_offset) != -1){

            curr_toy_page->obj_alloc_length[page_offset] = objects_needed;

            curr_toy_page->free_obj_cnt -= objects_needed;

        }

    }

    void *kaddr = curr_toy_page->kaddr;
    if (!curr_toy_page->page_mapped || !curr_toy_page->kaddr) {
        //NOTE: should use vmap
        kaddr = kmap_local_page(curr_toy_page->page);
        curr_toy_page->page_mapped = 1;
        curr_toy_page->kaddr = kaddr;
    }

    return (void *)((char *)kaddr + page_offset * OBJ_SIZE);

}


void print_toy_allocator_state(void);

void print_toy_allocator_state(void)
{
    struct toy_pages_list *curr = t_alloc_metadata.head;
    int page_idx = 0;

    printk(KERN_INFO "=== Toy Allocator State ===\n");
    printk(KERN_INFO "Total pages: %u\n", t_alloc_metadata.toy_pages_count);

    while (curr) {

        struct toy_page *p = curr->page;

        printk(KERN_INFO "\n[Page %d]\n", page_idx);
        printk(KERN_INFO "  page struct: %px\n", p->page);
        printk(KERN_INFO "  mapped: %u\n", p->page_mapped);
        printk(KERN_INFO "  free_obj_cnt: %u\n", p->free_obj_cnt);
        printk(KERN_INFO "  kaddr: %px\n", p->kaddr);

        printk(KERN_INFO "  objects:\n");

        for (int i = 0; i < OBJS_PER_PAGE; i++) {

            void *obj_addr = (void *)((char *)p->kaddr + i * OBJ_SIZE);

            if (p->obj_alloc_length[i]) {
                printk(KERN_INFO "    [%02d] %px -> %s allocation size: %u\n",
                       i,
                       obj_addr,
                       p->obj_available[i] ? "FREE" : "USED",
                       p->obj_alloc_length[i]);
            } else {
                printk(KERN_INFO "    [%02d] %px -> %s\n",
                       i,
                       obj_addr,
                       p->obj_available[i] ? "FREE" : "USED");
            }
            
        }

        curr = curr->next;
        page_idx++;
    }

    printk(KERN_INFO "=== End State ===\n");
}


SYSCALL_DEFINE2(taskinspect, unsigned int, val, unsigned int, size){


    if(!toy_allocator_initialized){
        init_toy_allocator();
    }

    int *integer = (int*)toy_alloc(size);

    *integer = 4;

    pr_info("value: %d\n",*integer);

    print_toy_allocator_state();

    if(val){toy_free((void*)integer);

    print_toy_allocator_state();}


    return val;
}