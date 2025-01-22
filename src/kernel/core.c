#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <test/test.h>
#include <common/buf.h>
#include <common/string.h>
#include <driver/virtio.h>
#include <kernel/paging.h>
#include <kernel/mem.h>

#define INIT_ELR 0x400000

volatile bool panic_flag;
u32 LBA;
void trap_return();
extern char icode[], eicode[];

NO_RETURN void idle_entry()
{
    set_cpu_on();
    // kalloc_test();
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap
        {
            arch_wfi();
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

static Proc *initproc;
extern struct page pages[];
void kernel_entry()
{
    /* LAB 4 TODO 3 BEGIN */
    Buf b;
    b.flags = 0;
    b.block_no = (u32)0x0;
    virtio_blk_rw(&b);
    u8 *data = b.data;
    LBA = *(int *)(data + 0x1CE + 0x8);
    /* LAB 4 TODO 3 END */

    init_filesystem();

    printk("Hello world! (Core %lld)\n", cpuid());

    /**
     * (Final) TODO BEGIN 
     * 
     * Map init.S to user space and trap_return to run icode.
     */
    initproc = create_proc();

    initproc->ucontext->gregs[0] = 0;
    initproc->ucontext->elr = INIT_ELR;
    initproc->ucontext->sp = 0x80000000;
    initproc->ucontext->spsr = 0;

    struct section *st = (struct section *)kalloc(sizeof(struct section));
    st->flags = ST_TEXT;
    st->begin = INIT_ELR;
    st->end = st->begin + (u64)eicode - (u64)icode;
    _insert_into_list(&initproc->pgdir.section_head, &st->stnode);
    void *p = kalloc_page();
    memcpy(p, (void *)icode, PAGE_SIZE);
    vmmap(&initproc->pgdir, INIT_ELR, p, PTE_USER_DATA | PTE_RO);

    start_proc(initproc, trap_return, 0);
    printk("initproc start!\n");
    while (1) {
        int code;
        auto pid = wait(&code);
        (void)pid;
    }
    PANIC();
    /* (Final) TODO END */
}

NO_INLINE NO_RETURN void _panic(const char *file, int line)
{
    printk("=====%s:%d PANIC%lld!=====\n", file, line, cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d. Stopped.\n", file, line);
    arch_stop_cpu();
}