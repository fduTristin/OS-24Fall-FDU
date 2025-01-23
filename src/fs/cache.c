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

static usize cachesize = 0;

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
    SpinLock lock;
    Semaphore begin;
    Semaphore end;
    u64 num_ops;
    u64 blocks_allocated_but_unused;
} log;

Block *find_cache(usize);
void recently_used(Block *);
bool evict();
void wblog();
void create_checkpoint();

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
    block->acquired = FALSE;
    block->pinned = FALSE;

    init_sleeplock(&block->lock);
    block->valid = FALSE;
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
                break;
            }
        }
        if (!b->acquired) {
            get_sem(&b->lock);
            b->acquired = TRUE;
        }
        recently_used(b);
        release_spinlock(&lock);
        return b;
    }
    if (get_num_cached_blocks() >= EVICTION_THRESHOLD) {
        evict();
    }

    b = (Block *)kalloc(sizeof(Block));
    init_block(b);
    _get_sem(&b->lock);
    b->acquired = TRUE;
    b->block_no = block_no;

    _insert_into_list(head.prev, &b->node);
    cachesize++;
    release_spinlock(&lock);
    device_read(b);
    b->valid = TRUE;

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
    init_spinlock(&lock);
    init_list_node(&head);

    init_spinlock(&log.lock);
    init_sem(&log.end, 0);
    init_sem(&log.begin, 0);
    log.blocks_allocated_but_unused = 0;

    // restore the log
    create_checkpoint();
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx)
{
    // TODO
    acquire_spinlock(&log.lock);
    usize LOG_MAX = MIN(LOG_MAX_SIZE, sblock->num_blocks - 1);
    while (log.blocks_allocated_but_unused + OP_MAX_NUM_BLOCKS +
                   header.num_blocks >
           LOG_MAX) {
        _lock_sem(&(log.begin));
        release_spinlock(&log.lock);
        if (!_wait_sem(&(log.begin), FALSE)) {
            PANIC();
        };
        acquire_spinlock(&log.lock);
    }
    log.blocks_allocated_but_unused +=
            OP_MAX_NUM_BLOCKS; // Suppose this op uses maximum number of blocks in log
    log.num_ops++;
    ctx->rm = OP_MAX_NUM_BLOCKS;
    release_spinlock(&log.lock);
}

// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block)
{
    // TODO

    // if ctx is NULL, directly write to device

    if (!ctx) {
        device_write(block);
        return;
    }

    // if block in log, return
    acquire_spinlock(&log.lock);
    for (usize i = 0; i < header.num_blocks; i++) {
        if (header.block_no[i] == block->block_no) {
            release_spinlock(&log.lock);
            return;
        }
    }

    if (ctx->rm == 0)
        PANIC();

    // If the block is not in the log, we open a new log block for this block.
    header.num_blocks++;
    header.block_no[header.num_blocks - 1] = block->block_no;
    block->pinned = TRUE;
    ctx->rm--;
    log.blocks_allocated_but_unused--;
    release_spinlock(&log.lock);
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx)
{
    // TODO
    acquire_spinlock(&log.lock);
    log.num_ops--;
    log.blocks_allocated_but_unused -= ctx->rm;
    // If there are other commits to the log, we wait for them

    if (log.num_ops > 0) {
        _lock_sem(&(log.end));
        post_sem(&log.begin);
        release_spinlock(&log.lock);
        if (!_wait_sem(&(log.end), FALSE)) {
            PANIC();
        };
        return;
    }

    // After all the commiters call end_op, we write the cache back to the log,
    // sync the header in memory with the header in disk and finally write the checkpoint
    // to the disk.
    if (log.num_ops == 0) {
        wblog();
        write_header();
        create_checkpoint();
        post_all_sem(&log.end);
        post_all_sem(&log.begin);
    }
    release_spinlock(&log.lock);
}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx)
{
    // TODO
    if (ctx->rm <= 0)
        PANIC();

    usize num_bitmap_blocks =
            (sblock->num_data_blocks + BIT_PER_BLOCK - 1) / BIT_PER_BLOCK;

    for (usize i = 0; i < num_bitmap_blocks; i++) {
        Block *bitmap_block = cache_acquire(sblock->bitmap_start + i);
        for (usize j = 0; j < BLOCK_SIZE * 8; j++) {
            usize block_no = i * BLOCK_SIZE * 8 + j;
            if (block_no >= sblock->num_blocks) {
                cache_release(bitmap_block);
                PANIC();
            }
            if (!bitmap_get((BitmapCell *)bitmap_block->data, j)) {
                Block *b = cache_acquire(block_no);
                memset(b->data, 0, BLOCK_SIZE);
                cache_sync(ctx, b);
                bitmap_set((BitmapCell *)bitmap_block->data, j);
                cache_sync(ctx, bitmap_block);
                cache_release(b);
                cache_release(bitmap_block);
                return block_no;
            }
        }
        cache_release(bitmap_block);
    }
    return -1;
}

// see `cache.h`.
static void cache_free(OpContext *ctx, usize block_no)
{
    // TODO
    Block *bitmap_block =
            cache_acquire(block_no / (BLOCK_SIZE * 8) + sblock->bitmap_start);
    bitmap_clear((BitmapCell *)bitmap_block->data, block_no % (BLOCK_SIZE * 8));
    cache_sync(ctx, bitmap_block);
    cache_release(bitmap_block);
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
    _detach_from_list(&b->node);
    _insert_into_list(head.prev, &b->node);
}

Block *find_cache(usize block_no)
{
    _for_in_list(p, &head)
    {
        if (p == &head) {
            continue;
        }
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
        if (p == &head) {
            continue;
        }
        if (!b->pinned & !b->acquired) {
            ListNode *temp = p->prev;
            _detach_from_list(p);
            kfree(b);
            cachesize--;
            // try best to make cachesize below threshold
            ret = cachesize < EVICTION_THRESHOLD;
            if (ret) {
                return ret;
            }
            p = temp;
        }
    }
    return ret;
}

void wblog()
{
    // Walk through every block_no in the header and write the changes back to the log.
    for (usize i = 0; i < header.num_blocks; i++) {
        Block *b = cache_acquire(header.block_no[i]);
        device->write(sblock->log_start + i + 1, b->data);
        b->pinned = FALSE;
        cache_release(b);
    }
}

void create_checkpoint()
{
    read_header();
    Block temp;
    init_block(&temp);

    for (usize i = 0; i < header.num_blocks; i++) {
        temp.block_no = sblock->log_start + 1 + i;
        device_read(&temp);
        temp.block_no = header.block_no[i];
        device_write(&temp);
    }

    header.num_blocks = 0;
    write_header();
}