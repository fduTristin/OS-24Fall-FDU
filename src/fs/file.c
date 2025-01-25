#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <fs/cache.h>
#include <fs/pipe.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

// the global file table.
// static struct ftable ftable;
static struct ftable ftable;

void init_ftable()
{
    // TODO: initialize your ftable.
    init_spinlock(&ftable.lock);
}

void init_oftable(struct oftable *oftable)
{
    // TODO: initialize your oftable for a new process.
    memset(oftable, NULL, NOFILE);
}

/* Allocate a file structure. */
struct file *file_alloc()
{
    /* (Final) TODO BEGIN */
    struct file *f;

    acquire_spinlock(&ftable.lock);
    for (f = ftable.file; f < ftable.file + NFILE; f++) {
        if (f->ref == 0) {
            f->ref = 1;
            release_spinlock(&ftable.lock);
            return f;
        }
    }
    release_spinlock(&ftable.lock);
    /* (Final) TODO END */
    printk("NO FILE LEFT!  ");
    PANIC();
    return NULL;
}

/* Increment ref count for file f. */
struct file *file_dup(struct file *f)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&ftable.lock);
    // if (f->ref < 1)
    //     printk("filedup\n");
    f->ref++;
    release_spinlock(&ftable.lock);
    /* (Final) TODO END */
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void file_close(struct file *f)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&ftable.lock);
    if (f->ref < 1)
        PANIC();
    if (--f->ref > 0) {
        release_spinlock(&ftable.lock);
        return;
    }
    if (f->type == FD_PIPE)
        pipe_close(f->pipe, f->writable);
    if (f->type == FD_INODE) {
        ASSERT(f->ip);
        release_spinlock(&ftable.lock);
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx, f->ip);
        bcache.end_op(&ctx);
        acquire_spinlock(&ftable.lock);
        f->type = FD_NONE;
        f->ref = 0;
        f->readable = f->writable = 0;
    }
    release_spinlock(&ftable.lock);
    /* (Final) TODO END */
}

/* Get metadata about file f. */
int file_stat(struct file *f, struct stat *st)
{
    /* (Final) TODO BEGIN */
    if (f->type == FD_INODE) {
        inodes.lock(f->ip);
        stati(f->ip, st);
        inodes.unlock(f->ip);
        return 0;
    }
    /* (Final) TODO END */
    return -1;
}

/* Read from file f. */
isize file_read(struct file *f, char *addr, isize n)
{
    /* (Final) TODO BEGIN */

    if (f->type == FD_PIPE)
        return pipe_read(f->pipe, (u64)addr, n);
    if (f->type == FD_INODE) {
        inodes.lock(f->ip);
        isize ret = 0;
        if ((ret = (isize)inodes.read(f->ip, (u8 *)addr, f->off, n)) > 0)
            f->off += ret;
        inodes.unlock(f->ip);
        return ret;
    }
    /* (Final) TODO END */
    return -1;
}

/* Write to file f. */
isize file_write(File *f, char *addr, isize n)
{
    // printk("(in file_write) size = %lld\n", n);
    /* (Final) TODO BEGIN */
    isize ret = 0;

    if (f->type == FD_PIPE)
        return pipe_write(f->pipe, (u64)addr, n);
    if (f->type == FD_INODE) {
        // write a few blocks at a time to avoid exceeding
        // the maximum log transaction size

        // limit size of file_write
        usize write_size = MIN(INODE_MAX_BYTES - f->off, (usize)n);

        usize write_pointer = 0; // where to write next
        while (write_pointer != write_size) {
            usize size = MIN(
                    write_size - write_pointer,
                    (usize)((OP_MAX_NUM_BLOCKS - 4) *
                            BLOCK_SIZE)); // I assume it safe to use a bit less than maximum numbers of OP_BLOCKS
            OpContext ctx;
            bcache.begin_op(&ctx);
            inodes.lock(f->ip);
            if (inodes.write(&ctx, f->ip, (u8 *)(addr + write_pointer), f->off,
                             size) != size) {
                inodes.unlock(f->ip);
                bcache.end_op(&ctx);
                return -1;
            }
            inodes.unlock(f->ip);
            bcache.end_op(&ctx);
            f->off += size;
            write_pointer += size;
            ret += size;
        }

        return ret;
    }
    /* (Final) TODO END */
    return -1;
}

usize get_file_ref(File *f)
{
    acquire_spinlock(&ftable.lock);
    auto ret = f->ref;
    release_spinlock(&ftable.lock);
    return ret;
}