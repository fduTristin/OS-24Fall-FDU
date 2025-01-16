#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/string.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>


void init_sections(ListNode *section_head) {
    /* (Final) TODO BEGIN */
    struct section* heap = (struct section*)kalloc(sizeof(struct section));
    memset(heap, 0, sizeof(struct section));
    heap->begin = heap->end = 0;
    heap->flags = ST_HEAP;
    _insert_into_list(section_head, &heap->stnode);
    /* (Final) TODO END */
}

void free_sections(struct pgdir *pd) {
    /* (Final) TODO BEGIN */
    
    /* (Final) TODO END */
}

u64 sbrk(i64 size) {
    /**
     * (Final) TODO BEGIN 
     * 
     * Increase the heap size of current process by `size`.
     * If `size` is negative, decrease heap size. `size` must
     * be a multiple of PAGE_SIZE.
     * 
     * Return the previous heap_end.
     */

    return 0;
    /* (Final) TODO END */
}

int pgfault_handler(u64 iss) {
    // Proc *p = thisproc();
    // struct pgdir *pd = &p->pgdir;
    // u64 addr = arch_get_far(); // Attempting to access this address caused the page fault

    /** 
     * (Final) TODO BEGIN
     * 
     * 1. Find the section struct which contains the faulting address `addr`.
     * 2. Check section flags to determine page fault type.
     * 3. Handle the page fault accordingly.
     * 4. Return to user code or kill the process.
     */
    return 0;
    /* (Final) TODO END */
}

void copy_sections(ListNode *from_head, ListNode *to_head)
{
    /* (Final) TODO BEGIN */

    /* (Final) TODO END */
}
