#include <defs.h>
#include <unistd.h>
#include <stdarg.h>
#include <syscall.h>

#define MAX_ARGS            5

static inline int
syscall(int64_t num, ...)   // num为系统调用编号
{
    va_list ap; // 用于在可变参数函数中 保存参数列表
    va_start(ap, num);  // 初始化参数列表，从num开始
    uint64_t a[MAX_ARGS];   // 存储提取出来的参数
    int i, ret;
    for (i = 0; i < MAX_ARGS; i ++) {
        a[i] = va_arg(ap, uint64_t);
    }
    va_end(ap); // 清理参数列表，结束访问，释放资源

    asm volatile (
        "ld a0, %1\n"   // 将传入的参数加载到寄存器中
        "ld a1, %2\n"
        "ld a2, %3\n"
        "ld a3, %4\n"
        "ld a4, %5\n"
    	"ld a5, %6\n"
        "ecall\n"       // 执行系统调用，从用户态切换到内核态
        "sd a0, %0"     // 系统调用的返回值存在a0，将a0存入ret中
        : "=m" (ret)    // 输出操作数
        : "m"(num), "m"(a[0]), "m"(a[1]), "m"(a[2]), "m"(a[3]), "m"(a[4])   //输入操作数
        :"memory");
    // 上面的代码，num加载到a0寄存器，a[0]加载到a1寄存器……
    return ret;
}

int
sys_exit(int64_t error_code) {
    return syscall(SYS_exit, error_code);
}

int
sys_fork(void) {
    return syscall(SYS_fork);
}

int
sys_wait(int64_t pid, int *store) {
    return syscall(SYS_wait, pid, store);
}

int
sys_yield(void) {
    return syscall(SYS_yield);
}

int
sys_kill(int64_t pid) {
    return syscall(SYS_kill, pid);
}

int
sys_getpid(void) {
    return syscall(SYS_getpid);
}

int
sys_putc(int64_t c) {
    return syscall(SYS_putc, c);
}

int
sys_pgdir(void) {
    return syscall(SYS_pgdir);
}

