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

    size = fixsize(n);
    //初始化二叉树
    //unsigned node_size = 2 * size;
    unsigned node_size = size;
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


static void
buddy_system_check(void) {
    struct Page* p0, * A, * B, * C, * D;
    p0 = A = B = C = D = NULL;
    A = alloc_pages(70);
    B = alloc_pages(35);
    C = alloc_pages(257);
    D = alloc_pages(63);
    cprintf("A分配70，B分配35，C分配257，D分配63\n");
    cprintf("此时A %p\n", A);
    cprintf("此时B %p\n", B);
    cprintf("此时C %p\n", C);
    cprintf("此时D %p\n", D);

    free_pages(B, 35);
    cprintf("B释放35\n");
    free_pages(D, 63);
    cprintf("D释放63\n");
    cprintf("此时BD应该合并\n");
    free_pages(A, 70);
    cprintf("A释放70\n");
    cprintf("此时前512个已空，我们再分配511个的A来测试\n");
    A = alloc_pages(511);
    cprintf("A分配511\n");
    cprintf("此时A %p\n", A);
    free_pages(A, 512);
    cprintf("A释放512\n");

    A = alloc_pages(255);
    B = alloc_pages(255);
    cprintf("A分配255，B分配255\n");
    cprintf("此时A %p\n", A);
    cprintf("此时B %p\n", B);
    free_pages(C, 257);
    free_pages(A, 255);
    free_pages(A, 255);
    cprintf("全部释放\n");
    cprintf("检查完成，没有错误\n");
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
