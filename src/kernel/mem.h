#pragma once
#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/rc.h>

#define ALLOCATABLE_PAGE_COUNT \
    ((P2K(PHYSTOP) - PAGE_BASE((u64) & end)) / PAGE_SIZE - 2) // exclude zero_page
#define ALL_PAGE_COUNT ((PHYSTOP - EXTMEM) / PAGE_SIZE)
#define PAGE_INDEX(page) ((KSPACE((u64)page) - P2K(EXTMEM)) / PAGE_SIZE)

struct page {
    RefCount ref;
};

void kinit();
u64 left_page_cnt();

WARN_RESULT void *kalloc_page();
void kfree_page(void *);

WARN_RESULT void *kalloc(unsigned long long);
void kfree(void *);

WARN_RESULT void *get_zero_page();
void kshare_page(u64);