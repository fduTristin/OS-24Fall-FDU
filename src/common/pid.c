#include <common/pid.h>

void init_bitmap(BITMAP *b)
{
    for (u32 i = 0; i != BITMAP_SIZE; i++) {
        b->bitmap[i] = 0xFF;
    }
}

bool is_empty(BITMAP *b, u32 index)
{
    return b->bitmap[index] == 0;
}

void set_bit(BITMAP *b, u32 index, u32 subindex)
{
    unsigned char mask = 1 << subindex;
    b->bitmap[index] |= mask;
}

void clear_bit(BITMAP *b, u32 index, u32 subindex)
{
    unsigned char mask = ~(1 << subindex);
    b->bitmap[index] &= mask;
}

u32 find_bit(BITMAP *b, u32 index)
{
    ASSERT(b->bitmap[index]);
    u32 mask = 1;
    for (u32 i = 0; i != 8; i++) {
        if ((mask & b->bitmap[index])) {
            return i;
        } else {
            mask = mask << 1;
        }
    }
    return 0;
}

u32 alloc_pid(BITMAP *b)
{
    for (u32 index = 0; index != BITMAP_SIZE; index++) {
        if (is_empty(b, index)) {
            continue;
        } else {
            u32 subindex = find_bit(b, index);
            clear_bit(b, index, subindex);
            u32 pid = (index << 3) | subindex;
            return pid;
        }
    }
    return -1;
}

void free_pid(BITMAP *b, u32 pid)
{
    u32 index = pid >> 3;
    u32 subindex = pid & 0x7;
    set_bit(b, index, subindex);
}