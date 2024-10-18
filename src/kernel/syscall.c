#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>
#include <test/test.h>
#include <aarch64/intrinsic.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"

void *syscall_table[NR_SYSCALL] = {
    [0 ... NR_SYSCALL - 1] = NULL,
    [SYS_myreport] = (void *)syscall_myreport,
};

void syscall_entry(UserContext *context)
{
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    // be sure to check the range of id. if id >= NR_SYSCALL, panic.
    u64 id = context->gregs[8];
    u64 x[5];
    for (int i = 0; i != 5; i++) {
        x[i] = context->gregs[i];
    }
    if (id >= NR_SYSCALL) {
        PANIC();
    }
    context->gregs[0] =
            ((u64(*)(u64, u64, u64, u64, u64, u64))syscall_table[id])(
                    x[0], x[1], x[2], x[3], x[4], x[5]);
}

#pragma GCC diagnostic pop