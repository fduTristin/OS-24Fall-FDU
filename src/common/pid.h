#pragma once

#include <common/defines.h>
#define MAX_PID 0x80000
#define BITMAP_SIZE (MAX_PID / 8) + 1

typedef struct BITMAP {
    unsigned char bitmap[BITMAP_SIZE];
} BITMAP;

void init_bitmap(BITMAP *b);

bool is_empty(BITMAP *b, u32 index);

void set_bit(BITMAP *b, u32 index, u32 subindex);

void clear_bit(BITMAP *b, u32 index, u32 subindex);

u32 find_bit(BITMAP *b, u32 index);

u32 alloc_pid(BITMAP *b);

void free_pid(BITMAP *b, u32 pid);