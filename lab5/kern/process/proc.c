#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc, 
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:
                                            
  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+ 
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  + 
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep 
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit   
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT          10
#define HASH_LIST_SIZE      (1 << HASH_SHIFT)
#define pid_hashfn(x)       (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// alloc_proc - alloc a proc_struct and init all fields of proc_struct
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
    //LAB4:EXERCISE1 2213870
    /*
     * below fields in proc_struct need to be initialized
     *       enum proc_state state;                      // Process state
     *       int pid;                                    // Process ID
     *       int runs;                                   // the running times of Proces
     *       uintptr_t kstack;                           // Process kernel stack
     *       volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
     *       struct proc_struct *parent;                 // the parent process
     *       struct mm_struct *mm;                       // Process's memory management field
     *       struct context context;                     // Switch here to run process
     *       struct trapframe *tf;                       // Trap frame for current interrupt
     *       uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
     *       uint32_t flags;                             // Process flag
     *       char name[PROC_NAME_LEN + 1];               // Process name
     */

     //LAB5 2211489 : (update LAB4 steps)
     /*
     * below fields(add in LAB5) in proc_struct need to be initialized  
     *       uint32_t wait_state;                        // waiting state
     *       struct proc_struct *cptr, *yptr, *optr;     // relations between processes
     */

        // 初始化进程结构体的各个字段
        proc->state = PROC_UNINIT;            // 初始状态为未初始化
        proc->pid = -1;                       // PID 未分配
        proc->runs = 0;                       // 运行次数为 0
        proc->kstack = 0;                     // 内核栈地址初始化为 0
        proc->need_resched = 0;           // 初始不需要重新调度
        proc->parent = NULL;                  // 父进程指针初始化为 NULL
        proc->mm = NULL;                      // 内存管理结构体指针初始化为 NULL

        // 初始化 context 字段
        memset(&proc->context, 0, sizeof(struct context));

        proc->tf = NULL;                      // 陷阱帧初始化为 NULL
        proc->cr3 = boot_cr3;                 // CR3 寄存器值初始化
        proc->flags = 0;                      // 进程标志初始化为 0

        // 初始化进程名称为一个空字符串
        memset(proc->name, 0, PROC_NAME_LEN + 1);

        // [新增]
        proc->wait_state = 0;
        proc->cptr = NULL;      // Child Pointer 表示当前进程的子进程
        proc->optr = NULL;      // Older Sibling Pointer 表示当前进程的上一个兄弟进程
        proc->yptr = NULL;      // Younger Sibling Pointer 表示当前进程的下一个兄弟进程

    }
    return proc;
}

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name) {
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc) {
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc) {
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL) {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process ++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc) {
    list_del(&(proc->list_link));
    if (proc->optr != NULL) {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL) {
        proc->yptr->optr = proc->optr;
    }
    else {
       proc->parent->cptr = proc->optr;
    }
    nr_process --;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++ last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) {
                if (++ last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}

// proc_run - make process "proc" running on cpu
// NOTE: before call switch_to, should load  base addr of "proc"'s new PDT
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        // LAB4:EXERCISE3 2213870
        /*
        * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
        * MACROs or Functions:
        *   local_intr_save():        Disable interrupts
        *   local_intr_restore():     Enable Interrupts
        *   lcr3():                   Modify the value of CR3 register
        *   switch_to():              Context switching between two processes
        */

        bool intr_flag; // 用于保存中断状态
        struct proc_struct* prev = current, * next = proc;
        // 禁用中断
        local_intr_save(intr_flag);
        {
            current = proc;

            // 加载新进程的页目录表基地址
            lcr3(next->cr3);

            // 进行上下文切换
            switch_to(&(prev->context), &(next->context));
        }
        // 启用中断
        local_intr_restore(intr_flag);

    }
}

// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void) {
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc) {
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc) {
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid) {
    if (0 < pid && pid < MAX_PID) {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list) {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid) {
                return proc;
            }
        }
    }
    return NULL;
}

// kernel_thread - create a kernel thread using "fn" function
// NOTE: the contents of temp trapframe tf will be copied to 
//       proc->tf in do_fork-->copy_thread function
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc) {
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL) {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc) {
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm) {
    struct Page *page;
    if ((page = alloc_page()) == NULL) {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm) {
    free_page(kva2page(mm->pgdir));
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc) {
    struct mm_struct *mm, *oldmm = current->mm;

    /* current is a kernel thread */
    if (oldmm == NULL) {
        return 0;
    }
    if (clone_flags & CLONE_VM) {
        mm = oldmm;
        goto good_mm;
    }
    int ret = -E_NO_MEM;
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }
    lock_mm(oldmm);
    {
        ret = dup_mmap(mm, oldmm);
    }
    unlock_mm(oldmm);

    if (ret != 0) {
        goto bad_dup_cleanup_mmap;
    }

good_mm:
    mm_count_inc(mm);
    proc->mm = mm;
    proc->cr3 = PADDR(mm->pgdir);
    return 0;
bad_dup_cleanup_mmap:
    exit_mmap(mm);
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    return ret;
}

// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - setup the kernel entry point and stack of process
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;

    // Set a0 to 0 so a child process knows it's just forked
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)(proc->tf);
}

/* do_fork -     parent process for a new child process
 * @clone_flags: used to guide how to clone the child process
 * @stack:       the parent's user stack pointer. if stack==0, It means to fork a kernel thread.
 * @tf:          the trapframe info, which will be copied to child process's proc->tf
 */
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS) {
        goto fork_out;
    }
    ret = -E_NO_MEM;
    //LAB4:EXERCISE2 2213870
    /*
     * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   alloc_proc:   create a proc struct and init fields (lab4:exercise1)
     *                 创建一个进程结构，并初始化字段
     *   setup_kstack: alloc pages with size KSTACKPAGE as process kernel stack
     *                 分配大小为KSTACKPAGE的页，作为进程内核栈
     *   copy_mm:      process "proc" duplicate OR share process "current"'s mm according clone_flags
     *                 if clone_flags & CLONE_VM, then "share" ; else "duplicate"
     *                 根据clone_flags，进程复制或者共享当前进程的mm，
     *                 如果clone_flags & CLONE_VM，则共享；否则复制
     *   copy_thread:  setup the trapframe on the  process's kernel stack top and
     *                 setup the kernel entry point and stack of process
     *                 设置中断帧在进程的内核栈顶，设置进程的内核入口点和栈
     *   hash_proc:    add proc into proc hash_list
     *                 把进程加到哈希列表中
     *   get_pid:      alloc a unique pid for process
     *                 分配一个独一无二的pid
     *   wakeup_proc:  set proc->state = PROC_RUNNABLE
     *                 设置进程状态为PROC_RUNNABLE
     * VARIABLES:
     *   proc_list:    the process set's list
     *   nr_process:   the number of process set
     */

    //    1. call alloc_proc to allocate a proc_struct
    //    2. call setup_kstack to allocate a kernel stack for child process
    //    3. call copy_mm to dup OR share mm according clone_flag
    //    4. call copy_thread to setup tf & context in proc_struct
    //    5. insert proc_struct into hash_list && proc_list
    //    6. call wakeup_proc to make the new child process RUNNABLE
    //    7. set ret vaule using child proc's pid

    //LAB5 2211489 : (update LAB4 steps)
    //TIPS: you should modify your written code in lab4(step1 and step5), not add more code.
   /* Some Functions
    *    set_links:  set the relation links of process.  ALSO SEE: remove_links:  lean the relation links of process 
    *    -------------------
    *    update step 1: set child proc's parent to current process, make sure current process's wait_state is 0
    *    update step 5: insert proc_struct into hash_list && proc_list, set the relation links of process
    *    更新第一步：设置子进程的父进程为当前进程，确保当前进程的wait_state = 0
    *    更新第五步：将proc_struct插入到哈希列表和进程列表中，设置进程关系的连接
    */

    // 分配进程结构体
    proc = alloc_proc();
    if (proc == NULL) {
        goto fork_out; // 分配进程结构失败
    }

    // [添加] 设置子进程的父进程为当前进程，确保当前进程的wait_state = 0
    proc->parent = current;
    assert(current->wait_state == 0);

    // 分配内核栈
    if (setup_kstack(proc) != 0) {
        goto bad_fork_cleanup_proc; // 分配内核栈失败
    }

    // 复制父进程的内存管理信息
    if (copy_mm(clone_flags, proc) != 0) {
        goto bad_fork_cleanup_proc;
    };

    // 设置新进程的中断帧和上下文
    copy_thread(proc, stack, tf);

    bool intr_flag;
    local_intr_save(intr_flag);//屏蔽中断，intr_flag置为1
    {
        proc->pid = get_pid();//获取当前进程PID
        hash_proc(proc); // 添加进程到哈希列表
        //list_add(&proc_list, &(proc->list_link));  // 添加进程到进程列表
        //nr_process++;

        // [添加] 设置进程关系的连接
        set_links(proc);
    }

    local_intr_restore(intr_flag);//恢复中断

    wakeup_proc(proc); // 使新进程可运行
    ret = proc->pid; // 设置返回值为新进程的 PID



fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}

// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int
do_exit(int error_code) {
    // 检查当前进程是否为idle或init，如果是，发出panic
    if (current == idleproc) {
        panic("idleproc exit.\n");
    }
    if (current == initproc) {
        panic("initproc exit.\n");
    }

    // 获取当前进程的内存管理结构mm
    struct mm_struct *mm = current->mm;

    // 如果mm不为空，说明是用户进程
    if (mm != NULL) 
    {
        // 切换到内核页表，确保接下来的操作在内核空间执行
        lcr3(boot_cr3);

        // 如果mm引用计数减到0，说明没有其他进程共享此mm
        if (mm_count_dec(mm) == 0) 
        {
            // 释放用户虚拟内存空间相关的资源
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        // 将当前进程的mm设置为NULL，表示资源已经释放
        current->mm = NULL;
    }

    // 设置进程状态为PROC_ZOMBIE，表示进程已退出
    current->state = PROC_ZOMBIE;
    current->exit_code = error_code;
    bool intr_flag;
    struct proc_struct *proc;

    // 关中断
    local_intr_save(intr_flag);
    {
        // 获取当前进程的父进程
        proc = current->parent;

        // 如果父进程处于等待子进程状态，则唤醒父进程
        if (proc->wait_state == WT_CHILD) {
            wakeup_proc(proc);
        }

        // 遍历当前进程的所有子进程
        while (current->cptr != NULL) 
        {
            proc = current->cptr;
            current->cptr = proc->optr;

            // 设置子进程的父进程为initproc，并加入initproc的子进程链表
            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL) {
                initproc->cptr->yptr = proc;
            }

            // 如果子进程也处于退出状态，唤醒initproc
            proc->parent = initproc;
            initproc->cptr = proc;
            if (proc->state == PROC_ZOMBIE) {
                if (initproc->wait_state == WT_CHILD) {
                    wakeup_proc(initproc);
                }
            }
        }
    }

    // 开中断
    local_intr_restore(intr_flag);

    // 调用调度器，选择新的进程执行
    schedule();

    // 如果执行到这里，表示代码执行出现错误，发出panic
    panic("do_exit will not return!! %d.\n", current->pid);
}

/* load_icode - load the content of binary program(ELF format) as the new content of current process
 * 加载二进制程序的内容（ELF格式）作为当前程序的新内容
 * @binary:  the memory addr of the content of binary program
 * @size:  the size of the content of binary program
 */
static int
load_icode(unsigned char *binary, size_t size) {
    // 检查当前进程的内存管理是否为空
    if (current->mm != NULL) {
        panic("load_icode: current->mm must be empty.\n");
    }

    int ret = -E_NO_MEM;
    struct mm_struct *mm;
    //(1) create a new mm for current process
    //(1) 给当前进程创建一个新的mm
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    //(2) create a new PDT, and mm->pgdir= kernel virtual addr of PDT
    //(2) 创建一个新的页目录PDT，并且设置
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }
    //(3) copy TEXT/DATA section, build BSS parts in binary to memory space of process
    //(3) 复制 TEXT/DATA段，在进程的存储空间以二进制构建BSS部分
    struct Page *page;
    //(3.1) get the file header of the bianry program (ELF format)
    //(3.1) 获取二进制文件头（ELF格式）
    struct elfhdr *elf = (struct elfhdr *)binary;    // elf指向ELF文件头
    //(3.2) get the entry of the program section headers of the bianry program (ELF format)
    //(3.2) 获取程序头的条目的二进制程序？
    struct proghdr *ph = (struct proghdr *)(binary + elf->e_phoff); // ph指向程序头
    //(3.3) This program is valid?
    //(3.3) 检查ELF是否有效
    if (elf->e_magic != ELF_MAGIC) {
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir;
    }

    uint32_t vm_flags, perm;
    struct proghdr *ph_end = ph + elf->e_phnum;
    for (; ph < ph_end; ph ++) {
    //(3.4) find every program section headers
    //(3.4) 遍历所有程序头
        // 如果程序头的类型是ELF_PT_LOAD，则需要加载到内存中
        if (ph->p_type != ELF_PT_LOAD) {
            continue ;
        }
        if (ph->p_filesz > ph->p_memsz) {
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }
        if (ph->p_filesz == 0) {
            // continue ;
        }

    //(3.5) call mm_map fun to setup the new vma ( ph->p_va, ph->p_memsz)
        // 设置新的虚拟内存映射vma
        vm_flags = 0, perm = PTE_U | PTE_V;
        if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC;    // 可执行
        if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE;   // 可写
        if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ;    // 可读
        // modify the perm bits here for RISC-V
        if (vm_flags & VM_READ) perm |= PTE_R;
        if (vm_flags & VM_WRITE) perm |= (PTE_W | PTE_R);
        if (vm_flags & VM_EXEC) perm |= PTE_X;

        // 调用mm_map将虚拟地址映射到进程的内存空间
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) {
            goto bad_cleanup_mmap;
        }
        unsigned char *from = binary + ph->p_offset;
        size_t off, size;
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE);

        ret = -E_NO_MEM;

     //(3.6) alloc memory, and  copy the contents of every program section (from, from+end) to process's memory (la, la+end)
     //(3.6) 分配内存，拷贝每个程序段的内容到进程的内存中（从la到la+end）
        end = ph->p_va + ph->p_filesz;
     //(3.6.1) copy TEXT/DATA section of bianry program
     //(3.6.1) 复制TEXT/DATA段
        while (start < end) {
            // 分配物理页面，映射到虚拟地址la
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap;
            }
            // 偏移量=虚拟页起始地址-数据起始位置，size为当前页面剩余可用大小
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            // 拷贝
            memcpy(page2kva(page) + off, from, size);
            start += size, from += size;
        }

     //(3.6.2) build BSS section of binary program
     //(3.6.2) 构建BSS段
        // 程序段结束位置=虚拟地址+内存大小
        end = ph->p_va + ph->p_memsz;
        if (start < la) {
            /* ph->p_memsz == ph->p_filesz */
            if (start == end) {
                continue ;
            }
            off = start + PGSIZE - la, size = PGSIZE - off;
            if (end < la) {
                size -= la - end;
            }
            // 清零
            memset(page2kva(page) + off, 0, size);
            start += size;
            assert((end < la && start == end) || (end >= la && start == la));
        }
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
        }
    }
    //(4) build user stack memory
    //(4) 构建用户栈内存
    vm_flags = VM_READ | VM_WRITE | VM_STACK;
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
        goto bad_cleanup_mmap;
    }
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-2*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-3*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-4*PGSIZE , PTE_USER) != NULL);
    
    //(5) set current process's mm, sr3, and set CR3 reg = physical addr of Page Directory
    //(5) 设置现在进程的mm，sr3，设置cr3为物理页目录地址
    mm_count_inc(mm);
    current->mm = mm;
    current->cr3 = PADDR(mm->pgdir);
    lcr3(PADDR(mm->pgdir));

    //(6) setup trapframe for user environment
    //(6) 设置用户环境的中断帧
    struct trapframe *tf = current->tf;
    // Keep sstatus
    uintptr_t sstatus = tf->status;
    memset(tf, 0, sizeof(struct trapframe));

    /* LAB5:EXERCISE1 2211489
     * should set tf->gpr.sp, tf->epc, tf->status
     * NOTICE: If we set trapframe correctly, then the user level process can return to USER MODE from kernel. So
     *          tf->gpr.sp should be user stack top (the value of sp)
     *          tf->epc should be entry point of user program (the value of sepc)
     *          tf->status should be appropriate for user program (the value of sstatus)
     *          hint: check meaning of SPP, SPIE in SSTATUS, use them by SSTATUS_SPP, SSTATUS_SPIE(defined in risv.h)
     * 如果我们正确设置了中断帧，那么用户级进程可以从内核返回到用户模式。
     *          tf->gpr.sp 应该是用户栈顶（sp 的值）
     *          tf->epc 应该是用户程序的入口点（sepc 的值）
     *          tf->status 应该适合用户程序（sstatus 的值）
     *          提示：检查 SSTATUS 中 SPP 和 SPIE 的含义，使用 SSTATUS_SPP 和 SSTATUS_SPIE（在 riscv.h 中定义）来设置它们
     *      SPP表示之前在的特权级，0为用户态，1为内核态
     *      SPIE为中断发生时，是否自动恢复到内核模式的中断使能，0为禁用中断，1为启用中断
     */
    tf->gpr.sp = USTACKTOP;     // 用户态栈顶指针
    tf->epc = elf->e_entry;     // 应用程序入口点
    tf->status = sstatus & ~(SSTATUS_SPP | SSTATUS_SPIE);

    ret = 0;
out:
    return ret;
bad_cleanup_mmap:
    exit_mmap(mm);
bad_elf_cleanup_pgdir:
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    goto out;
}

// do_execve - call exit_mmap(mm)&put_pgdir(mm) to reclaim memory space of current process
//           - call load_icode to setup new memory space accroding binary prog.
// 回收内存，设置新的内存空间
int
do_execve(const char *name, size_t len, unsigned char *binary, size_t size) {
    struct mm_struct *mm = current->mm;     // 获取当前进程的内存管理结构体
    // 检查name的内存空间能否被访问
    if (!user_mem_check(mm, (uintptr_t)name, len, 0)) {
        return -E_INVAL;
    }

    // 处理程序名称长度
    if (len > PROC_NAME_LEN) {
        len = PROC_NAME_LEN;
    }

    // 复制程序名称
    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
    memcpy(local_name, name, len);

    // 如果当前进程有虚拟内存映射
    if (mm != NULL) {
        cputs("mm != NULL");
        lcr3(boot_cr3);         // 设置页目录指针寄存器cr3
        if (mm_count_dec(mm) == 0)
        {
            exit_mmap(mm);      // 释放内存映射
            put_pgdir(mm);      // 释放页目录相关资源
            mm_destroy(mm);     // 释放内存管理资源，之后重新分配
        }
        current->mm = NULL;
    }

    int ret;
    // 加载新程序的二进制代码失败
    if ((ret = load_icode(binary, size)) != 0) {
        goto execve_exit;
    }

    // 给新程序命名
    set_proc_name(current, local_name);
    // 如果set_proc_name的实现不变, 为什么不能直接set_proc_name(current, name)?
    // 程序名称有长度限制，可能溢出

    return 0;

execve_exit:
    do_exit(ret);
    panic("already exit: %e.\n", ret);
}

// do_yield - ask the scheduler to reschedule
int
do_yield(void) {
    current->need_resched = 1;
    return 0;
}

// do_wait - wait one OR any children with PROC_ZOMBIE state, and free memory space of kernel stack
//         - proc struct of this child.
// NOTE: only after do_wait function, all resources of the child proces are free.
int
do_wait(int pid, int *code_store) {
    struct mm_struct *mm = current->mm;

    // 检查当前进程的内存地址空间是否有效
    if (code_store != NULL) {
        if (!user_mem_check(mm, (uintptr_t)code_store, sizeof(int), 1)) {
            return -E_INVAL;
        }
    }

    struct proc_struct *proc;
    bool intr_flag, haskid;

    // 查找并等待指定的子进程
repeat:
    haskid = 0;
    if (pid != 0) {
        proc = find_proc(pid);  // 查找pid对应的进程
        if (proc != NULL && proc->parent == current) {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {
                goto found;
            }
        }
    }
    // pid==0，查找当前进程的所有直接子进程
    else {
        proc = current->cptr;
        for (; proc != NULL; proc = proc->optr) {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {
                goto found;
            }
        }
    }

    // 没有找到期待的进程，进入睡眠状态
    if (haskid) {
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        schedule();
        if (current->flags & PF_EXITING) {
            do_exit(-E_KILLED);
        }
        goto repeat;
    }
    return -E_BAD_PROC;

found:
    // 系统进程无法退出
    if (proc == idleproc || proc == initproc) {
        panic("wait idleproc or initproc.\n");
    }
    if (code_store != NULL) {
        *code_store = proc->exit_code;
    }

    // 禁用中断
    local_intr_save(intr_flag);
    {
        unhash_proc(proc);  // 从哈希表中删除子进程条目
        remove_links(proc); // 移除子进程在进程链表中的连接
    }
    local_intr_restore(intr_flag);  // 恢复中断
    put_kstack(proc);       // 清理栈
    kfree(proc);            // 释放内存
    return 0;
}

// do_kill - kill process with pid by set this process's flags with PF_EXITING
int
do_kill(int pid) {
    struct proc_struct *proc;
    if ((proc = find_proc(pid)) != NULL) {
        if (!(proc->flags & PF_EXITING)) {
            proc->flags |= PF_EXITING;
            if (proc->wait_state & WT_INTERRUPTED) {
                wakeup_proc(proc);
            }
            return 0;
        }
        return -E_KILLED;
    }
    return -E_INVAL;
}

// kernel_execve - do SYS_exec syscall to exec a user program called by user_main kernel_thread
static int
kernel_execve(const char *name, unsigned char *binary, size_t size) {
    int64_t ret=0, len = strlen(name);
 //   ret = do_execve(name, len, binary, size);
    asm volatile(
        "li a0, %1\n"   // SYS_exec存到a0
        "lw a1, %2\n"
        "lw a2, %3\n"
        "lw a3, %4\n"
        "lw a4, %5\n"
    	"li a7, 10\n"
        "ebreak\n"
        "sw a0, %0\n"
        : "=m"(ret)
        : "i"(SYS_exec), "m"(name), "m"(len), "m"(binary), "m"(size)
        : "memory");
    cprintf("ret = %d\n", ret);
    return ret;
}

#define __KERNEL_EXECVE(name, binary, size) ({                          \
            cprintf("kernel_execve: pid = %d, name = \"%s\".\n",        \
                    current->pid, name);                                \
            kernel_execve(name, binary, (size_t)(size));                \
        })

#define KERNEL_EXECVE(x) ({                                             \
            extern unsigned char _binary_obj___user_##x##_out_start[],  \
                _binary_obj___user_##x##_out_size[];                    \
            __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,     \
                            _binary_obj___user_##x##_out_size);         \
        })

#define __KERNEL_EXECVE2(x, xstart, xsize) ({                           \
            extern unsigned char xstart[], xsize[];                     \
            __KERNEL_EXECVE(#x, xstart, (size_t)xsize);                 \
        })

#define KERNEL_EXECVE2(x, xstart, xsize)        __KERNEL_EXECVE2(x, xstart, xsize)

// user_main - kernel thread used to exec a user program
static int
user_main(void *arg) {
#ifdef TEST
    KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    KERNEL_EXECVE(exit);
#endif
    panic("user_main execve failed.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
// 创建用户线程
static int
init_main(void *arg) {
    size_t nr_free_pages_store = nr_free_pages();   // 可用空闲内存页面
    size_t kernel_allocated_store = kallocated();   // 内核已分配内存数量

    // 创建用户线程
    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0) {
        panic("create user_main failed.\n");
    }

    // 系统调用
    while (do_wait(0, NULL) == 0) {
        schedule();
    }

    cprintf("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL && initproc->yptr == NULL && initproc->optr == NULL);
    assert(nr_process == 2);
    assert(list_next(&proc_list) == &(initproc->list_link));
    assert(list_prev(&proc_list) == &(initproc->list_link));

    cprintf("init check memory pass.\n");
    return 0;
}

// proc_init - set up the first kernel thread idleproc "idle" by itself and 
//           - create the second kernel thread init_main
void
proc_init(void) {
    int i;

    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL) {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process ++;

    current = idleproc;

    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0) {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

// cpu_idle - at the end of kern_init, the first kernel thread idleproc will do below works
void
cpu_idle(void) {
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}

