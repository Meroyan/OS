# Lab5 实验报告

#### 练习0：填写已有实验

**本实验依赖实验2/3/4。请把你做的实验2/3/4的代码填入本实验中代码中有“LAB2”/“LAB3”/“LAB4”的注释相应部分。注意：为了能够正确执行lab5的测试应用程序，可能需对已完成的实验2/3/4的代码进行进一步改进。**'

- 在**alloc_proc**中，将lab4代码按照注释要求修改代码如下所示：

```cpp{.line-numbers}
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
```

- 在**do_fork**中，将lab4代码按照注释要求修改代码如下所示：

```cpp{.line-numbers}
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
        list_add(&proc_list, &(proc->list_link));  // 添加进程到进程列表
        nr_process++;

        // [添加] 设置进程关系的连接
        set_links(proc);
    }
    local_intr_restore(intr_flag);//恢复中断

    wakeup_proc(proc); // 使新进程可运行
    ret = proc->pid; // 设置返回值为新进程的 PID

```

#### 练习1：加载应用程序并执行（需要编码）

**do_execv函数调用load_icode（位于kern/process/proc.c中）来加载并解析一个处于内存中的ELF执行文件格式的应用程序。你需要补充load_icode的第6步，建立相应的用户内存空间来放置应用程序的代码段、数据段等，且要设置好proc_struct结构中的成员变量trapframe中的内容，确保在执行此进程后，能够从应用程序设定的起始执行地址开始执行。需设置正确的trapframe内容。**

**请在实验报告中简要说明你的设计实现过程。**

- **请简要描述这个用户态进程被ucore选择占用CPU执行（RUNNING态）到具体执行应用程序第一条指令的整个经过。**



#### 练习2：父进程复制自己的内存空间给子进程（需要编码）

**创建子进程的函数do_fork在执行中将拷贝当前进程（即父进程）的用户内存地址空间中的合法内容到新进程中（子进程），完成内存资源的复制。具体是通过copy_range函数（位于kern/mm/pmm.c中）实现的，请补充copy_range的实现，确保能够正确执行。**

**请在实验报告中简要说明你的设计实现过程。**

- **如何设计实现Copy on Write机制？给出概要设计，鼓励给出详细设计。**

>Copy-on-write（简称COW）的基本概念是指如果有多个使用者对一个资源A（比如内存块）进行读操作，则每个使用者只需获得一个指向同一个资源A的指针，就可以该资源了。若某使用者需要对这个资源A进行写操作，系统会对该资源进行拷贝操作，从而使得该“写操作”使用者获得一个该资源A的“私有”拷贝—资源B，可对资源B进行写操作。该“写操作”使用者对资源B的改变对于其他的使用者而言是不可见的，因为其他使用者看到的还是资源A。



#### 练习3： 阅读分析源代码，理解进程执行 fork/exec/wait/exit 的实现，以及系统调用的实现（不需要编码）

**请在实验报告中简要说明你对 fork/exec/wait/exit函数的分析。并回答如下问题：**

- **请分析fork/exec/wait/exit的执行流程。重点关注哪些操作是在用户态完成，哪些是在内核态完成？内核态与用户态程序是如何交错执行的？内核态执行结果是如何返回给用户程序的？**



- **请给出ucore中一个用户态进程的执行状态生命周期图（包执行状态，执行状态之间的变换关系，以及产生变换的事件或函数调用）。（字符方式画即可）**

**执行：make grade。如果所显示的应用程序检测都输出ok，则基本正确。（使用的是qemu-1.0.1）**



#### 扩展练习 Challenge：

**1. 实现 Copy on Write （COW）机制**

**给出实现源码,测试用例和设计报告（包括在cow情况下的各种状态转换（类似有限状态自动机）的说明）。**

**这个扩展练习涉及到本实验和上一个实验“虚拟内存管理”。在ucore操作系统中，当一个用户父进程创建自己的子进程时，父进程会把其申请的用户空间设置为只读，子进程可共享父进程占用的用户内存空间中的页面（这就是一个共享的资源）。当其中任何一个进程修改此用户内存空间中的某页面时，ucore会通过page fault异常获知该操作，并完成拷贝内存页面，使得两个进程都有各自的内存页面。这样一个进程所做的修改不会被另外一个进程可见了。请在ucore中实现这样的COW机制。**

**由于COW实现比较复杂，容易引入bug，请参考 https://dirtycow.ninja/ 看看能否在ucore的COW实现中模拟这个错误和解决方案。需要有解释。**

**这是一个big challenge.**


**2. 说明该用户程序是何时被预先加载到内存中的？与我们常用操作系统的加载有何区别，原因是什么？**