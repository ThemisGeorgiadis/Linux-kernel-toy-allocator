#include <linux/toy_alloc.h>


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

void init_toy_allocator(void){

    t_alloc_metadata.head = NULL;
    t_alloc_metadata.toy_pages_count = 0;

    toy_allocator_initialized = 1;

}


unsigned long mark_objects(struct toy_page **curr_toy_page, size_t objects_needed, unsigned long start_obj){

    size_t objects_allocated = 0, page_offset = -1;
    int flag = 1;

    for(int i=start_obj; i<OBJS_PER_PAGE; i++){

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

unsigned long calc_size(size_t size, size_t max_size ){

    unsigned long tmp = size/max_size + 1;

    if(size%max_size == 0)
        tmp--;

    return tmp;
    
}

//Find contiguous objects inside the page. size in objects
unsigned long check_obj_continuity(struct toy_page *curr_toy_page, size_t objects_needed)
{
    size_t run = 0;
    unsigned long start = 0;

    for (int i = 0; i < OBJS_PER_PAGE; i++) {

        if (curr_toy_page->obj_available[i]) {

            if (run == 0)
                start = i;

            run++;

            if (run == objects_needed)
                return start;

        } else {

            run = 0;

        }
    }

    return -1;
}

//Find contiguous pages. size in bytes
unsigned long check_page_continuity(struct toy_pages_list *curr_page_l, size_t size){

    unsigned long curr_obj_sz_sum = 0;
    struct toy_pages_list *temp_node = NULL;
    struct toy_page *curr_toy_page = curr_page_l->page;

    if(curr_toy_page->free_obj_cnt < OBJS_PER_PAGE){

                curr_page_l = curr_page_l->next;
                return -1;

            //If size fits check the next page
            }else {

                curr_obj_sz_sum += PAGE_SIZE;
                temp_node = curr_page_l->next;

                while(temp_node){

                    //We need contiguous, completely free pages
                    if(temp_node->page &&
                        temp_node->page->free_obj_cnt < OBJS_PER_PAGE)
                    {

                        return -1;

                    }else{

                        curr_obj_sz_sum += PAGE_SIZE;

                        if(curr_obj_sz_sum >= size)
                            return curr_obj_sz_sum;

                        temp_node = temp_node->next;
                    }
                }

            }

            return -1;
}

struct toy_page *allocate_new_pages(struct toy_pages_list *curr_page_l,
     unsigned long pages_needed, size_t objects_needed, struct toy_pages_list **saved_l){

    struct toy_page *curr_toy_page;
    struct toy_page *first_page = NULL;

    unsigned long remaining = objects_needed;
    unsigned long curr_obj_sz_sum;
    unsigned long page_offset;

    for (int i = 0; i < pages_needed; i++) {

        t_alloc_metadata.toy_pages_count++;

        curr_page_l->page = new_toy_page();
        curr_toy_page = curr_page_l->page;

        if (i == 0) {
            first_page = curr_toy_page;
            *saved_l = curr_page_l;
        }

        if (remaining > OBJS_PER_PAGE) {
            remaining -= OBJS_PER_PAGE;
            curr_obj_sz_sum = OBJS_PER_PAGE;
        } else {
            curr_obj_sz_sum = remaining;
        }

        page_offset = mark_objects(&curr_toy_page, curr_obj_sz_sum, 0);

        if ((long)page_offset != -1) {

            if (i == 0)
                curr_toy_page->obj_alloc_length[0] = objects_needed;

            curr_toy_page->free_obj_cnt -= curr_obj_sz_sum;
        }

        if (i + 1 < pages_needed) {

            curr_page_l->next = kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);

            if (!curr_page_l->next)
                return NULL;

            curr_page_l = curr_page_l->next;

        } else {

            curr_page_l->next = NULL;
        }
    }

    return first_page;
}

void toy_free(void* ptr){

    if(!t_alloc_metadata.toy_pages_count)
        return;

    struct toy_pages_list * curr_page_l = t_alloc_metadata.head;

    while(curr_page_l){
        //pr_info("%px - %px : %px\n", curr_page_l->page->kaddr, curr_page_l->page->kaddr + PAGE_SIZE, ptr);
        if(ptr >= curr_page_l->page->kaddr && ptr < curr_page_l->page->kaddr + PAGE_SIZE){
            break;
        }
        curr_page_l = curr_page_l->next;
    }

    if(curr_page_l){

        unsigned long page_offset = (ptr - curr_page_l->page->kaddr)/OBJ_SIZE;
        int obj_freed_count = curr_page_l->page->obj_alloc_length[page_offset];

        unsigned long pages_count = obj_freed_count / OBJS_PER_PAGE + 1;
        if(obj_freed_count%OBJS_PER_PAGE == 0)
            pages_count--;

        unsigned long remaining = obj_freed_count;
        int current_count = 0;

        for(int i=0; i<pages_count; i++){

            if(remaining > OBJS_PER_PAGE){

                remaining -= OBJS_PER_PAGE;
                current_count = OBJS_PER_PAGE;

            }else
                current_count = remaining;
            
            curr_page_l->page->obj_alloc_length[page_offset] = 0;
            curr_page_l->page->free_obj_cnt += current_count;

            for(int i=0; i<current_count; i++)
                curr_page_l->page->obj_available[page_offset + i] = 1;
            
            
            curr_page_l = curr_page_l->next;


        }

        
    }

}

/*
    Should add flags to toy_alloc
    object size for current page??
    switch to dynamic object sizes? -> less fragmentation


*/

void* toy_alloc(size_t size){

    //NEED MULTIPLE PAGES FLAG
    int m_pages_flag = 0, flag = 1;
    unsigned long pages_needed = 1;

    unsigned long page_offset = 0, remaining = 0;
    unsigned long allocated = 0, start_obj = 0, curr_obj_sz_sum = 0;

    struct toy_pages_list *curr_page_l = NULL;
    struct toy_pages_list *prev_page_l = NULL;
    struct toy_pages_list *saved_l = NULL;
    struct toy_pages_list *temp_node = NULL;
    struct toy_page *curr_toy_page;
    struct toy_page *first_page;
    size_t objects_needed;

    if(!size)
        return NULL;

    if(size > PAGE_SIZE){

        m_pages_flag = 1;
        pages_needed = calc_size(size, PAGE_SIZE);

    }

    objects_needed = calc_size(size, OBJ_SIZE);


    //check if we could we could possibly fit the allocation in already allocated pages
    if(t_alloc_metadata.toy_pages_count >= pages_needed){

        curr_page_l = t_alloc_metadata.head;

        //Try to allocate on existing pages
        while(curr_page_l){

            prev_page_l = curr_page_l;
            curr_toy_page = curr_page_l->page;

            //Check if we have a contiguous allocation area
            if(!m_pages_flag){

                start_obj = check_obj_continuity(curr_toy_page,objects_needed);

                if(!(curr_toy_page->free_obj_cnt >= objects_needed &&
                     start_obj != -1)){

                    curr_page_l = curr_page_l->next;
                    continue;

                }

            }else{

                curr_obj_sz_sum = check_page_continuity(curr_page_l, size);
                
                //Failed to find contiguous, completely free pages
                if(!((long)curr_obj_sz_sum >= (long)size)){
                    curr_page_l = curr_page_l->next;
                    continue;
                }
                    
            }

            //IF WE REACH THIS POINT WE HAVE A VALID MEMORY AREA

            temp_node = curr_page_l;
            curr_obj_sz_sum = 0;
            remaining = objects_needed;
            flag = 1;

            //Mark the allocated objects
            for(int i = 0; i<pages_needed; i++){

                if(remaining > OBJS_PER_PAGE){

                    remaining -= OBJS_PER_PAGE;
                    curr_obj_sz_sum = OBJS_PER_PAGE;

                }else
                    curr_obj_sz_sum = remaining;

                unsigned long tmp_off = mark_objects(&curr_page_l->page,curr_obj_sz_sum,start_obj);

                if(flag){

                        page_offset = tmp_off;
                        flag = 0;
                        saved_l = curr_page_l;
                    }

                curr_page_l = curr_page_l->next;
            }

            curr_page_l = temp_node;
            curr_toy_page = curr_page_l->page;
            curr_obj_sz_sum = 0;
            remaining = objects_needed;

            if (((long)page_offset) == -1){

                curr_page_l = curr_page_l->next;
                continue;
            }

            //Save the allocated size in objects
            curr_toy_page->obj_alloc_length[page_offset] = objects_needed;

            for(int i = 0; i<pages_needed; i++){

               if(remaining > OBJS_PER_PAGE){

                   remaining -= OBJS_PER_PAGE;
                   curr_obj_sz_sum = OBJS_PER_PAGE;

                   temp_node->page->free_obj_cnt -= curr_obj_sz_sum;

               }else{

                   allocated = 1;
                   curr_obj_sz_sum = remaining;
                   temp_node->page->free_obj_cnt -= curr_obj_sz_sum;
               }

               temp_node = temp_node->next;

            }

            break;
            
        }

        curr_obj_sz_sum = 0;
        remaining = objects_needed;
        flag = 1;

        //No free, already allocated, pages found -> allocate new ones
        if (!allocated) {

            prev_page_l->next =
                kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);

            if (!prev_page_l->next)
                return NULL;

            prev_page_l->next->next = NULL;

            first_page = allocate_new_pages(prev_page_l->next, pages_needed, objects_needed, &saved_l);
            
            if (!first_page)
                return NULL;
            
            curr_toy_page = first_page;
        }

    //No pages allocated from buddy yet
    }else {

        t_alloc_metadata.head = kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);

        if (!t_alloc_metadata.head)
            return NULL;

        first_page = allocate_new_pages(t_alloc_metadata.head, pages_needed, objects_needed, &saved_l);

        if (!first_page)
            return NULL;

        curr_toy_page = first_page;
    }

    struct page **pages;
    temp_node = saved_l;

    pages = (struct page**)kmalloc_array(pages_needed, sizeof(*pages), GFP_KERNEL);

    //Add the pages to the array for vmap
    for(int i = 0; i < pages_needed; i++){

        if(temp_node->page->page_mapped &&
             m_pages_flag){

            kunmap_local(temp_node->page->kaddr);
            temp_node->page->page_mapped = 0;
        }

        pages[i] = temp_node->page->page;
        temp_node = temp_node->next;
    }


    void *kaddr = saved_l->page->kaddr;

    temp_node = saved_l;

    //If not mapped already
    if (!saved_l->page->page_mapped || !saved_l->page->kaddr){

        kaddr = vmap(pages, pages_needed, VM_MAP, PAGE_KERNEL);

        for(int i = 0; i < pages_needed; i++){

            temp_node->page->page_mapped = 1;
            temp_node->page->kaddr = kaddr + i * PAGE_SIZE;

            temp_node = temp_node->next;
        }

    }

    return (void *)((char *)kaddr + page_offset * OBJ_SIZE);

}



void print_toy_allocator_state(void)
{
    struct toy_pages_list *curr = t_alloc_metadata.head;
    int page_idx = 0;

    pr_info("=== Toy Allocator State ===\n");
    pr_info("Total pages: %u\n", t_alloc_metadata.toy_pages_count);

    while (curr) {

        struct toy_page *p = curr->page;

        pr_info("\n[Page %d]\n", page_idx);
        pr_info("  page struct: %px\n", p->page);
        pr_info("  mapped: %u\n", p->page_mapped);
        pr_info("  free_obj_cnt: %u\n", p->free_obj_cnt);
        pr_info("  kaddr: %px\n", p->kaddr);

        pr_info("  objects:\n");

        for (int i = 0; i < OBJS_PER_PAGE; i++) {

            void *obj_addr = (void *)((char *)p->kaddr + i * OBJ_SIZE);

            if (p->obj_alloc_length[i]) {
                pr_info("    [%02d] %px -> %s allocation size: %u objects == %u bytes\n",
                       i,
                       obj_addr,
                       p->obj_available[i] ? "FREE" : "USED",
                       p->obj_alloc_length[i],
                       p->obj_alloc_length[i] * OBJ_SIZE);
            } else {
                pr_info("    [%02d] %px -> %s\n",
                       i,
                       obj_addr,
                       p->obj_available[i] ? "FREE" : "USED");
            }
            
        }

        curr = curr->next;
        page_idx++;
    }

    pr_info("=== End State ===\n");
}


SYSCALL_DEFINE2(toy_alloc, unsigned int, val, unsigned int, size){

    if(!toy_allocator_initialized){
        init_toy_allocator();
    }

    //int *integer = (int*)toy_alloc(size);

    //*integer = 4;

    //pr_info("value: %d\n",*integer);

    int *integer1 = (int*)toy_alloc(2*PAGE_SIZE);
    int *integer2 = (int*)toy_alloc(PAGE_SIZE);

    print_toy_allocator_state();

    toy_free((void*)integer1);

    integer1 = (int*)toy_alloc(2*PAGE_SIZE - PAGE_SIZE/2);

    print_toy_allocator_state();

    int *integer3 = (int*)toy_alloc(PAGE_SIZE/3);
    int *integer4 = (int*)toy_alloc(PAGE_SIZE/3);

    print_toy_allocator_state();

   *integer1 = 67;
   *integer2 = 62;

   pr_alert("%d %d %d %d\n",*integer1,*integer2,*integer3,*integer4);

  //print_toy_allocator_state();


    return val;
}