#include <pmm.h>
#include <list.h>
#include <string.h>
#include <buddy_system_pmm.h>
#include <default_pmm.h>
#include <stdio.h>


free_area_t free_area2;
#define free_list (free_area2.free_list)
#define nr_free (free_area2.nr_free)

struct Page* buddy_base;//buddy system的起始地址
unsigned* root;
int size;

#define LEFT_LEAF(index) ((index) * 2 + 1)  //左子树
#define RIGHT_LEAF(index) ((index) * 2 + 2) //右子树
#define PARENT(index) ( ((index) + 1) / 2 - 1)  //父节点
#define IS_POWER_OF_2(x) (!((x)&((x)-1)))   //是否为2的x次幂
#define MAX(a, b) ((a) > (b) ? (a) : (b))   //返回较大值

static unsigned fixsize(unsigned size) {    //调整为大于等于size的最接近的2的幂
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
    return size + 1;
}


static void
buddy_system_init(void) {
    list_init(&free_list);
    nr_free = 0;
}

static void
buddy_system_init_memmap(struct Page* base, size_t n) {
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
    buddy_base = base;

    //size = fixsize(n);
    size = 256;
    //初始化二叉树
    unsigned node_size = 2 * size;
    root = (unsigned*)(base + size);
    for (int i = 0; i < 2 * size - 1; ++i)
    {
        if (IS_POWER_OF_2(i + 1))
            node_size /= 2;
        root[i] = node_size;
    }
}

static struct Page*
buddy_system_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    struct Page* page = NULL;
    
    unsigned index = 0;
    unsigned node_size;
    unsigned offset = 0;

    if (n <= 0)
        n = 1;
    else if (!IS_POWER_OF_2(n))
    {
        n = fixsize(n);
    }
    if (root[index] < n)
        offset = -1;

    for (node_size = size; node_size != n; node_size /= 2) {
        if (root[LEFT_LEAF(index)] >= n)
            index = LEFT_LEAF(index);
        else
            index = RIGHT_LEAF(index);
    }
    root[index] = 0;
    offset = (index + 1) * node_size - size;
    while (index) {
        index = PARENT(index);
        root[index] = MAX(root[LEFT_LEAF(index)], root[RIGHT_LEAF(index)]);
    }

    unsigned x = fixsize(n);
    nr_free -= n;

    //开始分配的位置
    page = buddy_base + offset;
    //把分配出去的页面属性清除
    for (struct Page* p = page; p != page + x; p++)
        ClearPageProperty(p);

    page->property = n;


    return page;
}


static void
buddy_system_free_pages(struct Page* base, size_t n) {
    assert(n > 0);
    struct Page* p = base;
    n = fixsize(n);
    for (; p != base + n; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        set_page_ref(p, 0);
    }
    nr_free += n;

    unsigned offset = buddy_base - base;
    unsigned node_size = 1;
    unsigned index = size - offset - 1;
    unsigned left_longest, right_longest;


    for (; root[index]; index = PARENT(index)) {
        node_size *= 2;
        if (index == 0)
            return;
    }

    root[index] = node_size;
    
    while (index) {
        index = PARENT(index);
        node_size *= 2;

        left_longest = root[LEFT_LEAF(index)];
        right_longest = root[RIGHT_LEAF(index)];

        if (left_longest + right_longest == node_size)
            root[index] = node_size;
        else
            root[index] = MAX(left_longest, right_longest);
    }

}

static size_t
buddy_system_nr_free_pages(void) {
    return nr_free;
}

void print_node(int index)
{
    cprintf("node%d: %d\n", index, root[index]);
}

void print_from_to(int start, int end)
{
    for (int i = start; i <= end; i++)
        print_node(i);
    cprintf("\n");
}




static void
buddy_system_check(void) {
    cprintf("Starting buddy system memory checks...\n");

    // 进行内存分配测试
    struct Page* p1 = buddy_system_alloc_pages(3); // 分配3页
    struct Page* p2 = buddy_system_alloc_pages(8); // 分配8页
    struct Page* p3 = buddy_system_alloc_pages(9); // 分配9页

    cprintf("Allocated pages: p1 = %p, p2 = %p, p3 = %p\n", p1, p2, p3);
    
    // 打印当前Buddy树
    cprintf("Current Buddy Tree:\n");
    print_from_to(0, 80); // 从根节点开始打印树

    // 释放之前分配的页面
    if (p1) buddy_system_free_pages(p1, 3);
    if (p2) buddy_system_free_pages(p2, 8);
    if (p3) buddy_system_free_pages(p3, 9);

    // 打印当前Buddy树
    cprintf("Buddy Tree after deallocation:\n");
    print_from_to(0, 80); // 从根节点开始打印树

    // 再次分配页以验证内存状态
    struct Page* p4 = buddy_system_alloc_pages(127); // 分配127页
    cprintf("Allocated pages after deallocation: p4 = %p\n", p4);

    // 打印当前Buddy树
    cprintf("Buddy Tree after deallocation:\n");
    print_from_to(0, 80); // 从根节点开始打印树
}






//这个结构体在
const struct pmm_manager buddy_system_pmm_manager = {
    .name = "buddy_system_pmm_manager",
    .init = buddy_system_init,
    .init_memmap = buddy_system_init_memmap,
    .alloc_pages = buddy_system_alloc_pages,
    .free_pages = buddy_system_free_pages,
    .nr_free_pages = buddy_system_nr_free_pages,
    .check = buddy_system_check,
};
