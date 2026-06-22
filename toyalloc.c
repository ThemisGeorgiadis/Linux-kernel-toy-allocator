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


unsigned long check_continuity(struct toy_page **curr_toy_page, size_t objects_needed)
{
    size_t run = 0;
    unsigned long start = 0;

    for (int i = 0; i < OBJS_PER_PAGE; i++) {

        if ((*curr_toy_page)->obj_available[i]) {

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

void toy_free(void* ptr){

    if(!t_alloc_metadata.toy_pages_count)
        return;

    struct toy_pages_list * curr_page_l = t_alloc_metadata.head;

    while(curr_page_l){
        //printk("%px - %px : %px\n", curr_page_l->page->kaddr, curr_page_l->page->kaddr + PAGE_SIZE, ptr);
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
        pages_needed = size/PAGE_SIZE + 1;

        if(size%PAGE_SIZE == 0)
            pages_needed--;
    }


    objects_needed = size/OBJ_SIZE + 1;
    if(size%OBJ_SIZE == 0)
        objects_needed--;


    //check if we could we could possibly fit the allocation in already allocated pages
    if(t_alloc_metadata.toy_pages_count >= pages_needed){

        curr_page_l = t_alloc_metadata.head;

        //Try to allocate on existing pages
        while(curr_page_l){

            prev_page_l = curr_page_l;
            curr_toy_page = curr_page_l->page;

            curr_obj_sz_sum = 0;

            //If size doesnt fit skip to the next page
            if(m_pages_flag && curr_toy_page->free_obj_cnt < OBJS_PER_PAGE){

                curr_page_l = curr_page_l->next;
                continue;

            //If size fits check the next page
            }else if(m_pages_flag){

                curr_obj_sz_sum += PAGE_SIZE;
                temp_node = curr_page_l->next;

                while(temp_node){

                    //We need contiguous, completely free pages
                    if(temp_node->page &&
                        temp_node->page->free_obj_cnt < OBJS_PER_PAGE)
                    {

                        curr_page_l = curr_page_l->next;
                        break;

                    }else{

                        curr_obj_sz_sum += PAGE_SIZE;

                        if(curr_obj_sz_sum >= size)
                            break;

                        temp_node = temp_node->next;
                    }
                }

            }

            //Check if we found contiguous allocation area
            if(!m_pages_flag){
                start_obj = check_continuity(&curr_toy_page,objects_needed);

                if(!(curr_toy_page->free_obj_cnt >= objects_needed &&
                     start_obj != -1)){

                    curr_page_l = curr_page_l->next;
                    continue;

                }

            }else{
                
                //Failed to find contiguous, completely free pages
                if(!(curr_obj_sz_sum >= size)){
                    curr_page_l = curr_page_l->next;
                    continue;
                }
                    
            }

            temp_node = curr_page_l;
            curr_obj_sz_sum = 0;
            remaining = objects_needed;
            flag = 1;

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

            if (((long)page_offset) != -1){

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

            curr_page_l = curr_page_l->next;

        }

        curr_obj_sz_sum = 0;
        remaining = objects_needed;
        flag = 1;

        //No free, already allocated, pages found -> allocate new ones
        if(!allocated){ //!curr_page_l &&
            
            for(int i = 0; i<pages_needed; i++){

                prev_page_l->next = kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);

                if (!prev_page_l->next)
                    return NULL;

                prev_page_l->next->next = NULL;
                t_alloc_metadata.toy_pages_count++;
                curr_page_l = prev_page_l->next;

                curr_page_l->page = new_toy_page();
                curr_toy_page = curr_page_l->page;

                if(flag){
                    first_page = curr_toy_page;
                    saved_l = curr_page_l;
                }

                if(remaining > OBJS_PER_PAGE){
                    remaining -= OBJS_PER_PAGE;
                    curr_obj_sz_sum = OBJS_PER_PAGE;
                }else
                    curr_obj_sz_sum = remaining;

                page_offset = mark_objects(&curr_toy_page,curr_obj_sz_sum,0);

                if (((long)page_offset) != -1){

                    if(flag){
                        curr_toy_page->obj_alloc_length[0] = objects_needed;
                        flag = 0;
                    }

                    curr_toy_page->free_obj_cnt -= curr_obj_sz_sum;

                }

                prev_page_l = curr_page_l;

            }

            curr_toy_page = first_page;

        }

    //No pages allocated from buddy yet
    }else{

        struct toy_pages_list *head;
        struct toy_pages_list *curr;
        
        flag = 1;
        remaining = objects_needed;

        head = kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);

        if (!head)
            return NULL;

        t_alloc_metadata.head = head;
        curr = head;

        for(int i = 0; i < pages_needed; i++){

            curr->page = new_toy_page();

            if(flag){
                first_page = curr->page;
                saved_l = curr;

            }

            curr_toy_page = curr->page;
            t_alloc_metadata.toy_pages_count++;

            if(remaining > OBJS_PER_PAGE){
                remaining -= OBJS_PER_PAGE;
                curr_obj_sz_sum = OBJS_PER_PAGE;
            }else{
                curr_obj_sz_sum = remaining;
            }

            page_offset = mark_objects(&curr_toy_page, curr_obj_sz_sum, 0);

            if(((long)page_offset) != -1){

                if(flag){
                    curr_toy_page->obj_alloc_length[0] = objects_needed;
                    flag = 0;
                }

                curr_toy_page->free_obj_cnt -= curr_obj_sz_sum;
            }

            if(i + 1 < pages_needed){

                curr->next = kmalloc(sizeof(struct toy_pages_list), GFP_KERNEL);
                if(!curr->next)
                    return NULL;

                curr = curr->next;

            }else{

                curr->next = NULL;

            }
        }

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

    //int *integer = (int*)toy_alloc(size);

    //*integer = 4;

    //pr_info("value: %d\n",*integer);

    int *integer1 = (int*)toy_alloc(2*PAGE_SIZE);
    int *integer2 = (int*)toy_alloc(PAGE_SIZE);

    print_toy_allocator_state();

    toy_free((void*)integer1);

    print_toy_allocator_state();

    int *integer3 = (int*)toy_alloc(PAGE_SIZE/2);
    int *integer4 = (int*)toy_alloc(PAGE_SIZE/2);

    print_toy_allocator_state();

   *integer1 = 67;
   *integer2 = 62;

   pr_alert("%d %d %d %d\n",*integer1,*integer2,*integer3,*integer4);

  //print_toy_allocator_state();


    return val;
}