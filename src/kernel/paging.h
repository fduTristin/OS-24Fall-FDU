#pragma once

#include <aarch64/mmu.h>
#include <kernel/proc.h>

#define SECTION_TYPE u16
#define ST_FILE 1
#define ST_SWAP (1 << 1)
#define ST_RO (1 << 2)
#define ST_HEAP (1 << 3)
#define ST_TEXT (ST_FILE | ST_RO)
#define ST_DATA ST_FILE
#define ST_BSS ST_FILE
#define ST_USER_STACK (1 << 4)
#define ST_MMAP_SHARED (1 << 5)
#define ST_MMAP_PRIVATE (1 << 6)

struct section {
    u64 flags;
    u64 begin;
    u64 end;
    ListNode stnode;
    u64 prot; // mmap

    /* The following fields are for the file-backed sections. */

    struct file *fp;
    u64 offset; // Offset in file
    u64 length; // Length of mapped content in file
};

void init_section(struct section *);
int pgfault_handler(u64 iss);
void init_sections(ListNode *section_head);
void free_pages_of_section(struct pgdir *pd, struct section *sec);
void free_sections(struct pgdir *pd);
void copy_sections(ListNode *from_head, ListNode *to_head);
u64 sbrk(i64 size);
