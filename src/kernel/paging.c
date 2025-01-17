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

void init_sections(ListNode *section_head)
{
    /* (Final) TODO BEGIN */
    init_list_node(section_head);
    struct section *heap = (struct section *)kalloc(sizeof(struct section));
    memset(heap, 0, sizeof(struct section));
    heap->begin = heap->end = 0;
    heap->flags = ST_HEAP;
    _insert_into_list(section_head, &heap->stnode);
    /* (Final) TODO END */
}

/**
 * free pages referred by a section
*/
void free_pages_of_section(struct pgdir *pd, struct section *sec)
{
    u64 begin = PAGE_BASE(sec->begin);
    u64 end = sec->end;
    for (u64 i = begin; i < end; i += PAGE_SIZE) {
        PTEntriesPtr pte = get_pte(pd, i, false);
        if (pte && (*pte & PTE_VALID)) {
            /**
             * @todo mmap
             */
            kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
            *pte = NULL;
        }
    }
}

void free_sections(struct pgdir *pd)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&pd->lock);
    ListNode *p = pd->section_head.next;
    while (p != &(pd->section_head)) {
        struct section *sec = container_of(p, struct section, stnode);
        free_pages_of_section(pd, sec);
        p = p->next;
        _detach_from_list(&sec->stnode);
        if (sec->fp)
            file_close(sec->fp);
        kfree(sec);
    }
    release_spinlock(&pd->lock);

    /* (Final) TODO END */
}

struct section *find_type_section(struct pgdir *pd, SECTION_TYPE st)
{
    _for_in_list(p, &pd->section_head)
    {
        if (p == &pd->section_head)
            continue;
        struct section *sec = container_of(p, struct section, stnode);
        if (sec->flags == ST_HEAP) {
            return sec;
        }
    }
    return NULL;
}

u64 sbrk(i64 size)
{
    /**
     * (Final) TODO BEGIN 
     * 
     * Increase the heap size of current process by `size`.
     * If `size` is negative, decrease heap size. `size` must
     * be a multiple of PAGE_SIZE.
     * 
     * Return the previous heap_end.
     */
    // printk("sbrk size:%lld\n",size);
    ASSERT(size % PAGE_SIZE == 0);
    Proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    struct section *sec;

    acquire_spinlock(&pd->lock);

    // get heap section of current process
    ASSERT((sec = find_type_section(pd, ST_HEAP)));

    if (size == 0)
        return sec->end;
    u64 ret = sec->end;
    sec->end += size;
    if (size > 0) {
        ASSERT(sec->end > ret);
    } else {
        ASSERT(sec->end < ret);
        // free pages if heap size shrinks
        // printk("checking heap is aligned to page or not...\nbegin: %llu, end: %llu\n",
        //        sec->begin, sec->end);
        for (u64 i = sec->end; i < ret; i += PAGE_SIZE) {
            PTEntriesPtr pte = get_pte(pd, i, false);
            if (pte && *pte & PTE_VALID) {
                kfree_page((void *)P2K(PTE_ADDRESS(*pte)));
                *pte = 0;
            }
        }
    }
    release_spinlock(&pd->lock);
    return ret;
    /* (Final) TODO END */
}

int pgfault_handler(u64 iss)
{
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
    _for_in_list(node, from_head)
    {
        if (node == from_head) {
            break;
        }
        struct section *st = container_of(node, struct section, stnode);
        struct section *new_st = kalloc(sizeof(struct section));
        memmove(new_st, st, sizeof(struct section));
        if (st->fp != NULL) {
            new_st->fp = file_dup(st->fp);
        }
        _insert_into_list(to_head, &(new_st->stnode));
    }
    /* (Final) TODO END */
}
