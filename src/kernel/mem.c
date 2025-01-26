#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <common/string.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/list.h>

#define KERNELSTOP P2K(PHYSTOP)

RefCount kalloc_page_cnt;

SpinLock lock1, lock2;

extern char end[];
static void *zero_page;
struct page pages[ALL_PAGE_COUNT];

ListNode *free_pages = NULL;

typedef struct slab {
    struct slab *next;
} slab;

slab *slabs[512];

void new_slab(u64 index)
{
    u64 size = (index + 1) * 8;
    slab *new_slab = (slab *)kalloc_page();
    int *idx = (int *)new_slab;
    *idx = index;
    new_slab = (slab *)((u64)new_slab + 8);
    slab *s = new_slab;
    int free_count = 1;
    while (1) {
        u64 addr = (u64)s;
        if (addr + 2 * size > (u64)new_slab + PAGE_SIZE - 8) {
            s->next = NULL;
            break;
        }
        s->next = (slab *)(addr + size);
        s = s->next;
        free_count += 1;
    }
    *(idx + 1) = free_count;
    slabs[index] = new_slab;
}

void *fetch_slab(u64 size)
{
    u64 index = size % 8 ? (size / 8) : ((size / 8) - 1);
    slab *ret = NULL;
    if (slabs[index] == NULL) {
        new_slab(index);
        ret = slabs[index];
        u64 page = (u64)ret & ~4095;
        int *free_count = (int *)(page + 4);
        *free_count -= 1;
        slabs[index] = slabs[index]->next;
    } else {
        ret = slabs[index];
        u64 page_addr = (u64)ret & ~4095;
        int *free_count = (int *)(page_addr + 4);
        *free_count -= 1;
        slabs[index] = slabs[index]->next;
    }
    return (void *)ret;
}

void free_slab(slab *s, int index)
{
    slab *head = slabs[index];
    s->next = head;
    slabs[index] = s;
}

void kinit()
{
    init_rc(&kalloc_page_cnt);
    init_spinlock(&lock1);
    init_spinlock(&lock2);

    u64 addr = (u64)end;
    zero_page = (void *)(PAGE_BASE(addr) + PAGE_SIZE);
    memset(zero_page, 0, PAGE_SIZE);
    free_pages = (ListNode *)(zero_page + PAGE_SIZE);
    ListNode *cur = free_pages;
    while (1) {
        if ((((u64)cur) + PAGE_SIZE) == KERNELSTOP) {
            cur->next = NULL;
            break;
        }
        ListNode *next = (ListNode *)(((u64)cur) + PAGE_SIZE);
        cur->next = next;
        next->prev = cur;
        cur = next;
    }
    for (int i = 0; i != 512; i++) {
        slabs[i] = NULL;
    }
    for (int i = 0; i != ALL_PAGE_COUNT; i++) {
        if (pages[i].ref.count) {
            printk("pages[%d]: %lld\n", i, pages[i].ref.count);
        }
    }
}

void *kalloc_page()
{
    increment_rc(&kalloc_page_cnt);
    acquire_spinlock(&lock1);
    void *ret = (void *)(free_pages);
    if (ret == NULL) {
        PANIC();
    }
    free_pages = free_pages->next;
    release_spinlock(&lock1);
    ASSERT(pages[PAGE_INDEX(ret)].ref.count == 0);
    increment_rc(&pages[PAGE_INDEX(ret)].ref);
    // printk("pages: %lld\n", left_page_cnt());
    return ret;
}

void kfree_page(void *p)
{
    if (p == zero_page)
        return;
    u64 idx = PAGE_INDEX(p);
    if (decrement_rc(&pages[idx].ref)) {
        // printk("release_page: %p\n", p);
        decrement_rc(&kalloc_page_cnt);
        acquire_spinlock(&lock1);
        free_pages->prev = (ListNode *)p;
        free_pages->prev->next = free_pages;
        free_pages = free_pages->prev;
        release_spinlock(&lock1);
    }
    // printk("pages: %lld\n", left_page_cnt());
    return;
}

void *kalloc(u64 size)
{
    acquire_spinlock(&lock2);
    void *ret = fetch_slab(size);
    release_spinlock(&lock2);
    return ret;
}

void kfree(void *ptr)
{
    acquire_spinlock(&lock2);
    u64 page = ((u64)ptr) & ~4095;
    int *idx = (int *)page;
    int index = *idx;
    *(idx + 1) += 1; //free_block + 1
    u64 slabsize = (index + 1) << 3;
    if (*(idx + 1) == (int)((PAGE_SIZE - 8) / slabsize)) {
        slab *cur = slabs[index];
        u64 addr_cur = (u64)cur;
        while (addr_cur >= page && addr_cur < page + PAGE_SIZE && cur != NULL) {
            cur = cur->next;
            addr_cur = (u64)cur;
        }
        slabs[index] = cur;

        while (cur != NULL) {
            u64 addr_next = (u64)(cur->next);
            if (addr_next >= page && addr_next < page + PAGE_SIZE &&
                cur->next != NULL) {
                cur->next = cur->next->next;
            } else if (cur->next == NULL) {
                break;
            } else {
                cur = cur->next;
            }
        }
        kfree_page((void *)page);
    } else {
        free_slab((slab *)ptr, index);
    }
    release_spinlock(&lock2);
    return;
}

void *get_zero_page()
{
    return zero_page;
}

u64 left_page_cnt()
{
    return ALLOCATABLE_PAGE_COUNT - kalloc_page_cnt.count;
}

void kshare_page(u64 addr)
{
    // printk("share page: %llx\n",addr);
    u64 index = PAGE_INDEX(PAGE_BASE(addr));
    increment_rc(&pages[index].ref);
}

u64 get_page_ref(u64 addr)
{
    auto index = PAGE_INDEX(PAGE_BASE(addr));
    acquire_spinlock(&lock1);
    auto ret = pages[index].ref.count;
    release_spinlock(&lock1);
    return ret;
}