#include <aarch64/mmu.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <common/list.h>

#define KERNELSTOP P2K(PHYSTOP)

RefCount kalloc_page_cnt;

SpinLock lock1, lock2;

extern char end[];

ListNode* free_pages = NULL;

typedef struct slab {
    struct slab* next;
} slab;

slab* slabs[512];

void new_slab(u64 index)
{
    u64 size = (index + 1) * 8;
    slab* new_slab = (slab*)kalloc_page();
    int* idx = (int*)new_slab;
    *idx = index;
    new_slab = (slab*)((u64)new_slab + 8);
    slab* s = new_slab;
    while(1)
    {
        u64 addr = (u64)s;
        if(addr + 2 * size > (u64)new_slab + PAGE_SIZE - 8)
        {
            s->next = NULL;
            break;
        }
        s->next = (slab*)(addr + size);
        s = s->next;
    }
    slabs[index] = new_slab;
}

void* fetch_slab(u64 size)
{
    u64 index = size % 8 ? (size / 8) : ((size / 8) - 1);
    slab* ret = NULL;
    if(slabs[index] == NULL)
    {
        new_slab(index);
        ret = slabs[index];
        slabs[index] = slabs[index]->next;
    }
    else{
        ret = slabs[index];
        slabs[index] = slabs[index]->next;
    }
    return (void*)ret;
}

void free_slab(slab* s, int index)
{
    slab* head = slabs[index];
    s->next = head;
    slabs[index] = s;
}

void kinit() {
    init_rc(&kalloc_page_cnt);
    init_spinlock(&lock1);
    init_spinlock(&lock2);

    u64 addr = (u64)end;
    u64 MEMBEGIN = ((addr >> 12) + 1) << 12;
    free_pages = (ListNode*)MEMBEGIN;
    ListNode* cur = free_pages;
    while(1)
    {
        if((((u64)cur) + PAGE_SIZE) == KERNELSTOP)
        {
            cur->next = NULL;
            break;
        }
        ListNode* next = (ListNode*)(((u64)cur) + PAGE_SIZE);
        cur->next = next;
        next->prev = cur;
        cur = next;
    }
    for(int i = 0; i != 512; i++)
    {
        slabs[i] = NULL;
    }
}

void* kalloc_page() {
    increment_rc(&kalloc_page_cnt);
    acquire_spinlock(&lock1);
    void* ret = (void*)(free_pages);
    free_pages = free_pages->next;
    release_spinlock(&lock1);    
    return ret;
}

void kfree_page(void* p) {
    decrement_rc(&kalloc_page_cnt);
    acquire_spinlock(&lock1);
    free_pages->prev = (ListNode*)p;
    free_pages->prev->next = free_pages;
    free_pages = free_pages->prev;
    release_spinlock(&lock1);   
    return;
}

void* kalloc(u64 size) {
    acquire_spinlock(&lock2);
    void* ret = fetch_slab(size);
    release_spinlock(&lock2);       
    return ret;
}

void kfree(void* ptr) {
    acquire_spinlock(&lock2);
    u64 page = (((u64)ptr) >> 12) << 12;
    int* type = (int*)page;
    int index = *type;
    free_slab((slab*)ptr,index);
    release_spinlock(&lock2);  
    return;
}
