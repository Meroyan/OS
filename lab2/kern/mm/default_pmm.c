#include <pmm.h>
#include <list.h>
#include <string.h>
#include <default_pmm.h>

/* In the first fit algorithm, the allocator keeps a list of free blocks (known as the free list) and,
   on receiving a request for memory, scans along the list for the first block that is large enough to
   satisfy the request. If the chosen block is significantly larger than that requested, then it is
   usually split, and the remainder added to the list as another free block.
   Please see Page 196~198, Section 8.2 of Yan Wei Min's chinese book "Data Structure -- C programming language"
   在first fit算法中，分配器维持了一个空闲块的列表free list，并且在收到内存请求时，扫描列表找到符合的第一个
   足够大的块。如果选择的块远大于请求的大小，通常会拆分他，并将剩余部分作为空闲块加到列表中/
   */
   // you should rewrite functions: default_init,default_init_memmap,default_alloc_pages, default_free_pages.
   //你应当重新写default_init,default_init_memmap,default_alloc_pages, default_free_pages.

   /*
    * Details of FFMA
    * (1) Prepare: In order to implement the First-Fit Mem Alloc (FFMA), we should manage the free mem block use some list.
    *              The struct free_area_t is used for the management of free mem blocks. At first you should
    *              be familiar to the struct list in list.h. struct list is a simple doubly linked list implementation.
    *              You should know how to USE: list_init, list_add(list_add_after), list_add_before, list_del, list_next, list_prev
    *              Another tricky method is to transform a general list struct to a special struct (such as struct page):
    *              you can find some MACRO: le2page (in memlayout.h), (in future labs: le2vma (in vmm.h), le2proc (in proc.h),etc.)
    * （1）准备：为了实现FFMA，我们应该使用一些列表管理空闲的内存块。free_area_t结构被用于管理空闲内存块。
    * 在最开始，你应该熟悉list.h中的list结构，是一个简单的双向链表实现。
    * 你应该知道如何使用list_init, list_add(list_add_after), list_add_before, list_del, list_next, list_prev。
    * 另外一个棘手的方法是将一个普通的list结构转化为一个特殊的结构（例如page结构），
    * 你可以找到一些MACRO，le2page (in memlayout.h), (in future labs: le2vma (in vmm.h), le2proc (in proc.h),etc.)。
    *
    * (2) default_init: you can reuse the  demo default_init fun to init the free_list and set nr_free to 0.
    *              free_list is used to record the free mem blocks. nr_free is the total number for free mem blocks.
    * （2）default_init：你可以重用demo default_init去初始化free_list，并将nr_free设置为0.
    * free_list被用于记录空闲内存块，nr_free是空闲内存块的数量。
    *
    * (3) default_init_memmap:  CALL GRAPH: kern_init --> pmm_init-->page_init-->init_memmap--> pmm_manager->init_memmap
    *              This fun is used to init a free block (with parameter: addr_base, page_number).
    *              First you should init each page (in memlayout.h) in this free block, include:
    *                  p->flags should be set bit PG_property (means this page is valid. In pmm_init fun (in pmm.c),
    *                  the bit PG_reserved is setted in p->flags)
    *                  if this page  is free and is not the first page of free block, p->property should be set to 0.
    *                  if this page  is free and is the first page of free block, p->property should be set to total num of block.
    *                  p->ref should be 0, because now p is free and no reference.
    *                  We can use p->page_link to link this page to free_list, (such as: list_add_before(&free_list, &(p->page_link)); )
    *              Finally, we should sum the number of free mem block: nr_free+=n
    * （3）default_init_memmap：
    *      CALL GRAPH: kern_init --> pmm_init-->page_init-->init_memmap--> pmm_manager->init_memmap
    * default_init_memmap被用于初始化一个空闲块（参数：addr_base, page_number）。
    * 首先你应该初始化空闲块的每一页（在memlayout.h），包括
    *  p->flags应该被设置PG_property位（意思是这个页是有效的，在pmm.c的pmm_init函数）
    * 如果这个页是空闲的，并且不是该空闲块的第一个页，p->property应该被设为0.
    * 如果这个页是空闲的，并且是该空闲块的第一个页， p->property应该被设置为块的总数。
    * p->ref应该被设置为0，因为现在p是空闲的并且没有任何引用。
    * 我们可以使用p->page_link将页连接到free_list（例如：list_add_before(&free_list, &(p->page_link))）。
    * 最后，我们应该算空闲内存块总数：nr_free+=n
    *
    * (4) default_alloc_pages: search find a first free block (block size >=n) in free list and reszie the free block, return the addr
    *              of malloced block.
    *              (4.1) So you should search freelist like this:
    *                       list_entry_t le = &free_list;
    *                       while((le=list_next(le)) != &free_list) {
    *                       ....
    *                 (4.1.1) In while loop, get the struct page and check the p->property (record the num of free block) >=n?
    *                       struct Page *p = le2page(le, page_link);
    *                       if(p->property >= n){ ...
    *                 (4.1.2) If we find this p, then it' means we find a free block(block size >=n), and the first n pages can be malloced.
    *                     Some flag bits of this page should be setted: PG_reserved =1, PG_property =0
    *                     unlink the pages from free_list
    *                     (4.1.2.1) If (p->property >n), we should re-caluclate number of the the rest of this free block,
    *                           (such as: le2page(le,page_link))->property = p->property - n;)
    *                 (4.1.3)  re-caluclate nr_free (number of the the rest of all free block)
    *                 (4.1.4)  return p
    *               (4.2) If we can not find a free block (block size >=n), then return NULL
    * （4）default_alloc_pages：找寻空闲列表中的第一个空闲块（块大小>=n），并且重新设置空闲块，返回新分配空闲块的地址。
    * （4.1）所以你应该这样搜索空闲列表： list_entry_t le = &free_list;
    *                       while((le=list_next(le)) != &free_list)
    * （4.1.1）在这个while循环中，获取page结构并且检查p->property>=n？（记录空闲块的数量）
    *              struct Page *p = le2page(le, page_link);
    *                       if(p->property >= n)
    * （4.1.2）如果我们找到这个p，意味着我们找到了一个块大小大于n的空闲块，并且前n个页可以被分配。
    *           这个页的一些标志位应该被设定： PG_reserved =1, PG_property =0。取消free_list的连接。
    *   （4.1.2.1）如果p->property >n，我们应该重新计算这个空闲块的剩余数量（例如：le2page(le,page_link))->property = p->property - n）
    * （4.1.3）重新计算nr_free（所有剩余空闲块的数量）
    * （4.1.4）返回p
    * （4.2）如果我们不能找到一个块大小大于n的空闲块，返回NULL
    *
    * (5) default_free_pages: relink the pages into  free list, maybe merge small free blocks into big free blocks.
    *               (5.1) according the base addr of withdrawed blocks, search free list, find the correct position
    *                     (from low to high addr), and insert the pages. (may use list_next, le2page, list_add_before)
    *               (5.2) reset the fields of pages, such as p->ref, p->flags (PageProperty)
    *               (5.3) try to merge low addr or high addr blocks. Notice: should change some pages's p->property correctly.
    * （5）default_free_pages：重新连接页到空闲列表，也许可以合并小的空闲块为大的空闲块。
    * （5.1）通过提取块的基本地址，搜索空闲列表，找到正确位置（从低到高地址），并且插入页（可以使用list_next, le2page, list_add_before）
    * （5.2）重新设置页的字段，例如p->ref, p->flags
    * （5.3）尝试合并低地址或低地址块。注意：应该正确改变一些页的p->property
    *
    */

free_area_t free_area1;

#define free_list (free_area1.free_list)
#define nr_free (free_area1.nr_free)

static void
default_init(void) {
    list_init(&free_list);
    nr_free = 0;
}
//初始化空闲块列表，nr_free（记录空闲块数量）设置为0

//初始化空闲块的每一页，设置状态，连接到空闲列表。p->flags应该被设置PG_property位（意思是这个页是有效
static void
default_init_memmap(struct Page* base, size_t n) {
    assert(n > 0);
    struct Page* p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
    //如果空闲链表空，base加到链表里
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    }
    else {//非空，按照地址低→高，插入base
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            }
            else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }
}

//查找空闲列表中第一个块大小大于请求的空闲块，并调整其大小，返回已分配地址
static struct Page*
default_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {//请求页面>剩余空闲页面
        return NULL;
    }
    struct Page* page = NULL;
    list_entry_t* le = &free_list;
    while ((le = list_next(le)) != &free_list) {//遍历链表
        struct Page* p = le2page(le, page_link);
        if (p->property >= n) {//块的空闲页>=n
            page = p;
            break;
        }
    }
    if (page != NULL) {
        list_entry_t* prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));
        if (page->property > n) {//page的剩余页大于需要页，裁剪，重新计算，剩余的加到空闲列表
            struct Page* p = page + n;
            p->property = page->property - n;
            SetPageProperty(p);
            list_add(prev, &(p->page_link));
        }
        nr_free -= n;//更新空闲块数量
        ClearPageProperty(page);
    }
    return page;
}


//页面重新连接到空闲链表，合并小空闲块为大空闲块
static void
default_free_pages(struct Page* base, size_t n) {
    assert(n > 0);
    struct Page* p = base;
    for (; p != base + n; p++) {//遍历页
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;//更新空闲块数量

    if (list_empty(&free_list)) {//空闲列表空，直接赋值base
        list_add(&free_list, &(base->page_link));
    }
    else {//按照地址找合适位置
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            }
            else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }

    //与低地址块合并
    list_entry_t* le = list_prev(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);
            list_del(&(base->page_link));
            base = p;
        }
    }
    //与高地址块合并
    le = list_next(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
    }
}

//返回空闲块数量
static size_t
default_nr_free_pages(void) {
    return nr_free;
}

static void
basic_check(void) {
    struct Page* p0, * p1, * p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_list));

    struct Page* p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

// LAB2: below code is used to check the first fit allocation algorithm
// NOTICE: You SHOULD NOT CHANGE basic_check, default_check functions!
static void
default_check(void) {
    int count = 0, total = 0;
    list_entry_t* le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page* p = le2page(le, page_link);
        assert(PageProperty(p));
        count++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

    struct Page* p0 = alloc_pages(5), * p1, * p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    free_pages(p0 + 2, 3);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 2) && p0[2].property == 3);
    assert((p1 = alloc_pages(3)) != NULL);
    assert(alloc_page() == NULL);
    assert(p0 + 2 == p1);

    p2 = p0 + 1;
    free_page(p0);
    free_pages(p1, 3);
    assert(PageProperty(p0) && p0->property == 1);
    assert(PageProperty(p1) && p1->property == 3);

    assert((p0 = alloc_page()) == p2 - 1);
    free_page(p0);
    assert((p0 = alloc_pages(2)) == p2 + 1);

    free_pages(p0, 2);
    free_page(p2);

    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page* p = le2page(le, page_link);
        count--, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
}
//这个结构体在
const struct pmm_manager default_pmm_manager = {
    .name = "default_pmm_manager",
    .init = default_init,
    .init_memmap = default_init_memmap,
    .alloc_pages = default_alloc_pages,
    .free_pages = default_free_pages,
    .nr_free_pages = default_nr_free_pages,
    .check = default_check,
};