#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/pt.h>
#include <kernel/printk.h>
#include <kernel/paging.h>

/**
    @brief Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    @note If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    @note THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
 */
PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc)
{
    PTEntriesPtr pt0 = pgdir->pt;
    if (pt0 == NULL) {
        if (!alloc) {
            return NULL;
        }
        pt0 = kalloc_page();
        memset(pt0, 0, PAGE_SIZE);
        pgdir->pt = pt0;
    }
    PTEntriesPtr pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[VA_PART0(va)]));
    if (!(pt0[VA_PART0(va)] & PTE_VALID)) {
        if (!alloc) {
            return NULL;
        }
        pt1 = kalloc_page();
        memset((void *)pt1, 0, PAGE_SIZE);
        pt0[VA_PART0(va)] = K2P(pt1) | PTE_TABLE;
    }
    PTEntriesPtr pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[VA_PART1(va)]));
    if (!(pt1[VA_PART1(va)] & PTE_VALID)) {
        if (!alloc) {
            return NULL;
        }
        pt2 = kalloc_page();
        memset((void *)pt2, 0, PAGE_SIZE);
        pt1[VA_PART1(va)] = K2P(pt2) | PTE_TABLE;
    }
    PTEntriesPtr pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt2[VA_PART2(va)]));
    if (!(pt2[VA_PART2(va)] & PTE_VALID)) {
        if (!alloc) {
            return NULL;
        }
        pt3 = kalloc_page();
        memset((void *)pt3, 0, PAGE_SIZE);
        pt2[VA_PART2(va)] = K2P(pt3) | PTE_TABLE;
    }
    return pt3 + VA_PART3(va);
}

void init_pgdir(struct pgdir *pgdir)
{
    // lab3:
    pgdir->pt = NULL;

    // final:
    init_spinlock(&pgdir->lock);
    init_sections(&pgdir->section_head);
}

void free_pgdir(struct pgdir *pgdir)
{
    // TODO:
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    if (pgdir->pt == NULL) {
        return;
    }
    PTEntriesPtr pt[4];
    pt[0] = pgdir->pt;
    for (int i = 0; i != N_PTE_PER_TABLE; i++) {
        pt[1] = (PTEntriesPtr)P2K(PTE_ADDRESS(pt[0][i]));
        if (!(pt[0][i] & PTE_VALID)) {
            continue;
        }
        for (int j = 0; j != N_PTE_PER_TABLE; j++) {
            pt[2] = (PTEntriesPtr)P2K(PTE_ADDRESS(pt[1][j]));
            if (!(pt[1][j] & PTE_VALID)) {
                continue;
            }
            for (int k = 0; k != N_PTE_PER_TABLE; k++) {
                if (!(pt[2][k] & PTE_VALID)) {
                    continue;
                }
                kfree_page((void *)P2K(PTE_ADDRESS(pt[2][k])));
            }
            kfree_page((void *)pt[2]);
        }
        kfree_page((void *)pt[1]);
    }
    kfree_page((void *)pgdir->pt);
    pgdir->pt = NULL;
    // Final
    free_sections(pgdir);
}

void attach_pgdir(struct pgdir *pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

/**
 * Map virtual address 'va' to the physical address represented by kernel
 * address 'ka' in page directory 'pd', 'flags' is the flags for the page
 * table entry.
 */
void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags)
{
    /* (Final) TODO BEGIN */
    u64 pa = K2P(ka);
    PTEntriesPtr pte = get_pte(pd, va, TRUE);
    ASSERT(pte);
    *pte = PAGE_BASE(pa) | flags;
    arch_tlbi_vmalle1is();
    /* (Final) TODO END */
}

/*
 * Copy len bytes from p to user address va in page table pgdir.
 * Allocate physical pages if required.
 * Useful when pgdir is not the current page table.
 */
int copyout(struct pgdir *pd, void *va, void *p, usize len)
{
    /* (Final) TODO BEGIN */
    usize size = 0;
    while (size < len) {
        usize offset = VA_OFFSET(va);
        usize this_size = MIN(len, PAGE_SIZE - offset);
        PTEntriesPtr pte = get_pte(pd, (u64)va, TRUE);
        if (*pte == NULL) {
            void *new_page = kalloc_page();
            *pte = K2P(new_page) | PTE_USER_DATA;
        }
        memcpy((void *)(P2K(PTE_ADDRESS(*pte)) + offset), p, this_size);
        size += this_size;
        p += this_size;
        va += this_size;
    }
    return size == len ? 0 : -1;
    /* (Final) TODO END */
}