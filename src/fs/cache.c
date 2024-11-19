#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block device and super block to use.
            Correspondingly, you should NEVER use global instance of
            them, e.g. `get_super_block`, `block_device`

    @see init_bcache
 */
static const SuperBlock *sblock;

/**
    @brief the reference to the underlying block device.
 */
static const BlockDevice *device;

/**
    @brief global lock for block cache.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory block.

    We use a linked list to manage all allocated cached blocks.

    You can implement your own data structure if you like better performance.

    @see Block
 */
static ListNode head;

usize cachesize;

static LogHeader header; // in-memory copy of log header block.

/**
    @brief a struct to maintain other logging states.
    
    You may wonder where we store some states, e.g.
    
    * how many atomic operations are running?
    * are we checkpointing?
    * how to notify `end_op` that a checkpoint is done?

    Put them here!

    @see cache_begin_op, cache_end_op, cache_sync
 */
struct {
    /* your fields here */
} log;

// read the content from disk.
static INLINE void device_read(Block *block)
{
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block *block)
{
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header()
{
    device->read(sblock->log_start, (u8 *)&header);
}

// write log header back to disk.
static INLINE void write_header()
{
    device->write(sblock->log_start, (u8 *)&header);
}

// initialize a block struct.
static void init_block(Block *block)
{
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks()
{
    // TODO
    return cachesize;
}

// see `cache.h`.
static Block *cache_acquire(usize block_no)
{
    // TODO
    acquire_spinlock(&lock);
    Block *b = find_cache(block_no);
    if (b) {
        while (b->acquired) {
            release_spinlock(&lock);
            unalertable_wait_sem(&b->lock);
            acquire_spinlock(&lock);
            if (get_sem(&b->lock)) {
                b->acquired = TRUE;
                recently_used(b);
                release_spinlock(&lock);
                return b;
            }
        }
        get_sem(&b->lock);
        b->acquired = TRUE;
        recently_used(b);
        release_spinlock(&lock);
        return b;
    }

    if (get_num_cached_blocks() == EVICTION_THRESHOLD) {
        evict();
    }

    b = (Block *)kalloc(sizeof(Block));
    init_block(b);
    _get_sem(&b->lock);
    b->acquired = TRUE;
    b->block_no = block_no;

    _insert_into_list(head.prev, &b->node);

    device_read(b);
    b->valid = TRUE;
    release_spinlock(&lock);
    return b;
}

// see `cache.h`.
static void cache_release(Block *block)
{
    // TODO
    ASSERT(block->acquired);
    acquire_spinlock(&lock);
    block->acquired = FALSE;
    _post_sem(&block->lock);
    release_spinlock(&lock);
}

// see `cache.h`.
void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device)
{
    sblock = _sblock;
    device = _device;

    // TODO
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx)
{
    // TODO
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block)
{
    // TODO
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx)
{
    // TODO
}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx)
{
    // TODO
}

// see `cache.h`.
static void cache_free(OpContext *ctx, usize block_no)
{
    // TODO
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};

void recently_used(Block *b)
{
    if (&b->node == &head) {
        head = *head.next;
        return;
    }
    _detach_from_list(&b->node);
    _insert_into_list(head.prev, &b->node);
}

Block *find_cache(usize block_no)
{
    _for_in_list(p, &head)
    {
        Block *b = container_of(p, Block, node);
        if (b->block_no == block_no) {
            return b;
        }
    }
    return NULL;
}

bool evict()
{
    bool ret = FALSE;
    _for_in_list(p, &head)
    {
        Block *b = container_of(p, Block, node);
        if (!b->pinned) {
            if (p == &head) {
                head = *head.next;
            }
            _detach_from_list(p);
            ret = TRUE;
            return ret;
        }
    }
    return ret;
}