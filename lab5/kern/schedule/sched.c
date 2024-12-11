#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE);
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (proc->state != PROC_RUNNABLE) {
            proc->state = PROC_RUNNABLE;
            proc->wait_state = 0;
        }
        else {
            warn("wakeup runnable process.\n");
        }
    }
    local_intr_restore(intr_flag);
}

void
schedule(void) {
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;

    // 禁用中断
    local_intr_save(intr_flag);
    {
        current->need_resched = 0;

        // 如果当前进程是idle，则设置为链表的头部；
        // 否则，设置为链表中的下一个节点位置。
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        do {
            // 遍历链表，找到PROC_RUNNABLE状态的进程
            if ((le = list_next(le)) != &proc_list) {
                next = le2proc(le, list_link);
                if (next->state == PROC_RUNNABLE) {
                    break;
                }
            }
        } while (le != last);

        // 找不到
        if (next == NULL || next->state != PROC_RUNNABLE) {
            next = idleproc;
        }
        next->runs ++;

        // 切换进程
        if (next != current) 
        {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}

