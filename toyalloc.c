#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <linux/types.h>
#include <linux/hugetlb.h>

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
    }

    return toypage;

}

void init_toy_allocator(void){

    t_alloc_metadata.head = NULL;
    t_alloc_metadata.toy_pages_count = 0;

}

void* toy_alloc(size_t size){

    if(!size)
        return NULL;

    size_t objects_needed = size/OBJ_SIZE + 1;

    struct toy_pages_list *curr_page_l = NULL;
    struct toy_pages_list *prev_page_l = NULL;
    struct toy_page *curr_toy_page;

    unsigned int page_offset = 0;
    int allocated = 0;

    //is there a page with free objects?
    if(t_alloc_metadata.toy_pages_count){

        size_t objects_allocated;

        curr_page_l = t_alloc_metadata.head;

        while(curr_page_l){

            objects_allocated = 0;

            prev_page_l = curr_page_l;

            curr_toy_page = curr_page_l->page;

            allocated = 0;

            if(!(curr_toy_page->free_obj_cnt >= objects_needed)){

                curr_page_l = curr_page_l->next;
                continue;

            }

            int flag = 1;

            for(int i=0; i<OBJS_PER_PAGE; i++){

                if(curr_toy_page->obj_available[i] == 1){
                    curr_toy_page->obj_available[i] = 0;
                    objects_allocated++;
                    if(objects_allocated == objects_needed){
                        if(flag){
                            page_offset = i;
                            flag = 0;
                        }
                        allocated = 1;
                        break;
                    }
                }

            }

            if (allocated){

                curr_toy_page->free_obj_cnt -= objects_allocated;

                break;
            }

            curr_page_l = curr_page_l->next;

        }

        if(!curr_page_l && objects_allocated != objects_needed){

            prev_page_l->next = kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);
            if (!prev_page_l->next)
                return NULL;

            prev_page_l->next->next = NULL;
            curr_page_l = prev_page_l->next;

            curr_page_l->page = new_toy_page();
            curr_toy_page = curr_page_l->page;

            objects_allocated = 0;
            allocated = 0;

            int flag = 1;

            for(int i=0; i<OBJS_PER_PAGE; i++){

                if(curr_toy_page->obj_available[i] == 1){
                    curr_toy_page->obj_available[i] = 0;
                    objects_allocated++;
                    if(objects_allocated == objects_needed){
                        if(flag){
                            page_offset = i;
                            flag = 0;
                        }
                        allocated = 1;
                        break;
                    }
                }

                if (allocated){

                    curr_toy_page->free_obj_cnt -= objects_allocated;

                    break;
                }

            }

        }

        

    }else{

        t_alloc_metadata.head = kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);
        t_alloc_metadata.head->page = new_toy_page();
        
        curr_toy_page = t_alloc_metadata.head->page;
        t_alloc_metadata.head->next = NULL;
        t_alloc_metadata.toy_pages_count = 1;

        size_t objects_allocated = 0;

        for(int i=0; i<OBJS_PER_PAGE; i++){

                if(curr_toy_page->obj_available[i] == 1){
                    curr_toy_page->obj_available[i] = 0;
                    objects_allocated++;
                    if(objects_allocated == objects_needed){
                        page_offset = i;
                        curr_toy_page->free_obj_cnt -= objects_allocated;
                        break;
                    }
                }
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

            printk(KERN_INFO "    [%02d] %px -> %s\n",
                   i,
                   obj_addr,
                   p->obj_available[i] ? "FREE" : "USED");
        }

        curr = curr->next;
        page_idx++;
    }

    printk(KERN_INFO "=== End State ===\n");
}


SYSCALL_DEFINE2(taskinspect, unsigned int, val, unsigned int, size){

    if(val == 1){
        init_toy_allocator();
    }

    int *integer = (int*)toy_alloc(size);

    *integer = 4;

    pr_info("value: %d\n",*integer);

    print_toy_allocator_state();


    return val;
}